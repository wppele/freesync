#include "framework.h"
#include "SyncEngine.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <windows.h>

namespace fs = std::filesystem;

namespace
{
    void WriteLog(SyncLogCallback& log, const std::wstring& message)
    {
        if (log)
        {
            log(message);
        }
    }

    std::wstring ErrorMessage(const std::wstring& action, const fs::path& path, const std::error_code& ec)
    {
        const std::string message = ec.message();
        const std::wstring wideMessage(message.begin(), message.end());
        std::wstringstream ss;
        ss << L"[失败] " << action << L": " << path.wstring() << L"，原因: " << wideMessage;
        return ss.str();
    }

    bool ShouldCopyFile(const fs::path& sourceFile, const fs::path& targetFile)
    {
        std::error_code ec;
        if (!fs::exists(targetFile, ec))
        {
            return true;
        }

        const auto sourceSize = fs::file_size(sourceFile, ec);
        if (ec)
        {
            return false;
        }

        const auto targetSize = fs::file_size(targetFile, ec);
        if (ec || sourceSize != targetSize)
        {
            return true;
        }

        const auto sourceTime = fs::last_write_time(sourceFile, ec);
        if (ec)
        {
            return false;
        }

        const auto targetTime = fs::last_write_time(targetFile, ec);
        if (ec)
        {
            return true;
        }

        return sourceTime > targetTime;
    }

    void CopyFileIncremental(const fs::path& sourceFile, const fs::path& targetFile, SyncStats& stats, SyncLogCallback& log)
    {
        std::error_code ec;
        fs::create_directories(targetFile.parent_path(), ec);
        if (ec)
        {
            ++stats.failedFiles;
            WriteLog(log, ErrorMessage(L"创建目录", targetFile.parent_path(), ec));
            return;
        }

        if (!ShouldCopyFile(sourceFile, targetFile))
        {
            ++stats.skippedFiles;
            return;
        }

        const fs::path tempFile = targetFile.wstring() + L".freesync.tmp";
        fs::copy_file(sourceFile, tempFile, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            ++stats.failedFiles;
            WriteLog(log, ErrorMessage(L"复制文件", sourceFile, ec));
            return;
        }

        fs::rename(tempFile, targetFile, ec);
        if (ec)
        {
            fs::remove(targetFile, ec);
            ec.clear();
            fs::rename(tempFile, targetFile, ec);
            if (ec)
            {
                ++stats.failedFiles;
                WriteLog(log, ErrorMessage(L"替换文件", targetFile, ec));
                return;
            }
        }

        const auto sourceTime = fs::last_write_time(sourceFile, ec);
        if (!ec)
        {
            fs::last_write_time(targetFile, sourceTime, ec);
        }

        ++stats.copiedFiles;
        WriteLog(log, L"[复制] " + sourceFile.wstring() + L" -> " + targetFile.wstring());
    }

    void DeleteExtraTargetFiles(const fs::path& sourceRoot, const fs::path& targetRoot, SyncStats& stats, SyncLogCallback& log)
    {
        std::error_code ec;
        if (!fs::exists(targetRoot, ec))
        {
            return;
        }

        std::vector<fs::path> extraPaths;
        for (const auto& entry : fs::recursive_directory_iterator(targetRoot, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec)
            {
                ++stats.failedFiles;
                WriteLog(log, ErrorMessage(L"遍历目标目录", targetRoot, ec));
                ec.clear();
                continue;
            }

            const fs::path relativePath = fs::relative(entry.path(), targetRoot, ec);
            if (ec)
            {
                ++stats.failedFiles;
                WriteLog(log, ErrorMessage(L"计算相对路径", entry.path(), ec));
                ec.clear();
                continue;
            }

            const fs::path sourcePath = sourceRoot / relativePath;
            if (!fs::exists(sourcePath, ec))
            {
                extraPaths.push_back(entry.path());
            }
        }

        for (auto it = extraPaths.rbegin(); it != extraPaths.rend(); ++it)
        {
            fs::remove_all(*it, ec);
            if (ec)
            {
                ++stats.failedFiles;
                WriteLog(log, ErrorMessage(L"删除目标多余项", *it, ec));
                ec.clear();
            }
            else
            {
                ++stats.deletedFiles;
                WriteLog(log, L"[删除] " + it->wstring());
            }
        }
    }
}

namespace
{
    std::atomic<bool> g_isMonitoring{ false };
    std::thread g_monitorThread;
    HANDLE g_stopEvent = nullptr;

    void MonitorWorker(std::vector<SyncPair> pairs, SyncOptions options, SyncLogCallback log, ProgressCallback progress)
    {
        std::vector<HANDLE> handles;
        for (const auto& pair : pairs)
        {
            // 监控源文件夹
            HANDLE h = FindFirstChangeNotificationW(pair.source.c_str(), TRUE, 
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
            if (h != INVALID_HANDLE_VALUE && h != nullptr)
            {
                handles.push_back(h);
            }

            // 如果是双向同步，同时监控目标文件夹
            if (pair.isBidirectional)
            {
                HANDLE hTarget = FindFirstChangeNotificationW(pair.target.c_str(), TRUE, 
                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
                if (hTarget != INVALID_HANDLE_VALUE && hTarget != nullptr)
                {
                    handles.push_back(hTarget);
                }
            }
        }

        if (handles.empty()) return;

        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        handles.push_back(g_stopEvent); // 退出事件句柄位于最后

        while (g_isMonitoring)
        {
            DWORD waitResult = WaitForMultipleObjects((DWORD)handles.size(), handles.data(), FALSE, INFINITE);
            
            if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0 + handles.size() - 1)
            {
                int triggeredIndex = waitResult - WAIT_OBJECT_0;
                
                // 找出是哪个 pair 被触发了，并且是哪个路径触发的
                int pairIndex = -1;
                bool isTargetTriggered = false;
                int currentHandleCount = 0;
                for (size_t i = 0; i < pairs.size(); ++i)
                {
                    int handleCount = pairs[i].isBidirectional ? 2 : 1;
                    if (triggeredIndex < currentHandleCount + handleCount)
                    {
                        pairIndex = (int)i;
                        if (pairs[i].isBidirectional && (triggeredIndex == currentHandleCount + 1))
                        {
                            isTargetTriggered = true;
                        }
                        break;
                    }
                    currentHandleCount += handleCount;
                }
                
                if (pairIndex == -1) continue;

                // 消耗掉当前触发的通知，准备开始防抖等待
                FindNextChangeNotification(handles[triggeredIndex]);

                // 防抖: 循环等待直到 2 秒内没有新事件
                bool isSettled = false;
                while (!isSettled && g_isMonitoring)
                {
                    HANDLE waitHandles[2] = { handles[triggeredIndex], g_stopEvent };
                    DWORD debounceWait = WaitForMultipleObjects(2, waitHandles, FALSE, 2000);
                    
                    if (debounceWait == WAIT_TIMEOUT)
                    {
                        isSettled = true; // 2秒内无新变化
                    }
                    else if (debounceWait == WAIT_OBJECT_0)
                    {
                        FindNextChangeNotification(handles[triggeredIndex]); // 消耗并继续等待
                    }
                    else // 收到退出信号等
                    {
                        break;
                    }
                }

                if (isSettled && g_isMonitoring)
                {
                    // 记录触发变动的根目录
                    pairs[pairIndex].triggeredRoot = isTargetTriggered ? pairs[pairIndex].target : pairs[pairIndex].source;
                    
                    WriteLog(log, L"[监控] 检测到变动，触发同步: " + pairs[pairIndex].source + L" <-> " + pairs[pairIndex].target);
                    SyncFolderPair(pairs[pairIndex], options, log, progress);
                    
                    // 同步完成后，清理该任务关联的所有句柄在同步期间产生的积压信号（防止反馈环路）
                    int pairStartIdx = 0;
                    for (int i = 0; i < pairIndex; ++i) 
                        pairStartIdx += (pairs[i].isBidirectional ? 2 : 1);
                    
                    int numHandles = pairs[pairIndex].isBidirectional ? 2 : 1;
                    for (int i = 0; i < numHandles; ++i)
                    {
                        while (WaitForSingleObject(handles[pairStartIdx + i], 0) == WAIT_OBJECT_0)
                        {
                            FindNextChangeNotification(handles[pairStartIdx + i]);
                        }
                    }

                    pairs[pairIndex].triggeredRoot = L"";
                }
            }
            else if (waitResult == WAIT_OBJECT_0 + handles.size() - 1)
            {
                break; // 收到退出信号
            }
        }

        for (size_t i = 0; i < handles.size() - 1; ++i)
        {
            FindCloseChangeNotification(handles[i]);
        }
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}

void StartMonitoring(const std::vector<SyncPair>& pairs, const SyncOptions& options, SyncLogCallback log, ProgressCallback progress)
{
    if (g_isMonitoring) return;
    g_isMonitoring = true;
    g_monitorThread = std::thread(MonitorWorker, pairs, options, log, progress);
}

void StopMonitoring()
{
    if (!g_isMonitoring) return;
    g_isMonitoring = false;
    
    if (g_stopEvent)
    {
        SetEvent(g_stopEvent);
    }
    
    if (g_monitorThread.joinable())
    {
        g_monitorThread.join(); 
    }
}

bool IsMonitoring()
{
    return g_isMonitoring;
}

bool IsPathAvailable(const std::wstring& path)
{
    std::error_code ec;
    return fs::exists(fs::path(path), ec);
}

SyncStats SyncFolderPair(const SyncPair& pair, const SyncOptions& options, SyncLogCallback log, ProgressCallback progress)
{
    SyncStats stats;
    const fs::path rootA(pair.source);
    const fs::path rootB(pair.target);

    // 统一同步逻辑：从 src 同步到 dst
    auto syncOneWay = [&](const fs::path& src, const fs::path& tgt)
    {
        std::error_code ec;
        if (!fs::exists(src, ec) || !fs::is_directory(src, ec)) return;
        fs::create_directories(tgt, ec);

        // 统计总文件数用于计算进度
        size_t totalItems = 0;
        for (const auto& entry : fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied, ec))
        {
            if (!ec) totalItems++;
            else ec.clear();
        }

        size_t processedItems = 0;
        for (const auto& entry : fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec) { ec.clear(); continue; }

            const fs::path relativePath = fs::relative(entry.path(), src, ec);
            const fs::path targetPath = tgt / relativePath;
            
            if (entry.is_directory(ec))
            {
                fs::create_directories(targetPath, ec);
            }
            else if (entry.is_regular_file(ec))
            {
                CopyFileIncremental(entry.path(), targetPath, stats, log);
            }

            processedItems++;
            if (progress && totalItems > 0)
            {
                progress((float)processedItems / totalItems);
            }
        }
    };

    WriteLog(log, L"开始同步: " + rootA.wstring() + L" <-> " + rootB.wstring());

    if (pair.isBidirectional)
    {
        // 双向同步：优先同步有变动的一侧
        const bool targetTriggered = (!pair.triggeredRoot.empty() && pair.triggeredRoot == rootB.wstring());
        
        if (targetTriggered)
        {
            syncOneWay(rootB, rootA);
            if (options.deleteExtraFiles) DeleteExtraTargetFiles(rootB, rootA, stats, log);
            syncOneWay(rootA, rootB);
            if (options.deleteExtraFiles) DeleteExtraTargetFiles(rootA, rootB, stats, log);
        }
        else
        {
            syncOneWay(rootA, rootB);
            if (options.deleteExtraFiles) DeleteExtraTargetFiles(rootA, rootB, stats, log);
            syncOneWay(rootB, rootA);
            if (options.deleteExtraFiles) DeleteExtraTargetFiles(rootB, rootA, stats, log);
        }
    }
    else
    {
        syncOneWay(rootA, rootB);
        if (options.deleteExtraFiles)
        {
            DeleteExtraTargetFiles(rootA, rootB, stats, log);
        }
    }

    WriteLog(log, L"完成同步: 复制 " + std::to_wstring(stats.copiedFiles) +
        L"，跳过 " + std::to_wstring(stats.skippedFiles) +
        L"，删除 " + std::to_wstring(stats.deletedFiles) +
        L"，失败 " + std::to_wstring(stats.failedFiles));

    return stats;
}

SyncStats SyncFolderPairs(const std::vector<SyncPair>& pairs, const SyncOptions& options, SyncLogCallback log, ProgressCallback progress)
{
    SyncStats total;
    for (const auto& pair : pairs)
    {
        SyncStats current = SyncFolderPair(pair, options, log, progress);
        total.copiedFiles += current.copiedFiles;
        total.skippedFiles += current.skippedFiles;
        total.deletedFiles += current.deletedFiles;
        total.failedFiles += current.failedFiles;
    }

    WriteLog(log, L"全部任务完成: 复制 " + std::to_wstring(total.copiedFiles) +
        L"，跳过 " + std::to_wstring(total.skippedFiles) +
        L"，删除 " + std::to_wstring(total.deletedFiles) +
        L"，失败 " + std::to_wstring(total.failedFiles));

    return total;
}
