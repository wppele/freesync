#include "framework.h"
#include "SyncEngine.h"

#include <chrono>
#include <fstream>
#include <sstream>

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

bool IsPathAvailable(const std::wstring& path)
{
    std::error_code ec;
    return fs::exists(fs::path(path), ec);
}

SyncStats SyncFolderPair(const SyncPair& pair, const SyncOptions& options, SyncLogCallback log, ProgressCallback progress)
{
    SyncStats stats;
    const fs::path sourceRoot(pair.source);
    const fs::path targetRoot(pair.target);

    // 预遍历计算总数
    size_t totalFiles = 0;
    {
        std::error_code ec;
        for (const auto& _ : fs::recursive_directory_iterator(sourceRoot, fs::directory_options::skip_permission_denied, ec))
            totalFiles++;
    }
    size_t processedFiles = 0;

    WriteLog(log, L"开始同步: " + sourceRoot.wstring() + L" -> " + targetRoot.wstring());

    std::error_code ec;
    if (!fs::exists(sourceRoot, ec) || !fs::is_directory(sourceRoot, ec))
    {
        ++stats.failedFiles;
        WriteLog(log, L"[失败] 源目录不存在或不可访问: " + sourceRoot.wstring());
        return stats;
    }

    fs::create_directories(targetRoot, ec);
    if (ec)
    {
        ++stats.failedFiles;
        WriteLog(log, ErrorMessage(L"创建目标目录", targetRoot, ec));
        return stats;
    }

    for (const auto& entry : fs::recursive_directory_iterator(sourceRoot, fs::directory_options::skip_permission_denied, ec))
    {
        if (ec)
        {
            ++stats.failedFiles;
            WriteLog(log, ErrorMessage(L"遍历源目录", sourceRoot, ec));
            ec.clear();
            continue;
        }

        processedFiles++;
        const fs::path relativePath = fs::relative(entry.path(), sourceRoot, ec);
        if (ec)
        {
            ++stats.failedFiles;
            WriteLog(log, ErrorMessage(L"计算相对路径", entry.path(), ec));
            ec.clear();
            continue;
        }

        const fs::path targetPath = targetRoot / relativePath;
        if (entry.is_directory(ec))
        {
            fs::create_directories(targetPath, ec);
            if (ec)
            {
                ++stats.failedFiles;
                WriteLog(log, ErrorMessage(L"创建目录", targetPath, ec));
                ec.clear();
            }
        }
        else if (entry.is_regular_file(ec))
        {
            CopyFileIncremental(entry.path(), targetPath, stats, log);
        }
        
        if (totalFiles > 0 && progress)
            progress((float)processedFiles / totalFiles);
    }

    if (options.deleteExtraFiles)
    {
        DeleteExtraTargetFiles(sourceRoot, targetRoot, stats, log);
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
