#include <notepad_colon/language.h>

#include <algorithm>
#include <cwctype>

namespace notepad_colon {

Language DetectLanguage(const std::filesystem::path& path) noexcept {
    auto name = path.filename().wstring();
    std::transform(name.begin(), name.end(), name.begin(), std::towlower);
    if (name == L"cmakelists.txt" || name.ends_with(L".cmake")) return Language::cmake;
    if (name == L"makefile") return Language::plain_text;
    const auto extension = std::filesystem::path{name}.extension().wstring();
    if (extension == L".c" || extension == L".cc" || extension == L".cpp" ||
        extension == L".cxx" || extension == L".h" || extension == L".hh" ||
        extension == L".hpp" || extension == L".hxx") return Language::cpp;
    if (extension == L".cs") return Language::csharp;
    if (extension == L".java") return Language::java;
    if (extension == L".js" || extension == L".jsx" || extension == L".mjs") return Language::javascript;
    if (extension == L".ts" || extension == L".tsx") return Language::typescript;
    if (extension == L".py" || extension == L".pyw") return Language::python;
    if (extension == L".json" || extension == L".jsonc") return Language::json;
    if (extension == L".xml" || extension == L".xaml" || extension == L".svg") return Language::xml;
    if (extension == L".html" || extension == L".htm") return Language::html;
    if (extension == L".css" || extension == L".scss" || extension == L".less") return Language::css;
    if (extension == L".md" || extension == L".markdown") return Language::markdown;
    if (extension == L".ps1" || extension == L".psm1" || extension == L".psd1") return Language::powershell;
    if (extension == L".bat" || extension == L".cmd") return Language::batch;
    if (extension == L".ini" || extension == L".cfg" || extension == L".conf") return Language::ini;
    if (extension == L".yaml" || extension == L".yml") return Language::yaml;
    if (extension == L".sql") return Language::sql;
    if (extension == L".rs") return Language::rust;
    return Language::plain_text;
}

std::string_view LexerName(Language language) noexcept {
    switch (language) {
    case Language::cpp: case Language::csharp: case Language::java:
    case Language::javascript: case Language::typescript: return "cpp";
    case Language::python: return "python";
    case Language::json: return "json";
    case Language::xml: case Language::html: return "hypertext";
    case Language::css: return "css";
    case Language::markdown: return "markdown";
    case Language::cmake: return "cmake";
    case Language::powershell: return "powershell";
    case Language::batch: return "batch";
    case Language::ini: return "props";
    case Language::yaml: return "yaml";
    case Language::sql: return "sql";
    case Language::rust: return "rust";
    case Language::plain_text: return {};
    }
    return {};
}

std::wstring_view LanguageName(Language language) noexcept {
    switch (language) {
    case Language::plain_text: return L"Plain Text";
    case Language::cpp: return L"C/C++";
    case Language::csharp: return L"C#";
    case Language::java: return L"Java";
    case Language::javascript: return L"JavaScript";
    case Language::typescript: return L"TypeScript";
    case Language::python: return L"Python";
    case Language::json: return L"JSON";
    case Language::xml: return L"XML";
    case Language::html: return L"HTML";
    case Language::css: return L"CSS";
    case Language::markdown: return L"Markdown";
    case Language::cmake: return L"CMake";
    case Language::powershell: return L"PowerShell";
    case Language::batch: return L"Batch";
    case Language::ini: return L"INI";
    case Language::yaml: return L"YAML";
    case Language::sql: return L"SQL";
    case Language::rust: return L"Rust";
    }
    return L"Plain Text";
}

}  // namespace notepad_colon

