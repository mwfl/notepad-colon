#pragma once

#include <notepad_colon/document.h>

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
};

struct Session {
    std::vector<SessionEntry> documents;
    std::size_t active_index = 0;
};

std::string SerializeSession(const Session& session);
bool DeserializeSession(std::string_view input, Session& session) noexcept;

}  // namespace notepad_colon

