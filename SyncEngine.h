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
};

using SnapshotMap = std::unordered_map<std::wstring, FileSnapshot>;

struct SyncPair
{
    std::wstring source;
    std::wstring target;
    bool isBidirectional = false; // Add bidirectional flag
    std::wstring triggeredRoot; // 记录最近一次变动的根目录
};

struct SyncOptions
{
    bool deleteExtraFiles = false;
};

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

// 新增监控相关接口
void StartMonitoring(const std::vector<SyncPair>& pairs, const SyncOptions& options, SyncLogCallback log, ProgressCallback progress);
void StopMonitoring();
bool IsMonitoring();

bool IsPathAvailable(const std::wstring& path);
SyncStats SyncFolderPair(const SyncPair& pair, const SyncOptions& options, SyncLogCallback log, ProgressCallback progress);
SyncStats SyncFolderPairs(const std::vector<SyncPair>& pairs, const SyncOptions& options, SyncLogCallback log, ProgressCallback progress);

#define IDC_START_SYNC 1009
#define IDC_LOG_EDIT 1010
#define IDC_PROGRESS_BAR 1011
#define IDC_AUTO_MONITOR 1012
#define IDC_ADD_PAIR_DLG 1013 // New ID for Add Pair Dialog button
