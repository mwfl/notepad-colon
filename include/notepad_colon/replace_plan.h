#pragma once

#include <notepad_colon/workspace.h>
#include <optional>

namespace notepad_colon {

enum class ReplaceProgress { ready, writing, applied, restoring, restored, conflict, failed };

struct ReplaceBufferSnapshot {
    std::filesystem::path path;
    std::uint64_t document = 0;
    std::uint64_t revision = 0;
    std::wstring text;
};

struct ReviewedReplacement {
    std::filesystem::path path;
    std::uint64_t document = 0; // Nonzero: an unsaved, undoable editor change.
    std::uint64_t revision = 0;
    EncodingKind encoding = EncodingKind::utf8;
    unsigned int code_page = 0;
    bool bom = false;
    FileState original_state;
    std::optional<FileState> written_state;
    std::uint64_t before_hash = 0, after_hash = 0;
    std::size_t before_size = 0, after_size = 0;
    std::size_t replacements = 0;
    std::vector<ReplacementSpan> spans;
    bool selected = true;
    ReplaceProgress progress = ReplaceProgress::ready;
    std::wstring error;
};

struct ReplacePlan {
    std::filesystem::path directory;
    std::wstring query, replacement;
    std::vector<ReviewedReplacement> files;
    bool backups = true;
    bool started = false;
    bool cancelled = false;
    bool truncated = false;
    bool preflight_failed = false;
    std::size_t files_searched = 0, files_skipped = 0;
    std::wstring error;
};

// Plans are bounded to 256 files and 128 MiB of UTF-8 snapshots. Nothing is
// changed until the caller has reviewed and selected entries in the plan.
ReplacePlan PreviewWorkspaceReplacement(const std::vector<std::filesystem::path>& roots,
    std::wstring_view query, std::wstring_view replacement, const ReplaceOptions& options,
    const std::vector<ReplaceBufferSnapshot>& buffers, const std::filesystem::path& history,
    std::stop_token stop = {});
std::optional<std::wstring> ReadReplacementSnapshot(const ReplacePlan& plan,
    std::size_t index, bool after);
bool SaveReplacePlan(const ReplacePlan& plan) noexcept;
std::optional<ReplacePlan> LoadReplacePlan(const std::filesystem::path& directory) noexcept;
std::vector<ReplacePlan> LoadReplacementHistory(const std::filesystem::path& history);
// Validates every selected disk input and every staged output before the first
// write. During application each individual write checks the identity again.
void ApplyReplacePlan(ReplacePlan& plan, std::stop_token stop = {});
// Restores only unchanged outputs of this operation. Buffer snapshots are left
// to the UI, which recovers them into new tabs rather than overwriting edits.
void RestoreReplacePlan(ReplacePlan& plan, std::stop_token stop = {});
bool DeleteReplacePlan(const std::filesystem::path& history,
                       const std::filesystem::path& directory) noexcept;

} // namespace notepad_colon
