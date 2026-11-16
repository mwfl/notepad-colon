#pragma once

#include <notepad_colon/document.h>
#include <notepad_colon/large_file_buffer.h>
#include <notepad_colon/workspace.h>

#include <filesystem>
#include <string>
#include <vector>

namespace notepad_colon {

struct SessionEntry {
    std::filesystem::path path;
    std::wstring recovery_text;
    Encoding encoding = Encoding::utf8;
    LineEnding line_ending = LineEnding::crlf;
    ViewState view;
    bool dirty = false;
    // Zero is retained for old sessions, whose code page was not recorded.
    unsigned int ansi_code_page = 0;
    std::uint64_t window_offset = 0;
    std::filesystem::path recovery_file;
    std::uint64_t recovery_size = 0;
    std::uint64_t recovery_checksum = 0;
    // Transient worker input; durable sessions refer to an independent copy.
    std::shared_ptr<const LargeFileBuffer> large_snapshot;
    std::optional<FileState> disk_state;
    bool legacy_encoding_override = false;
    bool unicode_bom = true;
    std::optional<LineEnding> line_ending_override;
};

struct Session {
    std::vector<SessionEntry> documents;
    std::size_t active_index = 0;
    std::vector<std::filesystem::path> workspace_paths;
    // Compatibility alias for callers and v1/v2 session files.
    std::filesystem::path workspace_path;
};

std::string SerializeSession(const Session& session);
bool DeserializeSession(std::string_view input, Session& session) noexcept;
bool SaveSessionAtomic(const std::filesystem::path& path, const Session& session) noexcept;
bool LoadSession(const std::filesystem::path& path, Session& session,
                 bool* used_previous = nullptr) noexcept;

}  // namespace notepad_colon
