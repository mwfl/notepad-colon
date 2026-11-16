#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace notepad_colon {

// Persisted values: retain English=0 and Simplified Chinese=1 for existing settings.
enum class UiLanguage : std::uint32_t {
    english = 0, chinese_simplified = 1, chinese_traditional = 2, spanish = 3, french = 4, hindi = 5
};
struct UiLanguageInfo {
    UiLanguage language;
    std::wstring_view native_name;
    std::string_view locale;
};
std::span<const UiLanguageInfo> SupportedUiLanguages() noexcept;
bool IsValidUiLanguage(UiLanguage language) noexcept;

struct Translation {
    std::wstring_view english;
    std::wstring_view translated;
};

// The process-wide interface language. English is the source language: every
// user-visible string is written in English at its call site and looked up
// in the active catalog, so an untranslated string degrades to English
// instead of disappearing.
void SetUiLanguage(UiLanguage language) noexcept;
UiLanguage GetUiLanguage() noexcept;

// Returns the translation of `english` in the active language, or `english`
// itself when the language is English or the catalog has no entry.
std::wstring Tr(std::wstring_view english);

// True when the given language has an entry for `english`.
bool HasTranslation(UiLanguage language, std::wstring_view english);

// The complete catalog of a language (empty for English).
std::span<const Translation> TranslationCatalog(UiLanguage language) noexcept;

}  // namespace notepad_colon
