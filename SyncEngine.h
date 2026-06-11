#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <unordered_map>

struct FileSnapshot {
    uintmax_t size;
    long long lastWriteTime;
    bool isDirectory = false;
};

using SnapshotMap = std::unordered_map<std::wstring, FileSnapshot>;

struct SyncPair
{
    std::wstring taskName; // 新增任务名称
    std::wstring source;
    std::wstring target;
    bool isBidirectional = false; // Add bidirectional flag
    bool deleteExtraFiles = false; // Phase 2: Moved from SyncOptions
    bool autoMonitor = false;      // Phase 2: Added auto monitor flag per task
    std::wstring sourceVolumeGuid;
    std::wstring sourceRelativePath;
    std::wstring targetVolumeGuid;
    std::wstring targetRelativePath;
    std::wstring triggeredRoot; // 记录最近一次变动的根目录
};

// Phase 2: Removed SyncOptions struct since its only member moved to SyncPair

struct SyncStats
{
    std::atomic<unsigned long long> copiedFiles{0};
    std::atomic<unsigned long long> skippedFiles{0};
    std::atomic<unsigned long long> deletedFiles{0};
    std::atomic<unsigned long long> failedFiles{0};

    SyncStats() = default;
    SyncStats(const SyncStats& o) : 
        copiedFiles(o.copiedFiles.load()), 
        skippedFiles(o.skippedFiles.load()), 
        deletedFiles(o.deletedFiles.load()), 
        failedFiles(o.failedFiles.load()) {}
    SyncStats& operator=(const SyncStats& o) {
        copiedFiles = o.copiedFiles.load();
        skippedFiles = o.skippedFiles.load();
        deletedFiles = o.deletedFiles.load();
        failedFiles = o.failedFiles.load();
        return *this;
    }
};

using SyncLogCallback = std::function<void(const std::wstring&)>;
using ProgressCallback = std::function<void(float)>;
using SyncTaskCompleteCallback = std::function<void(int, const SyncStats&)>;

// 新增监控相关接口
void StartMonitoring(const std::vector<SyncPair>& pairs, SyncLogCallback log, ProgressCallback progress, SyncTaskCompleteCallback taskComplete = nullptr);
void StopMonitoring();
bool IsMonitoring();

bool IsPathAvailable(const std::wstring& path);
bool CaptureSyncPairVolumeInfo(SyncPair& pair);
bool TryResolveSyncPairPaths(SyncPair& pair, SyncLogCallback log = nullptr);
bool ResolveSyncPairPaths(std::vector<SyncPair>& pairs, SyncLogCallback log = nullptr);
bool DeleteSnapshotForPair(const SyncPair& pair, SyncLogCallback log = nullptr);
void CleanupOldTrashForPairs(const std::vector<SyncPair>& pairs, int retentionDays, SyncLogCallback log = nullptr);
SyncStats SyncFolderPair(const SyncPair& pair, SyncLogCallback log, ProgressCallback progress);
SyncStats SyncFolderPairs(const std::vector<SyncPair>& pairs, SyncLogCallback log, ProgressCallback progress);

#define IDC_START_SYNC 1009
#define IDC_LOG_EDIT 1010
#define IDC_PROGRESS_BAR 1011
#define IDC_AUTO_MONITOR 1012
#define IDC_ADD_PAIR_DLG 1013 // New ID for Add Pair Dialog button
