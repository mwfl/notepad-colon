#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace notepad_colon {

using DocumentId = std::uint64_t;

enum class Encoding { utf8, utf8_bom, utf16_le, utf16_be, ansi };
enum class LineEnding { crlf, lf, cr };

struct ViewState {
    std::intptr_t anchor = 0;
    std::intptr_t caret = 0;
    std::intptr_t first_visible_line = 0;
};

struct Document {
    DocumentId id = 0;
    std::filesystem::path path;
    std::wstring untitled_name = L"Untitled";
    Encoding encoding = Encoding::utf8;
    LineEnding line_ending = LineEnding::crlf;
    ViewState view;
    bool dirty = false;
    bool read_only = false;

    bool HasPath() const noexcept { return !path.empty(); }
    std::wstring DisplayName() const;
};

class Workspace final {
public:
    DocumentId AddUntitled();
    std::optional<DocumentId> AddPath(const std::filesystem::path& path);
    bool Remove(DocumentId id) noexcept;
    bool Select(DocumentId id) noexcept;
    Document* Find(DocumentId id) noexcept;
    const Document* Find(DocumentId id) const noexcept;
    Document* Active() noexcept;
    const Document* Active() const noexcept;
    const std::vector<Document>& Documents() const noexcept { return documents_; }
    std::optional<DocumentId> ActiveId() const noexcept { return active_; }

private:
    static bool SamePath(const std::filesystem::path& left,
                         const std::filesystem::path& right) noexcept;
    std::vector<Document> documents_;
    std::optional<DocumentId> active_;
    DocumentId next_id_ = 1;
    std::uint64_t untitled_counter_ = 1;
};

}  // namespace notepad_colon

