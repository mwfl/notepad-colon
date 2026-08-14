#include <notepad_colon/output.h>

#include <windows.h>

#include <cwctype>

namespace notepad_colon {

DocumentStatistics CalculateStatistics(std::wstring_view text) noexcept {
    DocumentStatistics result;
    result.characters = text.size();
    bool in_word = false, line_has_content = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto c = text[index];
        const bool whitespace = std::iswspace(c) != 0;
        if (!whitespace) { ++result.characters_without_whitespace; line_has_content = true; }
        if (!whitespace && !in_word) { ++result.words; in_word = true; }
        if (whitespace) in_word = false;
        if (c == L'\r' || c == L'\n') {
            if (line_has_content) ++result.non_blank_lines;
            line_has_content = false; ++result.lines;
            if (c == L'\r' && index + 1 < text.size() && text[index + 1] == L'\n') ++index;
        }
    }
    if (line_has_content) ++result.non_blank_lines;
    if (!text.empty()) {
        const auto bytes = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (bytes > 0) result.utf8_bytes = static_cast<std::size_t>(bytes);
    }
    return result;
}

std::wstring EscapeHtml(std::wstring_view text) {
    std::wstring result;
    for (const auto c : text) {
        switch (c) {
        case L'&': result += L"&amp;"; break;
        case L'<': result += L"&lt;"; break;
        case L'>': result += L"&gt;"; break;
        case L'\"': result += L"&quot;"; break;
        case L'\'': result += L"&#39;"; break;
        default: result += c;
        }
    }
    return result;
}

std::wstring ExportHtmlDocument(std::wstring_view title, std::wstring_view text,
                                bool dark_source) {
    const auto background = dark_source ? L"#1e1e1e" : L"#ffffff";
    const auto foreground = dark_source ? L"#e6e6e6" : L"#1e1e1e";
    return L"<!doctype html>\n<html><head><meta charset=\"utf-8\"><title>" + EscapeHtml(title) +
        L"</title><style>body{margin:0;background:" + background + L";color:" + foreground +
        L"}pre{box-sizing:border-box;margin:0;padding:24px;font:14px/1.5 'Cascadia Mono',Consolas,monospace;"
        L"white-space:pre-wrap;tab-size:4}</style></head><body><pre>" + EscapeHtml(text) +
        L"</pre></body></html>\n";
}
}  // namespace notepad_colon
