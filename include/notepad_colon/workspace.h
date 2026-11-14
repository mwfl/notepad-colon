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
    std::vector<std::wstring> include_globs;
    std::vector<std::wstring> exclude_globs;
    bool match_case = false;
    bool whole_word = false;
    bool regular_expression = false;
    bool multiline = false;
    bool use_gitignore = true;
    std::size_t maximum_results = 5000;
    std::uintmax_t maximum_file_size = 8 * 1024 * 1024;
};

struct SearchResult {
    std::vector<SearchMatch> matches;
    std::size_t files_searched = 0;
    std::size_t files_skipped = 0;
    bool truncated = false;
    bool cancelled = false;
    std::wstring error;
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
                            std::stop_token stop = {},
                            bool use_gitignore = true);
SearchResult SearchWorkspace(const std::filesystem::path& root,
                             std::wstring_view query,
                             const SearchOptions& options = {},
                             std::stop_token stop = {});
SearchResult SearchText(const std::filesystem::path& path, std::wstring_view text,
                        std::wstring_view query, const SearchOptions& options = {},
                        std::stop_token stop = {});
FileState CaptureFileState(const std::filesystem::path& path) noexcept;
// True when a workspace-relative path passes the include/exclude glob lists
// (an empty include list includes everything).
bool MatchesWorkspaceGlobs(const std::filesystem::path& relative,
                           const std::vector<std::wstring>& include_globs,
                           const std::vector<std::wstring>& exclude_globs);

struct TextReplacement {
    std::wstring text;
    std::size_t count = 0;
    std::wstring error;
};

struct ReplaceFileOutcome {
    std::filesystem::path path;
    std::size_t replacements = 0;
    bool written = false;
    std::wstring error;  // Empty unless the file had matches and could not be rewritten.
};

struct ReplaceOptions {
    SearchOptions search;
    bool create_backups = true;
    // Preview mode counts matches per file without writing anything.
    bool dry_run = false;
    // Files the caller rewrites itself (documents open in the editor).
    std::vector<std::filesystem::path> excluded_paths;
    std::size_t maximum_files = 100000;
};

struct ReplaceResult {
    std::vector<ReplaceFileOutcome> files;  // Only files that contained matches.
    std::size_t replacements = 0;
    std::size_t files_changed = 0;
    std::size_t files_failed = 0;
    std::size_t files_searched = 0;
    std::size_t files_skipped = 0;
    bool cancelled = false;
    bool truncated = false;
    std::wstring error;
};

// Replaces every non-overlapping match using the same matching rules as
// SearchText. Regular-expression replacements accept $1-style references.
TextReplacement ReplaceAllInText(std::wstring_view text, std::wstring_view query,
                                 std::wstring_view replacement,
                                 const SearchOptions& options = {});

// Rewrites matching files under `root`. Each file keeps its encoding, byte
// order mark and line endings, is replaced atomically, and is left untouched
// if it changed on disk during the operation or the result cannot be encoded.
ReplaceResult ReplaceInWorkspace(const std::filesystem::path& root, std::wstring_view query,
                                 std::wstring_view replacement,
                                 const ReplaceOptions& options = {},
                                 std::stop_token stop = {});

}  // namespace notepad_colon

