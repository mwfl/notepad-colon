#pragma once

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace notepad_colon {

struct WorkspaceEntry {
    std::filesystem::path relative_path;
    std::wstring search_key;
    bool directory = false;
    std::uintmax_t size = 0;
};

struct WorkspaceScan {
    std::vector<WorkspaceEntry> entries;
    bool truncated = false;
    std::size_t skipped = 0;
};

struct SearchMatch {
    std::filesystem::path path;
    std::size_t line = 0;
    std::size_t column = 0;
    std::wstring preview;
};

struct SearchOptions {
    bool match_case = false;
    bool whole_word = false;
    std::size_t maximum_results = 5000;
    std::uintmax_t maximum_file_size = 8 * 1024 * 1024;
};

struct SearchResult {
    std::vector<SearchMatch> matches;
    std::size_t files_searched = 0;
    std::size_t files_skipped = 0;
    bool truncated = false;
    bool cancelled = false;
};

struct FileState {
    std::uintmax_t size = 0;
    std::filesystem::file_time_type last_write{};
    bool exists = false;
    friend bool operator==(const FileState&, const FileState&) = default;
};

bool IsExcludedWorkspaceDirectory(std::wstring_view name) noexcept;
WorkspaceScan ScanWorkspace(const std::filesystem::path& root,
                            std::size_t maximum_entries = 20000,
                            std::stop_token stop = {});
SearchResult SearchWorkspace(const std::filesystem::path& root,
                             std::wstring_view query,
                             const SearchOptions& options = {},
                             std::stop_token stop = {});
FileState CaptureFileState(const std::filesystem::path& path) noexcept;

}  // namespace notepad_colon

