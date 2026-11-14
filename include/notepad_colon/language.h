#pragma once

#include <filesystem>
#include <span>
#include <string_view>

namespace notepad_colon {

// Order is part of the session and command-id contract: append only.
// (`delphi` rather than `pascal`: the latter is a <windows.h> macro.)
enum class Language {
    plain_text, cpp, csharp, java, javascript, typescript, python, json,
    xml, html, css, markdown, cmake, powershell, batch, ini, yaml, sql, rust,
    go, kotlin, swift, scala, groovy, objective_c, protobuf, glsl, php, dart,
    ruby, perl, lua, shell, makefile, dockerfile, toml, diff, delphi, fortran,
    assembly, visual_basic, r, matlab, julia, haskell, erlang, lisp, clojure,
    ocaml, fsharp, tcl, latex, registry, nim, zig, gdscript, verilog, vhdl,
    autoit, nsis, inno_setup, ada, cobol, coffeescript, d, properties
};

struct LanguageProfile {
    Language language = Language::plain_text;
    std::wstring_view name;
    std::string_view lexer;
    std::string_view primary_keywords;
    std::string_view secondary_keywords;
    bool supports_folding = false;
    // Stable identifier used by sessions and custom language definitions.
    std::string_view id;
    // Semicolon-separated lower-case extensions (with dot) and file names.
    std::wstring_view extensions;
    std::wstring_view filenames;
    // Empty when the language has no line-comment form.
    std::string_view line_comment;
    // "role:style,style;..." colour map for lexers without a dedicated
    // palette. Roles: c comment, s string, n number, k keyword, t type,
    // p preprocessor, a attribute/variable, h heading, m muted, l link,
    // g added, x error/removed.
    std::string_view styles;
};

Language DetectLanguage(const std::filesystem::path& path) noexcept;
std::string_view LexerName(Language language) noexcept;
std::wstring_view LanguageName(Language language) noexcept;
const LanguageProfile& GetLanguageProfile(Language language) noexcept;
std::span<const Language> AllLanguages() noexcept;

}  // namespace notepad_colon
