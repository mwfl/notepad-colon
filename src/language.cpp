#include <notepad_colon/language.h>

#include <algorithm>
#include <cwctype>
#include <array>

namespace notepad_colon {
namespace {
constexpr std::array languages{
    Language::plain_text, Language::cpp, Language::csharp, Language::java,
    Language::javascript, Language::typescript, Language::python, Language::json,
    Language::xml, Language::html, Language::css, Language::markdown, Language::cmake,
    Language::powershell, Language::batch, Language::ini, Language::yaml, Language::sql,
    Language::rust};

constexpr std::array profiles{
    LanguageProfile{Language::plain_text, L"Plain Text", {}, {}, {}, false},
    LanguageProfile{Language::cpp, L"C/C++", "cpp",
        "alignas alignof asm auto bool break case catch char class const consteval constexpr constinit continue co_await co_return co_yield decltype default delete do double else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept nullptr operator override private protected public register reinterpret_cast requires return short signed sizeof static static_assert struct switch template this thread_local throw true try typedef typename union unsigned using virtual void volatile wchar_t while",
        "std string string_view vector array span optional unique_ptr shared_ptr size_t uint8_t uint16_t uint32_t uint64_t", true},
    LanguageProfile{Language::csharp, L"C#", "cpp",
        "abstract as base bool break byte case catch char checked class const continue decimal default delegate do double else enum event explicit extern false finally fixed float for foreach goto if implicit in int interface internal is lock long namespace new null object operator out override params private protected public readonly record ref return sbyte sealed short sizeof stackalloc static string struct switch this throw true try typeof uint ulong unchecked unsafe ushort using virtual void volatile while async await dynamic get init set value var when where with yield",
        "DateTime Guid IEnumerable IList Dictionary List Task CancellationToken", true},
    LanguageProfile{Language::java, L"Java", "cpp",
        "abstract assert boolean break byte case catch char class const continue default do double else enum extends final finally float for goto if implements import instanceof int interface long native new null package private protected public return short static strictfp super switch synchronized this throw throws transient true try void volatile while record sealed permits non-sealed var yield",
        "String Object Class Integer Long Double Boolean List Map Set Optional Stream", true},
    LanguageProfile{Language::javascript, L"JavaScript", "cpp",
        "as async await break case catch class const continue debugger default delete do else export extends false finally for from function get if import in instanceof let new null of return set static super switch this throw true try typeof undefined var void while with yield",
        "Array BigInt Boolean Date Error JSON Map Math Number Object Promise Proxy Reflect RegExp Set String Symbol WeakMap WeakSet console document globalThis window", true},
    LanguageProfile{Language::typescript, L"TypeScript", "cpp",
        "abstract any as asserts async await bigint boolean break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface is keyof let module namespace never new null number object of override private protected public readonly require return satisfies set static string super switch symbol this throw true try tuple type typeof undefined unique unknown var void while with yield",
        "Array Date Error Map Promise Record Set String Partial Pick Omit Readonly Required", true},
    LanguageProfile{Language::python, L"Python", "python",
        "and as assert async await break class continue def del elif else except False finally for from global if import in is lambda None nonlocal not or pass raise return True try while with yield match case",
        "abs all any bool bytes callable chr dict enumerate filter float format frozenset getattr hasattr hash help hex id input int isinstance issubclass iter len list map max memoryview min next object oct open ord pow print property range repr reversed round set slice sorted str sum super tuple type vars zip __import__", true},
    LanguageProfile{Language::json, L"JSON", "json", "false true null", {}, false},
    LanguageProfile{Language::xml, L"XML", "hypertext", {}, {}, true},
    LanguageProfile{Language::html, L"HTML", "hypertext",
        "html head title base link meta style body article section nav aside h1 h2 h3 h4 h5 h6 header footer address p hr pre blockquote ol ul li dl dt dd figure figcaption main div a em strong small s cite q dfn abbr ruby rt rp data time code var samp kbd sub sup i b u mark bdi bdo span br wbr ins del picture source img iframe embed object param video audio track map area table caption colgroup col tbody thead tfoot tr td th form label input button select datalist optgroup option textarea output progress meter fieldset legend details summary dialog script noscript template slot canvas",
        "id class style title lang dir hidden tabindex href src alt width height name value type rel media charset content role aria-label", true},
    LanguageProfile{Language::css, L"CSS", "css",
        "color background border margin padding display position top right bottom left width height min-width max-width min-height max-height font font-family font-size font-weight line-height text-align text-decoration overflow opacity transform transition animation grid flex align-items justify-content gap content cursor visibility z-index",
        "active after before checked disabled empty enabled first-child focus hover last-child link not nth-child root target visited", true},
    LanguageProfile{Language::markdown, L"Markdown", "markdown", {}, {}, false},
    LanguageProfile{Language::cmake, L"CMake", "cmake",
        "add_compile_definitions add_compile_options add_custom_command add_custom_target add_definitions add_dependencies add_executable add_library add_link_options add_subdirectory add_test aux_source_directory block break build_command cmake_file_api cmake_host_system_information cmake_language cmake_minimum_required cmake_parse_arguments cmake_path cmake_policy configure_file continue create_test_sourcelist define_property else elseif enable_language enable_testing endforeach endfunction endif endmacro endwhile execute_process file find_file find_library find_package find_path find_program foreach function get_cmake_property get_directory_property get_filename_component get_property if include include_directories include_external_msproject include_guard install link_directories link_libraries list load_cache macro mark_as_advanced math message option project return separate_arguments set set_directory_properties set_property site_name source_group string target_compile_definitions target_compile_features target_compile_options target_include_directories target_link_directories target_link_libraries target_link_options target_precompile_headers target_sources try_compile try_run unset variable_watch while",
        "WIN32 UNIX APPLE MSVC CMAKE_SOURCE_DIR CMAKE_BINARY_DIR CMAKE_CURRENT_SOURCE_DIR CMAKE_CURRENT_BINARY_DIR PROJECT_SOURCE_DIR PROJECT_BINARY_DIR PROJECT_NAME", true},
    LanguageProfile{Language::powershell, L"PowerShell", "powershell",
        "begin break catch class continue data define do dynamicparam else elseif end enum exit filter finally for foreach from function hidden if in inlinescript parallel param process return sequence switch throw trap try until using var while workflow",
        "Get-ChildItem Get-Content Set-Content Select-Object Where-Object ForEach-Object Test-Path Join-Path Resolve-Path New-Item Remove-Item Move-Item Copy-Item Write-Output Write-Error Write-Warning Start-Process Get-Process Stop-Process", true},
    LanguageProfile{Language::batch, L"Batch", "batch",
        "assoc attrib break call cd chdir cls color copy date del dir echo endlocal erase exit for ftype goto if md mkdir mklink move path pause popd prompt pushd rd rem ren rename rmdir set setlocal shift start time title type ver verify vol choice find findstr powershell cmd",
        {}, false},
    LanguageProfile{Language::ini, L"INI", "props", {}, {}, false},
    LanguageProfile{Language::yaml, L"YAML", "yaml", "true false yes no null on off", {}, true},
    LanguageProfile{Language::sql, L"SQL", "sql",
        "add all alter and any as asc authorization backup begin between break browse bulk by cascade case check checkpoint close clustered coalesce collate column commit compute constraint contains containstable continue convert create cross current current_date current_time current_timestamp cursor database dbcc deallocate declare default delete deny desc disk distinct distributed double drop dump else end errlvl escape except exec execute exists exit external fetch file fillfactor for foreign freetext freetexttable from full function goto grant group having holdlock identity identity_insert identitycol if in index inner insert intersect into is join key kill left like lineno load merge national nocheck nonclustered not null nullif of off offsets on open opendatasource openquery openrowset openxml option or order outer over percent pivot plan precision primary print proc procedure public raiserror read readtext reconfigure references replication restore restrict return revert revoke right rollback rowcount rowguidcol rule save schema securityaudit select semantickeyphrasetable semanticsimilaritydetailstable semanticsimilaritytable session_user set setuser shutdown some statistics system_user table tablesample textsize then to top tran transaction trigger truncate try_convert tsequal union unique unpivot update updatetext use user values varying view waitfor when where while with within group writetext",
        "count sum avg min max cast substring trim upper lower length now date timestamp integer varchar nvarchar bigint decimal numeric boolean", true},
    LanguageProfile{Language::rust, L"Rust", "rust",
        "as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type unsafe use where while",
        "bool char str i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 Option Result String Vec Box Rc Arc Cow HashMap HashSet", true}
};
}

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
    if (extension == L".js" || extension == L".jsx" || extension == L".mjs" ||
        extension == L".cjs") return Language::javascript;
    if (extension == L".ts" || extension == L".tsx" || extension == L".mts" ||
        extension == L".cts") return Language::typescript;
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
    return GetLanguageProfile(language).name;
}

const LanguageProfile& GetLanguageProfile(Language language) noexcept {
    const auto index = static_cast<std::size_t>(language);
    return index < profiles.size() ? profiles[index] : profiles.front();
}

std::span<const Language> AllLanguages() noexcept { return languages; }

}  // namespace notepad_colon

