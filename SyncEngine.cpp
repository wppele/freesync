#include "framework.h"
#include "SyncEngine.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
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
                if (swscanf_s(line, L"%1023[^|]|%ju|%lld", relPath, (unsigned)_countof(relPath), &size, &lastWriteTime) == 3)
                {
                    snapshot[relPath] = { size, lastWriteTime };
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
                fwprintf(file, L"%s|%ju|%lld\n", relPath.c_str(), info.size, info.lastWriteTime);
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
        std::wstring snapshotPath = GetSnapshotFilePath(rootA.wstring(), rootB.wstring());
        SnapshotMap lastSnapshot = LoadSnapshot(snapshotPath);
        SnapshotMap currentSnapshot;
        SnapshotMap currentA;
        SnapshotMap currentB;
        std::mutex currentSnapshotMutex;

        auto scanDir = [](const fs::path& root, SnapshotMap& mapOut) {
            std::error_code ec;
            if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return;
            for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
            {
                if (!ec && entry.is_regular_file(ec))
                {
                    fs::path relPath = fs::relative(entry.path(), root, ec);
                    if (!ec) {
                        auto ftime = fs::last_write_time(entry.path(), ec);
                        long long timeVal = std::chrono::duration_cast<std::chrono::seconds>(ftime.time_since_epoch()).count();
                        mapOut[relPath.wstring()] = { entry.file_size(ec), timeVal };
                    }
                }
                else ec.clear();
            }
        };

        scanDir(rootA, currentA);
        scanDir(rootB, currentB);

        std::vector<std::future<void>> futures;
        std::atomic<size_t> processedItems{ 0 };
        size_t totalItems = currentA.size() + currentB.size();

        auto processFile = [&](const std::wstring& relPathStr, bool inA, bool inB, bool inSnap, FileSnapshot snapA, FileSnapshot snapB, FileSnapshot snapS) {
            fs::path relPath(relPathStr);
            fs::path pathA = rootA / relPath;
            fs::path pathB = rootB / relPath;

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
                    if (fs::remove(pathA, ec)) stats.deletedFiles++;
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
                    if (fs::remove(pathB, ec)) stats.deletedFiles++;
                    else stats.failedFiles++;
                } else {
                    // New in B, or modified in B and deleted in A -> Copy to A
                    CopyFileIncremental(pathB, pathA, stats, log);
                    std::lock_guard<std::mutex> lock(currentSnapshotMutex);
                    currentSnapshot[relPathStr] = snapB;
                }
            }
        };

        // Gather all relative paths
        std::unordered_set<std::wstring> allPaths;
        for (const auto& kv : currentA) allPaths.insert(kv.first);
        for (const auto& kv : currentB) allPaths.insert(kv.first);
        for (const auto& kv : lastSnapshot) allPaths.insert(kv.first);

        std::vector<std::wstring> pathList(allPaths.begin(), allPaths.end());
        totalItems = pathList.size();
        const size_t batchSize = (std::max<size_t>)(1, pathList.size() / std::thread::hardware_concurrency());

        for (size_t i = 0; i < pathList.size(); i += batchSize) {
            auto batchEnd = (std::min)(pathList.size(), i + batchSize);
            std::vector<std::wstring> batch(pathList.begin() + i, pathList.begin() + batchEnd);

            futures.push_back(std::async(std::launch::async, [batch, &currentA, &currentB, &lastSnapshot, processFile, &progress, &processedItems, totalItems]() {
                for (const auto& relPathStr : batch) {
                    bool inA = currentA.count(relPathStr) > 0;
                    bool inB = currentB.count(relPathStr) > 0;
                    bool inSnap = lastSnapshot.count(relPathStr) > 0;

                    FileSnapshot snapA = inA ? currentA[relPathStr] : FileSnapshot{0, 0};
                    FileSnapshot snapB = inB ? currentB[relPathStr] : FileSnapshot{0, 0};
                    FileSnapshot snapS = inSnap ? lastSnapshot[relPathStr] : FileSnapshot{0, 0};

                    processFile(relPathStr, inA, inB, inSnap, snapA, snapB, snapS);

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

        SaveSnapshot(snapshotPath, currentSnapshot);
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
