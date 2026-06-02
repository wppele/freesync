#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

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

bool IsPathAvailable(const std::wstring& path);
SyncStats SyncFolderPair(const SyncPair& pair, const SyncOptions& options, SyncLogCallback log);
SyncStats SyncFolderPairs(const std::vector<SyncPair>& pairs, const SyncOptions& options, SyncLogCallback log);
