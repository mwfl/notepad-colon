#pragma once

#include <filesystem>
#include <span>
#include <string_view>

namespace notepad_colon {

enum class Language {
    plain_text, cpp, csharp, java, javascript, typescript, python, json,
    xml, html, css, markdown, cmake, powershell, batch, ini, yaml, sql, rust
};

struct LanguageProfile {
    Language language = Language::plain_text;
    std::wstring_view name;
    std::string_view lexer;
    std::string_view primary_keywords;
    std::string_view secondary_keywords;
    bool supports_folding = false;
};

Language DetectLanguage(const std::filesystem::path& path) noexcept;
std::string_view LexerName(Language language) noexcept;
std::wstring_view LanguageName(Language language) noexcept;
const LanguageProfile& GetLanguageProfile(Language language) noexcept;
std::span<const Language> AllLanguages() noexcept;

}  // namespace notepad_colon

