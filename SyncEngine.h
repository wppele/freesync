#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

struct SyncPair
{
    std::wstring source;
    std::wstring target;
};

struct SyncOptions
{
    bool deleteExtraFiles = false;
};

struct SyncStats
{
    unsigned long long copiedFiles = 0;
    unsigned long long skippedFiles = 0;
    unsigned long long deletedFiles = 0;
    unsigned long long failedFiles = 0;
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
