#pragma once

#include <filesystem>
#include <string_view>

namespace notepad_colon {

enum class Language {
    plain_text, cpp, csharp, java, javascript, typescript, python, json,
    xml, html, css, markdown, cmake, powershell, batch, ini, yaml, sql, rust
};

Language DetectLanguage(const std::filesystem::path& path) noexcept;
std::string_view LexerName(Language language) noexcept;
std::wstring_view LanguageName(Language language) noexcept;

}  // namespace notepad_colon

