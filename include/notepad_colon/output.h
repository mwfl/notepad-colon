#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace notepad_colon {

struct DocumentStatistics {
    std::size_t characters = 0;
    std::size_t characters_without_whitespace = 0;
    std::size_t words = 0;
    std::size_t lines = 1;
    std::size_t non_blank_lines = 0;
    std::size_t utf8_bytes = 0;
};

DocumentStatistics CalculateStatistics(std::wstring_view text) noexcept;
std::wstring EscapeHtml(std::wstring_view text);
std::wstring ExportHtmlDocument(std::wstring_view title, std::wstring_view text,
                                bool dark_source = false);

}  // namespace notepad_colon
