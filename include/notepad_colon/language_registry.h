#pragma once

#include <notepad_colon/language.h>

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace notepad_colon {

struct TreeSitterDefinition {
    std::string grammar;
    std::string highlights_query;
    std::string symbols_query;
    std::string wasm_language_name;
    std::vector<std::uint8_t> wasm_bytes;
};

struct RegisteredLanguage {
    std::string id;
    std::wstring name;
    std::vector<std::wstring> extensions;
    std::vector<std::wstring> filenames;
    std::string fallback_lexer;
    std::optional<Language> builtin;
    std::optional<TreeSitterDefinition> tree_sitter;
};

struct LanguageLoadError {
    std::filesystem::path path;
    std::string message;
};

class LanguageRegistry final {
public:
    LanguageRegistry();

    void ResetBuiltins();
    std::size_t LoadDirectory(const std::filesystem::path& directory);
    const RegisteredLanguage* Find(std::string_view id) const noexcept;
    const RegisteredLanguage* Detect(const std::filesystem::path& path) const noexcept;
    const std::vector<RegisteredLanguage>& Languages() const noexcept { return languages_; }
    const std::vector<LanguageLoadError>& Errors() const noexcept { return errors_; }

private:
    std::vector<RegisteredLanguage> languages_;
    std::vector<LanguageLoadError> errors_;
};

}  // namespace notepad_colon
