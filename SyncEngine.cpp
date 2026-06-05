#include "framework.h"
#include "SyncEngine.h"

#include <chrono>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <windows.h>

#include <future>
#include <unordered_set>
#include <shlobj.h>

namespace fs = std::filesystem;

namespace
{
    std::wstring GetSnapshotFilePath(const std::wstring& source, const std::wstring& target)
    {
        WCHAR appData[MAX_PATH]{};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
        {
            std::wstring folder = std::wstring(appData) + L"\\FreeSync";
            std::error_code ec;
            fs::create_directories(folder, ec);
            std::hash<std::wstring> hasher;
            size_t hash = hasher(source) ^ (hasher(target) << 1);
            return folder + L"\\" + std::to_wstring(hash) + L".snapshot";
        }
        return L"";
    }

    std::wstring GetSnapshotFilePath(const SyncPair& pair)
    {
        const bool hasStableSource = !pair.sourceVolumeGuid.empty() && !pair.sourceRelativePath.empty();
        const bool hasStableTarget = !pair.targetVolumeGuid.empty() && !pair.targetRelativePath.empty();
        if (hasStableSource && hasStableTarget)
        {
            return GetSnapshotFilePath(
                pair.sourceVolumeGuid + L"|" + pair.sourceRelativePath,
                pair.targetVolumeGuid + L"|" + pair.targetRelativePath);
        }

        return GetSnapshotFilePath(pair.source, pair.target);
    }

    SnapshotMap LoadSnapshot(const std::wstring& filePath)
    {
        SnapshotMap snapshot;
        FILE* file = nullptr;
        if (_wfopen_s(&file, filePath.c_str(), L"rt, ccs=UTF-8") == 0 && file)
        {
            WCHAR line[2048];
            while (fgetws(line, 2048, file))
            {
                WCHAR relPath[1024]{};
                uintmax_t size = 0;
                long long lastWriteTime = 0;
                int isDirectory = 0;
                if (swscanf_s(line, L"%1023[^|]|%d|%ju|%lld", relPath, (unsigned)_countof(relPath), &isDirectory, &size, &lastWriteTime) == 4)
                {
                    snapshot[relPath] = { size, lastWriteTime, isDirectory != 0 };
                }
            }
            fclose(file);
        }
        return snapshot;
    }

    void SaveSnapshot(const std::wstring& filePath, const SnapshotMap& snapshot)
    {
        if (filePath.empty()) return;
        std::wstring tmpPath = filePath + L".tmp";
        FILE* file = nullptr;
        if (_wfopen_s(&file, tmpPath.c_str(), L"wt, ccs=UTF-8") == 0 && file)
        {
            for (const auto& [relPath, info] : snapshot)
            {
                fwprintf(file, L"%s|%d|%ju|%lld\n", relPath.c_str(), info.isDirectory ? 1 : 0, info.size, info.lastWriteTime);
            }
            fclose(file);
            
            std::error_code ec;
            fs::rename(tmpPath, filePath, ec);
            if (ec)
            {
                // Fallback if rename fails (e.g., cross-device link, though unlikely here)
                fs::copy_file(tmpPath, filePath, fs::copy_options::overwrite_existing, ec);
                fs::remove(tmpPath, ec);
            }
        }
    }

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

    std::wstring GetVolumeRootForPath(const std::wstring& path)
    {
        if (path.empty())
        {
            return L"";
        }

        WCHAR root[MAX_PATH * 4]{};
        if (GetVolumePathNameW(path.c_str(), root, ARRAYSIZE(root)))
        {
            return root;
        }

        return L"";
    }

    bool CapturePathVolumeInfo(const std::wstring& path, std::wstring& volumeGuid, std::wstring& relativePath)
    {
        const std::wstring volumeRoot = GetVolumeRootForPath(path);
        if (volumeRoot.empty())
        {
            return false;
        }

        WCHAR volumeName[MAX_PATH * 4]{};
        if (!GetVolumeNameForVolumeMountPointW(volumeRoot.c_str(), volumeName, ARRAYSIZE(volumeName)))
        {
            return false;
        }

        volumeGuid = volumeName;
        relativePath = path;
        if (relativePath.size() >= volumeRoot.size())
        {
            relativePath = relativePath.substr(volumeRoot.size());
        }

        while (!relativePath.empty() && (relativePath.front() == L'\\' || relativePath.front() == L'/'))
        {
            relativePath.erase(relativePath.begin());
        }

        return !volumeGuid.empty();
    }

    std::vector<std::wstring> GetMountPointsForVolume(const std::wstring& volumeGuid)
    {
        std::vector<std::wstring> mountPoints;
        if (volumeGuid.empty())
        {
            return mountPoints;
        }

        DWORD requiredLength = 0;
        if (!GetVolumePathNamesForVolumeNameW(volumeGuid.c_str(), nullptr, 0, &requiredLength) &&
            GetLastError() != ERROR_MORE_DATA)
        {
            return mountPoints;
        }

        if (requiredLength == 0)
        {
            return mountPoints;
        }

        std::vector<WCHAR> buffer(requiredLength + 1, L'\0');
        if (!GetVolumePathNamesForVolumeNameW(volumeGuid.c_str(), buffer.data(), (DWORD)buffer.size(), &requiredLength))
        {
            return mountPoints;
        }

        const WCHAR* current = buffer.data();
        while (*current)
        {
            mountPoints.emplace_back(current);
            current += wcslen(current) + 1;
        }

        return mountPoints;
    }

    std::wstring CombineRootAndRelativePath(std::wstring root, const std::wstring& relativePath)
    {
        if (root.empty())
        {
            return L"";
        }

        if (!root.empty() && root.back() != L'\\' && root.back() != L'/')
        {
            root += L"\\";
        }

        std::wstring rel = relativePath;
        while (!rel.empty() && (rel.front() == L'\\' || rel.front() == L'/'))
        {
            rel.erase(rel.begin());
        }

        return root + rel;
    }

    bool TryResolvePathByVolumeInfo(std::wstring& path, const std::wstring& volumeGuid, const std::wstring& relativePath, const std::wstring& label, SyncLogCallback& log)
    {
        std::error_code ec;
        if (!path.empty() && fs::exists(path, ec))
        {
            return true;
        }
        ec.clear();

        if (volumeGuid.empty())
        {
            WriteLog(log, L"[路径不可用] " + label + L": " + path + L" (缺少卷标识，无法自动恢复)");
            return false;
        }

        std::vector<std::wstring> candidateRoots = GetMountPointsForVolume(volumeGuid);
        candidateRoots.push_back(volumeGuid);

        for (const auto& root : candidateRoots)
        {
            const std::wstring candidate = CombineRootAndRelativePath(root, relativePath);
            if (!candidate.empty() && fs::exists(candidate, ec))
            {
                if (candidate != path)
                {
                    WriteLog(log, L"[路径已恢复] " + label + L": " + path + L" -> " + candidate);
                    path = candidate;
                }
                return true;
            }
            ec.clear();
        }

        WriteLog(log, L"[路径不可用] " + label + L": " + path + L" (请检查磁盘是否已插入)");
        return false;
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
            stats.failedFiles++;
            WriteLog(log, ErrorMessage(L"创建目录", targetFile.parent_path(), ec));
            return;
        }

        if (!ShouldCopyFile(sourceFile, targetFile))
        {
            stats.skippedFiles++;
            return;
        }

        const fs::path tempFile = targetFile.wstring() + L".freesync.tmp";
        
        // Use CopyFileExW for better performance, especially on large files
        BOOL copyResult = CopyFileExW(sourceFile.wstring().c_str(), tempFile.wstring().c_str(), nullptr, nullptr, nullptr, 0);
        
        if (!copyResult)
        {
            stats.failedFiles++;
            WriteLog(log, ErrorMessage(L"复制文件", sourceFile, std::error_code(GetLastError(), std::system_category())));
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
                stats.failedFiles++;
                WriteLog(log, ErrorMessage(L"替换文件", targetFile, ec));
                return;
            }
        }

        const auto sourceTime = fs::last_write_time(sourceFile, ec);
        if (!ec)
        {
            fs::last_write_time(targetFile, sourceTime, ec);
        }

        stats.copiedFiles++;
        WriteLog(log, L"[复制] " + sourceFile.wstring() + L" -> " + targetFile.wstring());
    }

    void DeleteExtraTargetFiles(const fs::path& sourceRoot, const fs::path& targetRoot, SyncStats& stats, SyncLogCallback& log)
    {
        std::error_code ec;
        if (!fs::exists(targetRoot, ec))
        {
            return;
        }

        std::unordered_set<std::wstring> sourceFiles;
        for (const auto& entry : fs::recursive_directory_iterator(sourceRoot, fs::directory_options::skip_permission_denied, ec))
        {
            if (!ec) {
                sourceFiles.insert(fs::relative(entry.path(), sourceRoot, ec).wstring());
            } else {
                ec.clear();
            }
        }

        std::vector<fs::path> extraPaths;
        for (const auto& entry : fs::recursive_directory_iterator(targetRoot, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec)
            {
                stats.failedFiles++;
                WriteLog(log, ErrorMessage(L"遍历目标目录", targetRoot, ec));
                ec.clear();
                continue;
            }

            const fs::path relativePath = fs::relative(entry.path(), targetRoot, ec);
            if (ec)
            {
                stats.failedFiles++;
                WriteLog(log, ErrorMessage(L"计算相对路径", entry.path(), ec));
                ec.clear();
                continue;
            }

            if (sourceFiles.find(relativePath.wstring()) == sourceFiles.end())
            {
                extraPaths.push_back(entry.path());
            }
        }

        for (auto it = extraPaths.rbegin(); it != extraPaths.rend(); ++it)
        {
            fs::remove_all(*it, ec);
            if (ec)
            {
                stats.failedFiles++;
                WriteLog(log, ErrorMessage(L"删除目标多余项", *it, ec));
                ec.clear();
            }
            else
            {
                stats.deletedFiles++;
                WriteLog(log, L"[删除] " + it->wstring());
            }
        }
    }

    void DeleteEmptyParentDirectories(const fs::path& root, fs::path directory, SyncStats& stats, SyncLogCallback& log)
    {
        std::error_code ec;
        if (!fs::exists(root, ec) || !fs::is_directory(root, ec) || directory.empty())
        {
            return;
        }

        const fs::path canonicalRoot = fs::weakly_canonical(root, ec);
        if (ec)
        {
            ec.clear();
            return;
        }

        while (!directory.empty())
        {
            const fs::path canonicalDirectory = fs::weakly_canonical(directory, ec);
            if (ec || canonicalDirectory == canonicalRoot)
            {
                ec.clear();
                break;
            }

            if (!fs::is_empty(directory, ec))
            {
                ec.clear();
                break;
            }

            fs::remove(directory, ec);
            if (ec)
            {
                stats.failedFiles++;
                WriteLog(log, ErrorMessage(L"删除空目录", directory, ec));
                ec.clear();
                break;
            }

            stats.deletedFiles++;
            WriteLog(log, L"[删除空目录] " + directory.wstring());
            directory = directory.parent_path();
        }
    }

    bool IsChildPathOf(const std::wstring& child, const std::wstring& parent)
    {
        if (child.size() <= parent.size())
        {
            return false;
        }

        if (child.compare(0, parent.size(), parent) != 0)
        {
            return false;
        }

        const wchar_t separator = child[parent.size()];
        return separator == L'\\' || separator == L'/';
    }
}

namespace
{
    std::atomic<bool> g_isMonitoring{ false };
    std::thread g_monitorThread;
    HANDLE g_stopEvent = nullptr;

    void MonitorWorker(std::vector<SyncPair> pairs, SyncOptions options, SyncLogCallback log, ProgressCallback progress)
    {
        // For accurate file monitoring, we should use ReadDirectoryChangesW.
        // However, rewriting the entire monitoring loop with Overlapped I/O for ReadDirectoryChangesW
        // is quite complex and verbose for this example. We will keep FindFirstChangeNotification for now
        // but note it as a critical optimization point. 
        // 
        // As an optimization we will apply a better debouncing strategy.
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
                    try {
                        // 重新尝试恢复路径（以防磁盘刚插入）
                        if (!TryResolveSyncPairPaths(pairs[pairIndex], log))
                        {
                            WriteLog(log, L"[监控跳过] 任务路径不可用且无法恢复: " + pairs[pairIndex].source + L" -> " + pairs[pairIndex].target);
                            continue;
                        }

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
                    catch (const std::exception& e)
                    {
                        WriteLog(log, L"[监控异常] 同步任务时出错: " + std::wstring(e.what(), e.what() + strlen(e.what())));
                    }
                    catch (...)
                    {
                        WriteLog(log, L"[监控异常] 同步任务时发生未知错误");
                    }
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

bool CaptureSyncPairVolumeInfo(SyncPair& pair)
{
    bool ok = true;
    ok = CapturePathVolumeInfo(pair.source, pair.sourceVolumeGuid, pair.sourceRelativePath) && ok;
    ok = CapturePathVolumeInfo(pair.target, pair.targetVolumeGuid, pair.targetRelativePath) && ok;
    return ok;
}

bool TryResolveSyncPairPaths(SyncPair& pair, SyncLogCallback log)
{
    SyncLogCallback logCopy = log;
    if (pair.sourceVolumeGuid.empty() || pair.sourceRelativePath.empty() ||
        pair.targetVolumeGuid.empty() || pair.targetRelativePath.empty())
    {
        CaptureSyncPairVolumeInfo(pair);
    }

    const bool sourceOk = TryResolvePathByVolumeInfo(pair.source, pair.sourceVolumeGuid, pair.sourceRelativePath, L"源文件夹", logCopy);
    const bool targetOk = TryResolvePathByVolumeInfo(pair.target, pair.targetVolumeGuid, pair.targetRelativePath, L"目标文件夹", logCopy);

    if (sourceOk && targetOk)
    {
        CaptureSyncPairVolumeInfo(pair);
        return true;
    }

    return false;
}

bool ResolveSyncPairPaths(std::vector<SyncPair>& pairs, SyncLogCallback log)
{
    bool allOk = true;
    for (auto& pair : pairs)
    {
        if (!TryResolveSyncPairPaths(pair, log))
        {
            allOk = false;
        }
    }
    return allOk;
}

bool DeleteSnapshotForPair(const SyncPair& pair, SyncLogCallback log)
{
    if (!pair.isBidirectional)
    {
        return true;
    }

    const std::wstring snapshotPath = GetSnapshotFilePath(pair);
    const std::wstring legacySnapshotPath = GetSnapshotFilePath(pair.source, pair.target);
    if (snapshotPath.empty() && legacySnapshotPath.empty())
    {
        WriteLog(log, L"[失败] 删除快照文件: 无法获取快照文件路径");
        return false;
    }

    bool ok = true;
    std::error_code ec;
    auto removeSnapshot = [&](const std::wstring& path)
    {
        if (path.empty())
        {
            return;
        }

        ec.clear();
        if (!fs::exists(path, ec))
        {
            return;
        }

        fs::remove(path, ec);
        if (ec)
        {
            WriteLog(log, ErrorMessage(L"删除快照文件", path, ec));
            ok = false;
            return;
        }

        WriteLog(log, L"[删除快照] " + path);
    };

    removeSnapshot(snapshotPath);
    if (legacySnapshotPath != snapshotPath)
    {
        removeSnapshot(legacySnapshotPath);
    }

    return ok;
}

SyncStats SyncFolderPair(const SyncPair& pair, const SyncOptions& options, SyncLogCallback log, ProgressCallback progress)
{
    SyncStats stats;
    std::error_code ec;
    const fs::path rootA(pair.source);
    const fs::path rootB(pair.target);

    if (!fs::exists(rootA, ec))
    {
        WriteLog(log, L"[同步跳过] 源目录不存在: " + rootA.wstring());
        return stats;
    }
    ec.clear();
    if (!fs::exists(rootB, ec))
    {
        fs::create_directories(rootB, ec);
        if (ec)
        {
            WriteLog(log, L"[同步失败] 无法创建目标目录: " + rootB.wstring());
            return stats;
        }
    }

    // 统一同步逻辑：从 src 同步到 dst
    auto syncOneWay = [&](const fs::path& src, const fs::path& tgt)
    {
        std::error_code ec;
        if (!fs::exists(src, ec) || !fs::is_directory(src, ec)) return;
        fs::create_directories(tgt, ec);

        std::vector<fs::directory_entry> entries;
        for (const auto& entry : fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied, ec))
        {
            if (!ec) entries.push_back(entry);
            else ec.clear();
        }

        size_t totalItems = entries.size();
        std::atomic<size_t> processedItems{0};

        std::vector<std::future<void>> futures;
        const size_t batchSize = (std::max<size_t>)(1, entries.size() / std::thread::hardware_concurrency());

        for (size_t i = 0; i < entries.size(); i += batchSize) {
            auto batchEnd = (std::min)(entries.size(), i + batchSize);
            std::vector<fs::directory_entry> batch(entries.begin() + i, entries.begin() + batchEnd);

            futures.push_back(std::async(std::launch::async, [batch, src, tgt, &stats, &log, &progress, &processedItems, totalItems]() {
                for (const auto& entry : batch) {
                    std::error_code localEc;
                    const fs::path relativePath = fs::relative(entry.path(), src, localEc);
                    const fs::path targetPath = tgt / relativePath;
                    
                    if (entry.is_directory(localEc))
                    {
                        fs::create_directories(targetPath, localEc);
                    }
                    else if (entry.is_regular_file(localEc))
                    {
                        CopyFileIncremental(entry.path(), targetPath, stats, log);
                    }

                    processedItems++;
                    if (progress && totalItems > 0)
                    {
                        progress((float)processedItems / totalItems);
                    }
                }
            }));
        }

        for(auto& f : futures) {
            f.wait();
        }
    };

    WriteLog(log, L"开始同步: " + rootA.wstring() + L" <-> " + rootB.wstring());

    if (pair.isBidirectional)
    {
        std::wstring snapshotPath = GetSnapshotFilePath(pair);
        const std::wstring legacySnapshotPath = GetSnapshotFilePath(rootA.wstring(), rootB.wstring());
        std::error_code snapshotEc;
        if (snapshotPath != legacySnapshotPath &&
            !fs::exists(snapshotPath, snapshotEc) &&
            fs::exists(legacySnapshotPath, snapshotEc))
        {
            WriteLog(log, L"[快照迁移] 使用旧路径快照并迁移到稳定磁盘标识快照。");
            snapshotPath = legacySnapshotPath;
        }
        SnapshotMap lastSnapshot = LoadSnapshot(snapshotPath);
        const std::wstring saveSnapshotPath = GetSnapshotFilePath(pair);
        SnapshotMap currentSnapshot;
        SnapshotMap currentA;
        SnapshotMap currentB;
        std::mutex currentSnapshotMutex;

        auto scanDir = [](const fs::path& root, SnapshotMap& mapOut) {
            std::error_code ec;
            if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return;

            // 目录处理任务包
            struct DirTask {
                fs::path absPath;
                fs::path relPath;
            };

            std::mutex mapMutex;
            std::mutex queueMutex;
            std::condition_variable cv;
            std::queue<DirTask> dirsToProcess;
            int activeTasks = 0; // 当前正在执行的任务数

            // 推入根目录作为初始任务，根目录的相对路径为空
            dirsToProcess.push({root, fs::path()});

            unsigned int numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 4; // 兜底策略

            std::vector<std::thread> workers;
            for (unsigned int i = 0; i < numThreads; ++i) {
                workers.emplace_back([&]() {
                    while (true) {
                        DirTask currentTask;
                        {
                            std::unique_lock<std::mutex> lock(queueMutex);
                            // 等待队列有任务，或者所有任务都已处理完成
                            cv.wait(lock, [&]() { return !dirsToProcess.empty() || activeTasks == 0; });

                            if (dirsToProcess.empty() && activeTasks == 0) {
                                return; // 没有任务且所有线程均空闲，退出线程
                            }

                            currentTask = std::move(dirsToProcess.front());
                            dirsToProcess.pop();
                            activeTasks++; // 增加活跃任务计数
                        }

                        try {
                            std::vector<DirTask> subDirs;
                            SnapshotMap localMap; // 局部缓存，减少加锁频率

                            std::error_code iterEc;
                            auto it = fs::directory_iterator(currentTask.absPath, fs::directory_options::skip_permission_denied, iterEc);
                            auto end = fs::directory_iterator();

                            while (!iterEc && it != end) {
                                const auto& entry = *it;
                                std::error_code localEc;

                                // 优化：避免调用耗时的 fs::relative，通过逐层拼接获取相对路径
                                fs::path childRelPath = currentTask.relPath.empty() 
                                    ? entry.path().filename() 
                                    : currentTask.relPath / entry.path().filename();

                                if (entry.is_directory(localEc)) {
                                    if (!localEc) {
                                        auto ftime = fs::last_write_time(entry.path(), localEc);
                                        long long timeVal = 0;
                                        if (!localEc) {
                                            timeVal = std::chrono::duration_cast<std::chrono::seconds>(ftime.time_since_epoch()).count();
                                        }
                                        localMap[childRelPath.wstring()] = { 0, timeVal, true };
                                        subDirs.push_back({entry.path(), std::move(childRelPath)});
                                    }
                                }
                                else if (!localEc && entry.is_regular_file(localEc)) {
                                    if (!localEc) {
                                        auto ftime = fs::last_write_time(entry.path(), localEc);
                                        long long timeVal = std::chrono::duration_cast<std::chrono::seconds>(ftime.time_since_epoch()).count();
                                        localMap[childRelPath.wstring()] = { entry.file_size(localEc), timeVal, false };
                                    }
                                }
                                else {
                                    localEc.clear();
                                }

                                it.increment(iterEc);
                            }

                            // 批量写入全局结果，减少锁竞争
                            if (!localMap.empty()) {
                                std::lock_guard<std::mutex> lock(mapMutex);
                                for (auto& kv : localMap) {
                                    mapOut[kv.first] = kv.second;
                                }
                            }

                            // 将子目录推入任务队列并减少活跃任务计数
                            {
                                std::lock_guard<std::mutex> lock(queueMutex);
                                for (auto& dir : subDirs) {
                                    dirsToProcess.push(std::move(dir));
                                }
                                activeTasks--;
                            }
                            cv.notify_all(); // 唤醒可能在等待队列的线程
                        } catch (...) {
                            // 异常安全保护
                            std::lock_guard<std::mutex> lock(queueMutex);
                            activeTasks--;
                            cv.notify_all();
                        }
                    }
                });
            }

            // 等待所有工作线程完成
            for (auto& t : workers) {
                if (t.joinable()) {
                    t.join();
                }
            }
        };

        scanDir(rootA, currentA);
        scanDir(rootB, currentB);

        std::vector<std::future<void>> futures;
        std::atomic<size_t> processedItems{ 0 };
        size_t totalItems = currentA.size() + currentB.size();
        std::unordered_set<std::wstring> deletedDirectories;

        auto recordUnchangedDirectoryAncestors = [&](const std::wstring& relPathStr)
        {
            fs::path parent = fs::path(relPathStr).parent_path();
            while (!parent.empty())
            {
                const std::wstring parentStr = parent.wstring();
                auto it = currentA.find(parentStr);
                if (it != currentA.end() && it->second.isDirectory)
                {
                    std::lock_guard<std::mutex> lock(currentSnapshotMutex);
                    currentSnapshot[parentStr] = it->second;
                }
                else
                {
                    auto itB = currentB.find(parentStr);
                    if (itB != currentB.end() && itB->second.isDirectory)
                    {
                        std::lock_guard<std::mutex> lock(currentSnapshotMutex);
                        currentSnapshot[parentStr] = itB->second;
                    }
                }
                parent = parent.parent_path();
            }
        };

        auto isUnderDeletedDirectory = [&](const std::wstring& relPathStr)
        {
            for (const auto& deletedDirectory : deletedDirectories)
            {
                if (IsChildPathOf(relPathStr, deletedDirectory))
                {
                    return true;
                }
            }
            return false;
        };

        auto removeDirectoryTree = [&](const fs::path& path, const std::wstring& relPathStr)
        {
            std::error_code ec;
            if (!fs::exists(path, ec))
            {
                ec.clear();
                deletedDirectories.insert(relPathStr);
                return;
            }

            fs::remove_all(path, ec);
            if (ec)
            {
                stats.failedFiles++;
                WriteLog(log, ErrorMessage(L"删除目录", path, ec));
                ec.clear();
            }
            else
            {
                stats.deletedFiles++;
                deletedDirectories.insert(relPathStr);
                WriteLog(log, L"[删除目录] " + path.wstring());
            }
        };

        auto createDirectoryItem = [&](const fs::path& path, const std::wstring& relPathStr, const FileSnapshot& snapshot)
        {
            std::error_code ec;
            fs::create_directories(path, ec);
            if (ec)
            {
                stats.failedFiles++;
                WriteLog(log, ErrorMessage(L"创建目录", path, ec));
                ec.clear();
                return;
            }

            std::lock_guard<std::mutex> lock(currentSnapshotMutex);
            currentSnapshot[relPathStr] = snapshot;
            WriteLog(log, L"[创建目录] " + path.wstring());
        };

        auto processFile = [&](const std::wstring& relPathStr, bool inA, bool inB, bool inSnap, FileSnapshot snapA, FileSnapshot snapB, FileSnapshot snapS) {
            fs::path relPath(relPathStr);
            fs::path pathA = rootA / relPath;
            fs::path pathB = rootB / relPath;

            const bool isDirectory = (inA && snapA.isDirectory) || (inB && snapB.isDirectory) || (inSnap && snapS.isDirectory);

            if (isDirectory)
            {
                if (inA && inB)
                {
                    std::lock_guard<std::mutex> lock(currentSnapshotMutex);
                    currentSnapshot[relPathStr] = snapA;
                }
                else if (inA && !inB)
                {
                    if (inSnap)
                    {
                        removeDirectoryTree(pathA, relPathStr);
                    }
                    else
                    {
                        createDirectoryItem(pathB, relPathStr, snapA);
                    }
                }
                else if (!inA && inB)
                {
                    if (inSnap)
                    {
                        removeDirectoryTree(pathB, relPathStr);
                    }
                    else
                    {
                        createDirectoryItem(pathA, relPathStr, snapB);
                    }
                }

                return;
            }

            if (isUnderDeletedDirectory(relPathStr))
            {
                return;
            }

            bool aChanged = false;
            bool bChanged = false;

            if (inA) {
                aChanged = (!inSnap) || (snapA.size != snapS.size || snapA.lastWriteTime != snapS.lastWriteTime);
            }
            if (inB) {
                bChanged = (!inSnap) || (snapB.size != snapS.size || snapB.lastWriteTime != snapS.lastWriteTime);
            }

            std::error_code ec;
            if (inA && inB) {
                if (aChanged && !bChanged) {
                    // A updated, copy to B
                    CopyFileIncremental(pathA, pathB, stats, log);
                    std::lock_guard<std::mutex> lock(currentSnapshotMutex);
                    currentSnapshot[relPathStr] = snapA;
                } else if (!aChanged && bChanged) {
                    // B updated, copy to A
                    CopyFileIncremental(pathB, pathA, stats, log);
                    std::lock_guard<std::mutex> lock(currentSnapshotMutex);
                    currentSnapshot[relPathStr] = snapB;
                } else if (aChanged && bChanged) {
                    // Conflict, newer wins
                    if (snapA.lastWriteTime > snapB.lastWriteTime) {
                        CopyFileIncremental(pathA, pathB, stats, log);
                        std::lock_guard<std::mutex> lock(currentSnapshotMutex);
                        currentSnapshot[relPathStr] = snapA;
                    } else {
                        CopyFileIncremental(pathB, pathA, stats, log);
                        std::lock_guard<std::mutex> lock(currentSnapshotMutex);
                        currentSnapshot[relPathStr] = snapB;
                    }
                } else {
                    // No change
                    std::lock_guard<std::mutex> lock(currentSnapshotMutex);
                    currentSnapshot[relPathStr] = snapA;
                }
            } else if (inA && !inB) {
                if (inSnap && !aChanged) {
                    // Deleted in B, no change in A -> Delete in A
                    if (fs::remove(pathA, ec))
                    {
                        stats.deletedFiles++;
                        DeleteEmptyParentDirectories(rootA, pathA.parent_path(), stats, log);
                    }
                    else stats.failedFiles++;
                } else {
                    // New in A, or modified in A and deleted in B -> Copy to B
                    CopyFileIncremental(pathA, pathB, stats, log);
                    std::lock_guard<std::mutex> lock(currentSnapshotMutex);
                    currentSnapshot[relPathStr] = snapA;
                }
            } else if (!inA && inB) {
                if (inSnap && !bChanged) {
                    // Deleted in A, no change in B -> Delete in B
                    if (fs::remove(pathB, ec))
                    {
                        stats.deletedFiles++;
                        DeleteEmptyParentDirectories(rootB, pathB.parent_path(), stats, log);
                    }
                    else stats.failedFiles++;
                } else {
                    // New in B, or modified in B and deleted in A -> Copy to A
                    CopyFileIncremental(pathB, pathA, stats, log);
                    std::lock_guard<std::mutex> lock(currentSnapshotMutex);
                    currentSnapshot[relPathStr] = snapB;
                }
            }
        };

        auto processPath = [&](const std::wstring& relPathStr)
        {
            const bool inA = currentA.count(relPathStr) > 0;
            const bool inB = currentB.count(relPathStr) > 0;
            const bool inSnap = lastSnapshot.count(relPathStr) > 0;

            const FileSnapshot snapA = inA ? currentA[relPathStr] : FileSnapshot{ 0, 0, false };
            const FileSnapshot snapB = inB ? currentB[relPathStr] : FileSnapshot{ 0, 0, false };
            const FileSnapshot snapS = inSnap ? lastSnapshot[relPathStr] : FileSnapshot{ 0, 0, false };

            processFile(relPathStr, inA, inB, inSnap, snapA, snapB, snapS);
        };

        // Gather all relative paths
        std::unordered_set<std::wstring> allPaths;
        for (const auto& kv : currentA) allPaths.insert(kv.first);
        for (const auto& kv : currentB) allPaths.insert(kv.first);
        for (const auto& kv : lastSnapshot) allPaths.insert(kv.first);

        std::vector<std::wstring> pathList(allPaths.begin(), allPaths.end());
        totalItems = pathList.size();

        std::vector<std::wstring> directoryPaths;
        std::vector<std::wstring> filePaths;
        directoryPaths.reserve(pathList.size());
        filePaths.reserve(pathList.size());

        for (const auto& relPathStr : pathList)
        {
            const bool isDirectory =
                (currentA.count(relPathStr) > 0 && currentA[relPathStr].isDirectory) ||
                (currentB.count(relPathStr) > 0 && currentB[relPathStr].isDirectory) ||
                (lastSnapshot.count(relPathStr) > 0 && lastSnapshot[relPathStr].isDirectory);

            if (isDirectory)
            {
                directoryPaths.push_back(relPathStr);
            }
            else
            {
                filePaths.push_back(relPathStr);
            }
        }

        std::sort(directoryPaths.begin(), directoryPaths.end(), [](const std::wstring& a, const std::wstring& b)
            {
                return a.size() > b.size();
            });

        for (const auto& relPathStr : directoryPaths)
        {
            processPath(relPathStr);
            processedItems++;
            if (progress && totalItems > 0)
            {
                progress((float)processedItems / totalItems);
            }
        }

        const size_t batchSize = (std::max<size_t>)(1, filePaths.size() / std::thread::hardware_concurrency());

        for (size_t i = 0; i < filePaths.size(); i += batchSize) {
            auto batchEnd = (std::min)(filePaths.size(), i + batchSize);
            std::vector<std::wstring> batch(filePaths.begin() + i, filePaths.begin() + batchEnd);

            futures.push_back(std::async(std::launch::async, [batch, processPath, &progress, &processedItems, totalItems]() {
                for (const auto& relPathStr : batch) {
                    processPath(relPathStr);

                    processedItems++;
                    if (progress && totalItems > 0)
                    {
                        progress((float)processedItems / totalItems);
                    }
                }
            }));
        }

        for (auto& f : futures) {
            f.wait();
        }

        SaveSnapshot(saveSnapshotPath.empty() ? snapshotPath : saveSnapshotPath, currentSnapshot);
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
    std::vector<std::future<SyncStats>> futures;
    
    // 如果有多个任务，并且没有进度条要求，可以并发执行
    // 由于目前的进度条回调是总的，多线程直接回调会导致进度跳跃，这里为了简单起见，依然采用按对进行并发，
    // 但是进度可能不准确，如果是正式应用，可以采用更复杂的进度合并策略。
    // 在这里，我们通过 future 来等待所有的同步结束。
    for (const auto& pair : pairs)
    {
        futures.push_back(std::async(std::launch::async, [&pair, &options, log, progress]() {
            return SyncFolderPair(pair, options, log, progress);
        }));
    }

    for (auto& f : futures) {
        SyncStats current = f.get();
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
