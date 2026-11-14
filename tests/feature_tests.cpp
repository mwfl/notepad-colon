// Model-layer coverage for language coverage, legacy-encoding detection and
// the other features added after 1.2.0. Each section is independent.
#include <notepad_colon/comparison.h>
#include <notepad_colon/encoding_analysis.h>
#include <notepad_colon/encoding_detection.h>
#include <notepad_colon/language.h>
#include <notepad_colon/language_registry.h>
#include <notepad_colon/line_tools.h>
#include <notepad_colon/localization.h>
#include <notepad_colon/shell_menu.h>
#include <notepad_colon/workspace.h>

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {
int failures = 0;

void Check(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

std::vector<std::uint8_t> Encode(std::wstring_view text, unsigned int code_page) {
    const int required = ::WideCharToMultiByte(code_page, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>((std::max)(required, 0)));
    if (required > 0)
        ::WideCharToMultiByte(code_page, 0, text.data(), static_cast<int>(text.size()),
            reinterpret_cast<char*>(bytes.data()), required, nullptr, nullptr);
    return bytes;
}

std::string Narrow(std::wstring_view text) {
    std::string result;
    for (const auto value : text) result.push_back(value < 0x80 ? static_cast<char>(value) : '?');
    return result;
}

void TestLanguages() {
    using notepad_colon::Language;
    const auto languages = notepad_colon::AllLanguages();
    Check(languages.size() >= 50, "at least 50 built-in languages");
    std::set<std::string_view> ids;
    std::set<std::wstring> extensions;
    std::set<std::wstring> names;
    for (std::size_t index = 0; index < languages.size(); ++index) {
        const auto& profile = notepad_colon::GetLanguageProfile(languages[index]);
        const auto label = Narrow(profile.name);
        Check(static_cast<std::size_t>(profile.language) == index,
              label + ": profile order matches the enumeration");
        Check(!profile.id.empty() && ids.insert(profile.id).second, label + ": unique identifier");
        Check(names.insert(std::wstring{profile.name}).second, label + ": unique display name");
        Check(std::ranges::all_of(profile.id, [](char value) {
            return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-';
        }), label + ": identifier is a lower-case slug");
        std::size_t start = 0;
        const auto list = profile.extensions;
        while (start < list.size()) {
            const auto end = (std::min)(list.find(L';', start), list.size());
            const std::wstring extension{list.substr(start, end - start)};
            Check(extension.size() >= 2 && extension.front() == L'.',
                  label + ": extensions start with a dot");
            Check(std::ranges::none_of(extension, [](wchar_t value) {
                return value >= L'A' && value <= L'Z'; }), label + ": extensions are lower-case");
            Check(extensions.insert(extension).second,
                  label + ": extension " + Narrow(extension) + " is claimed only once");
            Check(notepad_colon::DetectLanguage(L"sample" + extension) == profile.language,
                  label + ": extension " + Narrow(extension) + " round-trips through detection");
            start = end + 1;
        }
        // Every palette entry must be "role:number[,number]".
        for (std::size_t cursor = 0; cursor < profile.styles.size();) {
            const auto end = (std::min)(profile.styles.find(';', cursor), profile.styles.size());
            const auto item = profile.styles.substr(cursor, end - cursor);
            Check(item.size() >= 3 && item[1] == ':' &&
                  std::string_view{"csnktpahmlgx"}.find(item[0]) != std::string_view::npos &&
                  std::ranges::all_of(item.substr(2), [](char value) {
                      return (value >= '0' && value <= '9') || value == ','; }),
                  label + ": palette entry is well formed");
            cursor = end + 1;
        }
    }
    struct Case { const wchar_t* file; Language expected; };
    const Case cases[]{
        {L"main.go", Language::go}, {L"App.kt", Language::kotlin}, {L"View.swift", Language::swift},
        {L"index.PHP", Language::php}, {L"app.rb", Language::ruby}, {L"Gemfile", Language::ruby},
        {L"init.lua", Language::lua}, {L"deploy.sh", Language::shell}, {L".bashrc", Language::shell},
        {L"Makefile", Language::makefile}, {L"GNUmakefile", Language::makefile},
        {L"Dockerfile", Language::dockerfile}, {L"Dockerfile.dev", Language::dockerfile},
        {L"Cargo.toml", Language::toml}, {L"fix.patch", Language::diff},
        {L"lib.dart", Language::dart}, {L"main.zig", Language::zig}, {L"top.sv", Language::verilog},
        {L"setup.iss", Language::inno_setup}, {L"app.properties", Language::properties},
        {L".editorconfig", Language::properties}, {L"schema.proto", Language::protobuf},
        {L"CMakeLists.txt", Language::cmake}, {L"notes.txt", Language::plain_text},
        {L"README", Language::plain_text}, {L"core.clj", Language::clojure},
        {L"Program.fs", Language::fsharp}, {L"build.gradle", Language::groovy}};
    for (const auto& item : cases)
        Check(notepad_colon::DetectLanguage(item.file) == item.expected,
              "detects " + Narrow(item.file));
    Check(notepad_colon::GetLanguageProfile(Language::python).line_comment == "#" &&
          notepad_colon::GetLanguageProfile(Language::lua).line_comment == "--" &&
          notepad_colon::GetLanguageProfile(Language::html).line_comment.empty(),
          "line-comment prefixes come from the language table");

    notepad_colon::LanguageRegistry registry;
    Check(registry.Languages().size() == languages.size(), "registry mirrors the built-in table");
    const auto expect = [&](const wchar_t* file, std::string_view id) {
        const auto* found = registry.Detect(file);
        Check(found && found->id == id, "registry detects " + Narrow(file));
    };
    expect(L"CMakeLists.txt", "cmake");  // file name must outrank the .txt extension
    expect(L"Dockerfile.prod", "dockerfile");
    expect(L"main.go", "go");
    expect(L"unknown.xyz", "plain-text");
    expect(L"notes.txt", "plain-text");
}

void TestEncodingDetection() {
    using notepad_colon::DetectLegacyCodePage;
    struct Sample { const char* label; unsigned int code_page; std::wstring_view text; };
    const Sample samples[]{
        {"Simplified Chinese GB18030", 54936,
         L"这是一个用来检测编码的简体中文文件。我们需要打开文件夹，保存并且查找替换其中的内容。"},
        {"Traditional Chinese Big5", 950,
         L"這是一個用來檢測編碼的繁體中文檔案。我們需要打開資料夾，儲存並且查找替換其中的內容。"},
        {"Japanese Shift-JIS", 932,
         L"これは文字コードを判定するための日本語のテキストファイルです。フォルダーを開いて保存します。"},
        {"Japanese EUC-JP", 51932,
         L"これは文字コードを判定するための日本語のテキストファイルです。フォルダーを開いて保存します。"},
        {"Korean EUC-KR", 949,
         L"이것은 문자 인코딩을 감지하기 위한 한국어 텍스트 파일입니다. 폴더를 열고 저장합니다."}};
    for (const auto& sample : samples) {
        if (!::IsValidCodePage(sample.code_page)) continue;
        const auto bytes = Encode(sample.text, sample.code_page);
        Check(!bytes.empty(), std::string{sample.label} + ": fixture encodes");
        // Detection must not depend on the machine's own ANSI code page.
        for (const unsigned int system : {1252u, 936u, 932u, 949u, 950u}) {
            const auto guess = DetectLegacyCodePage(bytes, system);
            Check(guess.detected && guess.code_page == sample.code_page,
                  std::string{sample.label} + " detected with system code page " +
                  std::to_string(system) + " (got " + std::to_string(guess.code_page) + ")");
        }
        const auto analysis = notepad_colon::AnalyzeEncoding(bytes);
        Check(analysis.encoding == notepad_colon::EncodingKind::ansi &&
              analysis.code_page == sample.code_page, std::string{sample.label} + ": analysis adopts the guess");
        const auto decoded = notepad_colon::DecodeBytes(bytes, analysis.encoding, analysis.code_page);
        Check(decoded && *decoded == sample.text, std::string{sample.label} + ": decodes losslessly");
        const auto encoded = decoded
            ? notepad_colon::EncodeText(*decoded, analysis.encoding, analysis.code_page) : std::nullopt;
        Check(encoded && *encoded == bytes, std::string{sample.label} + ": round-trips to identical bytes");
        // Mixed with ASCII source code, as in a commented source file.
        std::vector<std::uint8_t> mixed;
        const std::string_view prefix = "int main() { return 0; } // ";
        mixed.insert(mixed.end(), prefix.begin(), prefix.end());
        mixed.insert(mixed.end(), bytes.begin(), bytes.end());
        Check(DetectLegacyCodePage(mixed, 1252).code_page == sample.code_page,
              std::string{sample.label} + ": detected inside ASCII source");
    }
    // Western text: no CJK candidate may claim it.
    const auto western = Encode(L"Café déjà vu — naïve façade, Füße über Straße.", 1252);
    for (const unsigned int system : {1252u, 936u, 932u}) {
        const auto guess = DetectLegacyCodePage(western, system);
        Check(!guess.detected, "Western text is not mistaken for CJK content");
        Check(guess.code_page == 1252 || guess.code_page == system,
              "Western text falls back to a single-byte or system code page");
    }
    Check(DetectLegacyCodePage(western, 1252).code_page == 1252,
          "Western text keeps the Western system code page");
    // Explicit code pages are never overridden by detection.
    const auto big5 = Encode(L"這是一個繁體中文檔案，我們需要打開資料夾。", 950);
    Check(notepad_colon::AnalyzeEncoding(big5, 1252).code_page == 1252,
          "an explicit code page bypasses detection");
    // UTF-8 stays UTF-8 and never reaches the detector.
    const auto utf8 = Encode(L"这是 UTF-8 文本", CP_UTF8);
    Check(notepad_colon::AnalyzeEncoding(utf8).encoding == notepad_colon::EncodingKind::utf8,
          "valid UTF-8 is still reported as UTF-8");
    // A sample cut inside a double-byte character must still be detected.
    auto truncated = Encode(samples[0].text, 54936);
    truncated.pop_back();
    Check(DetectLegacyCodePage(truncated, 1252).code_page == 54936,
          "a sample ending inside a character is tolerated");
    Check(DetectLegacyCodePage({}, 1252).code_page == 1252 &&
          !DetectLegacyCodePage({}, 1252).detected, "an empty sample falls back to the system code page");
    Check(notepad_colon::SupportedLegacyCodePages().size() >= 6 &&
          notepad_colon::CodePageName(950).find(L"Big5") != std::wstring::npos &&
          !notepad_colon::CodePageName(437).empty(), "code pages have display names");
}
std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    const std::string raw{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    return {raw.begin(), raw.end()};
}

void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> EncodeAs(std::wstring_view text, notepad_colon::EncodingKind kind,
                                   unsigned int code_page = 0) {
    return notepad_colon::EncodeText(text, kind, code_page).value_or(std::vector<std::uint8_t>{});
}

void TestReplaceInText() {
    using notepad_colon::ReplaceAllInText;
    using notepad_colon::SearchOptions;
    auto result = ReplaceAllInText(L"Foo foo FOO food", L"foo", L"bar");
    Check(result.count == 4 && result.text == L"bar bar bar bard", "literal replace ignores case by default");
    result = ReplaceAllInText(L"Foo foo FOO food", L"foo", L"bar", {.match_case = true});
    Check(result.count == 2 && result.text == L"Foo bar FOO bard", "literal replace honours match case");
    result = ReplaceAllInText(L"foo food foo_bar (foo)", L"foo", L"X", {.whole_word = true});
    Check(result.count == 2 && result.text == L"X food foo_bar (X)", "literal replace honours whole word");
    result = ReplaceAllInText(L"aaaa", L"aa", L"b");
    Check(result.count == 2 && result.text == L"bb", "matches do not overlap");
    result = ReplaceAllInText(L"abc", L"b", L"");
    Check(result.count == 1 && result.text == L"ac", "an empty replacement deletes");
    result = ReplaceAllInText(L"aXa", L"x", L"xx");
    Check(result.count == 1 && result.text == L"axxa", "the replacement is never rescanned");
    result = ReplaceAllInText(L"abc", L"", L"z");
    Check(result.count == 0 && result.text == L"abc", "an empty query changes nothing");
    result = ReplaceAllInText(L"line1\r\nline2\r\n", L"line", L"row");
    Check(result.text == L"row1\r\nrow2\r\n", "line endings are untouched");
    result = ReplaceAllInText(L"key=value; other=thing", LR"((\w+)=(\w+))", L"$2=$1",
                              {.regular_expression = true});
    Check(result.count == 2 && result.text == L"value=key; thing=other",
          "regular expressions support group references");
    result = ReplaceAllInText(L"Hello HELLO", L"hello", L"x", {.match_case = true, .regular_expression = true});
    Check(result.count == 0 && result.text == L"Hello HELLO", "regular expressions honour match case");
    result = ReplaceAllInText(L"abc", L"x*", L"-", {.regular_expression = true});
    Check(result.count == 0 && result.text == L"abc", "empty regular-expression matches are skipped");
    result = ReplaceAllInText(L"abc", L"(", L"-", {.regular_expression = true});
    Check(result.count == 0 && result.text == L"abc" && !result.error.empty(),
          "an invalid regular expression reports an error and changes nothing");
    result = ReplaceAllInText(L"中文 文本 中文", L"中文", L"汉字");
    Check(result.count == 2 && result.text == L"汉字 文本 汉字", "non-ASCII text is replaced");
}

void TestReplaceInWorkspace() {
    using notepad_colon::EncodingKind;
    const auto root = std::filesystem::temp_directory_path() /
        (L"notepad-colon-replace-test-" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / L"src" / L"deep");
    std::filesystem::create_directories(root / L"ignored");
    std::filesystem::create_directories(root / L"node_modules");

    const auto utf8 = root / L"src" / L"utf8.txt";
    const auto bom = root / L"src" / L"bom.txt";
    const auto utf16 = root / L"src" / L"utf16.txt";
    const auto big5 = root / L"src" / L"deep" / L"big5.txt";
    const auto sjis = root / L"src" / L"sjis.txt";
    const auto binary = root / L"src" / L"data.bin";
    const auto untouched = root / L"src" / L"other.txt";
    const auto skipped_by_glob = root / L"src" / L"skip.min.js";
    const auto gitignored = root / L"ignored" / L"file.txt";
    const auto generated = root / L"node_modules" / L"file.txt";
    const auto open_document = root / L"src" / L"open.txt";

    const std::wstring unix_text = L"alpha needle\nNEEDLE beta\n";
    const std::wstring windows_text = L"needle one\r\nneedle two\r\n";
    const std::wstring traditional = L"繁體 needle 檔案\r\n";
    const std::wstring japanese = L"日本語 needle テキスト\r\n";
    WriteBytes(utf8, EncodeAs(unix_text, EncodingKind::utf8));
    WriteBytes(bom, EncodeAs(windows_text, EncodingKind::utf8_bom));
    WriteBytes(utf16, EncodeAs(windows_text, EncodingKind::utf16_le));
    WriteBytes(big5, EncodeAs(traditional, EncodingKind::ansi, 950));
    WriteBytes(sjis, EncodeAs(japanese, EncodingKind::ansi, 932));
    WriteBytes(binary, {'n', 'e', 'e', 'd', 'l', 'e', 0, 1, 2});
    WriteBytes(untouched, EncodeAs(L"nothing here\n", EncodingKind::utf8));
    WriteBytes(skipped_by_glob, EncodeAs(L"needle\n", EncodingKind::utf8));
    WriteBytes(gitignored, EncodeAs(L"needle\n", EncodingKind::utf8));
    WriteBytes(generated, EncodeAs(L"needle\n", EncodingKind::utf8));
    WriteBytes(open_document, EncodeAs(L"needle\n", EncodingKind::utf8));
    WriteBytes(root / L".gitignore", EncodeAs(L"ignored/\n", EncodingKind::utf8));

    const std::vector<std::filesystem::path> all{utf8, bom, utf16, big5, sjis, binary, untouched,
        skipped_by_glob, gitignored, generated, open_document};
    std::vector<std::vector<std::uint8_t>> original;
    for (const auto& path : all) original.push_back(ReadBytes(path));

    notepad_colon::ReplaceOptions options;
    options.search.exclude_globs = {L"*.min.js"};
    options.excluded_paths = {open_document};
    options.dry_run = true;
    auto preview = notepad_colon::ReplaceInWorkspace(root, L"needle", L"thread", options);
    Check(preview.files.size() == 5 && preview.replacements == 8 && preview.files_changed == 0,
          "preview counts matches in every eligible file (" + std::to_string(preview.files.size()) +
          " files, " + std::to_string(preview.replacements) + " matches)");
    bool unchanged = true;
    for (std::size_t index = 0; index < all.size(); ++index)
        unchanged = unchanged && ReadBytes(all[index]) == original[index];
    Check(unchanged, "preview writes nothing");
    Check(!std::filesystem::exists(utf8.wstring() + L".bak"), "preview creates no backups");

    options.dry_run = false;
    const auto applied = notepad_colon::ReplaceInWorkspace(root, L"needle", L"thread", options);
    Check(applied.files_changed == 5 && applied.files_failed == 0 && applied.replacements == 8,
          "replace rewrites every eligible file");
    Check(ReadBytes(utf8) == EncodeAs(L"alpha thread\nthread beta\n", EncodingKind::utf8),
          "UTF-8 without BOM keeps its encoding and LF endings");
    Check(ReadBytes(bom) == EncodeAs(L"thread one\r\nthread two\r\n", EncodingKind::utf8_bom),
          "UTF-8 with BOM keeps its BOM and CRLF endings");
    Check(ReadBytes(utf16) == EncodeAs(L"thread one\r\nthread two\r\n", EncodingKind::utf16_le),
          "UTF-16 LE stays UTF-16 LE");
    Check(ReadBytes(big5) == EncodeAs(L"繁體 thread 檔案\r\n", EncodingKind::ansi, 950),
          "Big5 stays Big5");
    Check(ReadBytes(sjis) == EncodeAs(L"日本語 thread テキスト\r\n", EncodingKind::ansi, 932),
          "Shift-JIS stays Shift-JIS");
    Check(ReadBytes(binary) == original[5], "binary files are never rewritten");
    Check(ReadBytes(untouched) == original[6] && !std::filesystem::exists(untouched.wstring() + L".bak"),
          "files without matches are not rewritten or backed up");
    Check(ReadBytes(skipped_by_glob) == original[7], "exclude globs are honoured");
    Check(ReadBytes(gitignored) == original[8], ".gitignore is honoured");
    Check(ReadBytes(generated) == original[9], "generated directories are skipped");
    Check(ReadBytes(open_document) == original[10], "caller-owned (open) documents are left to the editor");
    Check(ReadBytes(utf8.wstring() + L".bak") == original[0] &&
          ReadBytes(big5.wstring() + L".bak") == original[3], "backups hold the original bytes");
    for (const auto& outcome : applied.files)
        Check(outcome.written && outcome.error.empty() && outcome.replacements > 0,
              "every reported file was written");

    // A replacement the file's encoding cannot represent must leave it alone.
    const auto before_failure = ReadBytes(sjis);
    notepad_colon::ReplaceOptions no_backup;
    no_backup.create_backups = false;
    no_backup.search.include_globs = {L"sjis.txt"};
    const auto failed = notepad_colon::ReplaceInWorkspace(root, L"thread", L"한국어", no_backup);
    Check(failed.files_failed == 1 && failed.files_changed == 0 && failed.replacements == 0 &&
          failed.files.size() == 1 && !failed.files.front().written && !failed.files.front().error.empty(),
          "an unrepresentable replacement is reported as a failure");
    Check(ReadBytes(sjis) == before_failure, "a failed file keeps its original bytes");

    // Regular expressions, case sensitivity and include globs.
    notepad_colon::ReplaceOptions regex;
    regex.create_backups = false;
    regex.search.regular_expression = true;
    regex.search.match_case = true;
    regex.search.include_globs = {L"utf8.txt"};
    const auto swapped = notepad_colon::ReplaceInWorkspace(root, LR"((\w+) thread)", L"thread $1", regex);
    Check(swapped.files_changed == 1 && swapped.replacements == 1 &&
          ReadBytes(utf8) == EncodeAs(L"thread alpha\nthread beta\n", EncodingKind::utf8),
          "regular-expression replace with include glob");
    const auto invalid = notepad_colon::ReplaceInWorkspace(root, L"(", L"x", regex);
    Check(!invalid.error.empty() && invalid.files.empty(), "invalid regular expressions are rejected up front");

    // Cancellation before the first file leaves everything as it was.
    std::stop_source source;
    source.request_stop();
    const auto before_cancel = ReadBytes(utf8);
    const auto cancelled = notepad_colon::ReplaceInWorkspace(root, L"thread", L"x", no_backup, source.get_token());
    Check(cancelled.files_changed == 0 && ReadBytes(utf8) == before_cancel, "a cancelled replace writes nothing");

    Check(notepad_colon::ReplaceInWorkspace(root, L"", L"x").files.empty(), "an empty query is a no-op");
    Check(notepad_colon::ReplaceInWorkspace(root / L"missing", L"a", L"b").files.empty(),
          "a missing root is a no-op");

    // Search uses the same decoding, so legacy and UTF-16 files are searchable.
    notepad_colon::SearchOptions no_backups;
    no_backups.exclude_globs = {L"*.bak"};
    const auto found = notepad_colon::SearchWorkspace(root, L"檔案", no_backups);
    Check(found.matches.size() == 1 && found.matches.front().path == big5,
          "Find in Files decodes Big5 content regardless of the system code page");
    notepad_colon::SearchOptions utf16_only;
    utf16_only.include_globs = {L"utf16.txt"};
    Check(notepad_colon::SearchWorkspace(root, L"thread", utf16_only).matches.size() == 2 &&
          notepad_colon::SearchWorkspace(root, L"needle", utf16_only).matches.empty(),
          "Find in Files searches UTF-16 files");
    std::filesystem::remove_all(root, ignored);
}
void TestLocalization() {
    using notepad_colon::Tr;
    using notepad_colon::UiLanguage;
    Check(notepad_colon::GetUiLanguage() == UiLanguage::english, "English is the default language");
    Check(Tr(L"&File") == L"&File", "English returns the source text");
    Check(notepad_colon::TranslationCatalog(UiLanguage::english).empty(), "English has no catalog");
    const auto catalog = notepad_colon::TranslationCatalog(UiLanguage::chinese_simplified);
    Check(catalog.size() >= 500, "the Chinese catalog covers the whole interface");
    std::set<std::wstring_view> seen;
    for (const auto& entry : catalog) {
        const auto label = Narrow(entry.english);
        Check(!entry.english.empty() && !entry.translated.empty(), label + ": non-empty entry");
        Check(seen.insert(entry.english).second, label + ": listed once");
        // A menu mnemonic must survive translation or the item loses its key.
        const auto has_mnemonic = [](std::wstring_view text) {
            return text.find(L'&') != std::wstring_view::npos;
        };
        Check(has_mnemonic(entry.english) == has_mnemonic(entry.translated),
              label + ": mnemonic preserved");
        const auto ellipsis = [](std::wstring_view text) { return text.ends_with(L"..."); };
        Check(ellipsis(entry.english) == ellipsis(entry.translated), label + ": ellipsis preserved");
        Check(std::ranges::count(entry.english, L'\n') == std::ranges::count(entry.translated, L'\n'),
              label + ": line breaks preserved");
        Check(notepad_colon::HasTranslation(UiLanguage::chinese_simplified, entry.english),
              label + ": reachable through lookup");
    }
    notepad_colon::SetUiLanguage(UiLanguage::chinese_simplified);
    Check(Tr(L"&File") == L"文件(&F)" && Tr(L"Replace in Files...") == L"在文件中替换...",
          "Chinese lookups return the translation");
    Check(Tr(L"a string that is not in the catalog") == L"a string that is not in the catalog",
          "unknown strings degrade to English");
    Check(Tr(L"").empty(), "the empty string stays empty");
    notepad_colon::SetUiLanguage(UiLanguage::english);
    Check(Tr(L"&File") == L"&File", "switching back restores English");
    Check(!notepad_colon::HasTranslation(UiLanguage::english, L"&File"), "English has no lookups");
}
std::wstring ReadRegistryString(HKEY root, const std::wstring& key, const wchar_t* name) {
    wchar_t buffer[1024]{};
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    return ::RegGetValueW(root, key.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS
        ? std::wstring{buffer} : std::wstring{L"<missing>"};
}

void TestShellMenus() {
    using notepad_colon::ShellMenuStatus;
    // An isolated key stands in for HKCU\Software\Classes: the real one is never touched.
    const std::wstring classes = L"Software\\mwfl\\Tests\\NotepadColonShell-" +
                                 std::to_wstring(::GetCurrentProcessId());
    ::RegDeleteTreeW(HKEY_CURRENT_USER, classes.c_str());
    const notepad_colon::ShellMenuSpec spec{L"C:\\Program Files\\Notepad Colon\\notepad-colon.exe"};
    Check(!notepad_colon::ShellMenusRegistered(HKEY_CURRENT_USER, classes), "menus start unregistered");
    Check(notepad_colon::RemoveShellMenus(HKEY_CURRENT_USER, classes) == ShellMenuStatus::success,
          "removing absent menus is a harmless no-op");
    Check(notepad_colon::RegisterShellMenus(HKEY_CURRENT_USER, classes, spec) == ShellMenuStatus::success &&
          notepad_colon::ShellMenusRegistered(HKEY_CURRENT_USER, classes), "menus register");
    Check(ReadRegistryString(HKEY_CURRENT_USER, classes + L"\\*\\shell\\NotepadColon", nullptr) ==
              L"Edit with Notepad::", "file verb has its label");
    Check(ReadRegistryString(HKEY_CURRENT_USER, classes + L"\\*\\shell\\NotepadColon\\command", nullptr) ==
              L"\"C:\\Program Files\\Notepad Colon\\notepad-colon.exe\" \"%1\"",
          "file command quotes the executable and the path");
    Check(ReadRegistryString(HKEY_CURRENT_USER,
              classes + L"\\Directory\\Background\\shell\\NotepadColon\\command", nullptr) ==
              L"\"C:\\Program Files\\Notepad Colon\\notepad-colon.exe\" \"%V\"",
          "folder-background command passes the folder being viewed");
    Check(ReadRegistryString(HKEY_CURRENT_USER, classes + L"\\Directory\\shell\\NotepadColon", L"Icon") ==
              L"\"C:\\Program Files\\Notepad Colon\\notepad-colon.exe\",0", "verbs carry the application icon");
    // Registering again (for example after moving the portable folder) updates in place.
    const notepad_colon::ShellMenuSpec moved{L"D:\\Tools\\notepad-colon.exe", L"编辑", L"打开文件夹"};
    Check(notepad_colon::RegisterShellMenus(HKEY_CURRENT_USER, classes, moved) == ShellMenuStatus::success &&
          ReadRegistryString(HKEY_CURRENT_USER, classes + L"\\*\\shell\\NotepadColon\\command", nullptr) ==
              L"\"D:\\Tools\\notepad-colon.exe\" \"%1\"" &&
          ReadRegistryString(HKEY_CURRENT_USER, classes + L"\\*\\shell\\NotepadColon", nullptr) == L"编辑",
          "re-registering updates the path and label");
    // Unrelated verbs of other programs survive removal.
    HKEY other{};
    ::RegCreateKeyExW(HKEY_CURRENT_USER, (classes + L"\\*\\shell\\OtherEditor").c_str(), 0, nullptr, 0,
                      KEY_WRITE, nullptr, &other, nullptr);
    ::RegCloseKey(other);
    Check(notepad_colon::RemoveShellMenus(HKEY_CURRENT_USER, classes) == ShellMenuStatus::success &&
          !notepad_colon::ShellMenusRegistered(HKEY_CURRENT_USER, classes), "menus are removed");
    HKEY probe{};
    const bool other_survives = ::RegOpenKeyExW(HKEY_CURRENT_USER,
        (classes + L"\\*\\shell\\OtherEditor").c_str(), 0, KEY_READ, &probe) == ERROR_SUCCESS;
    if (other_survives) ::RegCloseKey(probe);
    const bool ours_gone = ::RegOpenKeyExW(HKEY_CURRENT_USER,
        (classes + L"\\*\\shell\\NotepadColon").c_str(), 0, KEY_READ, &probe) != ERROR_SUCCESS;
    Check(other_survives && ours_gone, "removal deletes only this application's verbs");
    // A same-named verb without our owner marker belongs to someone else.
    HKEY foreign{};
    ::RegCreateKeyExW(HKEY_CURRENT_USER, (classes + L"\\Directory\\shell\\NotepadColon").c_str(), 0,
                      nullptr, 0, KEY_WRITE, nullptr, &foreign, nullptr);
    ::RegCloseKey(foreign);
    Check(notepad_colon::RegisterShellMenus(HKEY_CURRENT_USER, classes, spec) == ShellMenuStatus::conflict,
          "a foreign verb with the same name blocks registration");
    Check(::RegOpenKeyExW(HKEY_CURRENT_USER, (classes + L"\\*\\shell\\NotepadColon").c_str(), 0,
                          KEY_READ, &probe) != ERROR_SUCCESS, "a blocked registration writes nothing");
    Check(notepad_colon::RemoveShellMenus(HKEY_CURRENT_USER, classes) == ShellMenuStatus::conflict,
          "a foreign verb is never removed");
    Check(notepad_colon::RegisterShellMenus(HKEY_CURRENT_USER, classes, {}) == ShellMenuStatus::failed,
          "an empty executable path is rejected");
    ::RegDeleteTreeW(HKEY_CURRENT_USER, classes.c_str());
}
void TestDelimited() {
    using notepad_colon::DetectDelimiter;
    Check(DetectDelimiter("id,name,value\n1,alpha,3.5\n2,beta,4.5\n") == ',', "comma-separated rows");
    Check(DetectDelimiter("id\tname\tvalue\r\n1\talpha\t3.5\r\n") == '\t', "tab-separated rows with CRLF");
    Check(DetectDelimiter("a;b;c\n1;2;3\n4;5;6\n") == ';', "semicolon-separated rows");
    Check(DetectDelimiter("a|b\n1|2\n") == '|', "pipe-separated rows");
    Check(DetectDelimiter("name,notes\n\"Smith, John\",\"a, b, c\"\nx,y\n") == ',',
          "delimiters inside quotes do not change the column count");
    Check(DetectDelimiter("Hello, world.\nThis is prose, with commas, in places.\nNo table here\n") == 0,
          "prose is not a table");
    Check(DetectDelimiter("a,b,c\n") == 0 && DetectDelimiter("") == 0, "one row is not enough evidence");
    Check(DetectDelimiter("a,b;c\n1,2;3\n4,5;6\n") == ',', "the delimiter with more columns wins ties");
    Check(DetectDelimiter("a,b,c\n1,2,3\n4,5") == ',', "a sample cut inside its last row is tolerated");

    std::vector<notepad_colon::DelimitedField> fields;
    const std::string_view line = "1,\"Smith, \"\"J\"\"\",,last";
    notepad_colon::SplitDelimitedLine(line, ',', fields);
    Check(fields.size() == 4, "quoted delimiters and escaped quotes stay in one field");
    Check(fields.size() == 4 && line.substr(fields[1].start, fields[1].length) == "\"Smith, \"\"J\"\"\"" &&
          fields[2].length == 0 && line.substr(fields[3].start, fields[3].length) == "last",
          "field ranges cover exactly the field text");
    notepad_colon::SplitDelimitedLine("", ',', fields);
    Check(fields.size() == 1 && fields[0].length == 0, "an empty line is one empty field");
    notepad_colon::SplitDelimitedLine("a,", ',', fields);
    Check(fields.size() == 2 && fields[1].start == 2 && fields[1].length == 0, "a trailing delimiter adds an empty field");
    Check(notepad_colon::DelimitedFieldAt("ab,cd,ef", ',', 0) == std::optional<std::size_t>{0} &&
          notepad_colon::DelimitedFieldAt("ab,cd,ef", ',', 2) == std::optional<std::size_t>{0} &&
          notepad_colon::DelimitedFieldAt("ab,cd,ef", ',', 3) == std::optional<std::size_t>{1} &&
          notepad_colon::DelimitedFieldAt("ab,cd,ef", ',', 8) == std::optional<std::size_t>{2} &&
          notepad_colon::DelimitedFieldAt("ab,cd,ef", ',', 99) == std::optional<std::size_t>{2},
          "caret offsets map to field indexes");
}

void TestLineFilter() {
    using notepad_colon::FilterLines;
    using notepad_colon::LineFilterOptions;
    const std::string_view log =
        "2026-01-01 INFO start\r\n2026-01-01 ERROR disk full\r\n2026-01-01 info again\n\nlast Error line";
    auto result = FilterLines(log, "error");
    Check(result.lines.size() == 2 && result.lines[0].line_number == 2 &&
          result.lines[0].text == "2026-01-01 ERROR disk full" && result.lines[1].line_number == 5 &&
          result.lines[1].text == "last Error line" && result.lines_scanned == 5,
          "case-insensitive filter keeps matching lines with their numbers");
    Check(result.lines[0].offset == 23 && log.substr(result.lines[1].offset) == "last Error line",
          "kept lines record their byte offset");
    result = FilterLines(log, "ERROR", {.match_case = true});
    Check(result.lines.size() == 1 && result.lines[0].line_number == 2, "match case");
    result = FilterLines(log, "info", {.invert = true});
    Check(result.lines.size() == 3 && result.lines[1].text.empty() && result.lines[1].line_number == 4,
          "inverted filter keeps non-matching lines, including empty ones");
    result = FilterLines(log, R"(^\d{4}-\d\d-\d\d (ERROR|INFO) )", {.match_case = true, .regular_expression = true});
    Check(result.lines.size() == 2 && result.lines[1].line_number == 2, "regular expression per line");
    result = FilterLines(log, "(", {.regular_expression = true});
    Check(!result.error.empty() && result.lines.empty(), "invalid regular expression is reported");
    result = FilterLines(log, "");
    Check(result.lines.size() == 5, "an empty query keeps every line");
    result = FilterLines("", "x");
    Check(result.lines.empty() && result.lines_scanned == 0, "empty input");
    result = FilterLines("中文 错误\n正常\n", "错误");
    Check(result.lines.size() == 1 && result.lines[0].text == "中文 错误", "non-ASCII queries match bytewise");
    result = FilterLines("a\nb\na\na\n", "a", {.maximum_lines = 2});
    Check(result.lines.size() == 2 && result.truncated, "the line limit truncates");
    result = FilterLines("aaaa\naaaa\naaaa\n", "a", {.maximum_bytes = 9});
    Check(result.lines.size() == 2 && result.truncated, "the byte limit truncates");
    result = FilterLines(std::string(5000, 'x') + "\nshort x\n", "x", {.maximum_line_bytes = 100});
    Check(result.lines.size() == 2 && result.lines[0].text.size() == 100 && result.lines[1].text == "short x",
          "over-long kept lines are cut, later lines keep their numbers");
    std::stop_source source;
    source.request_stop();
    Check(FilterLines(log, "a", {}, source.get_token()).cancelled, "cancellation is honoured");

    // Streaming across chunk boundaries must agree with a line-by-line scan.
    std::string big;
    std::vector<std::uint64_t> expected;
    std::uint64_t number = 0;
    while (big.size() < 9u * 1024 * 1024) {
        ++number;
        const bool hit = number % 977 == 0;
        // Varying lengths make chunk boundaries fall at every position in a line.
        big += std::string(number % 211, 'p') + (hit ? " NEEDLE " : " filler ") + std::to_string(number);
        big += number % 3 == 0 ? "\r\n" : "\n";
        if (hit) expected.push_back(number);
    }
    big += "tail NEEDLE without newline";
    expected.push_back(number + 1);
    std::size_t reads = 0, largest_read = 0;
    const notepad_colon::ChunkReader reader = [&](std::uint64_t offset, std::size_t length) {
        ++reads;
        largest_read = (std::max)(largest_read, length);
        const auto part = std::string_view{big}.substr(static_cast<std::size_t>(offset), length);
        return std::vector<std::uint8_t>{part.begin(), part.end()};
    };
    result = FilterLines(reader, big.size(), "needle");
    bool same = result.lines.size() == expected.size();
    for (std::size_t index = 0; same && index < expected.size(); ++index) {
        const auto& line = result.lines[index];
        same = line.line_number == expected[index] &&
               line.text.find("NEEDLE") != std::string::npos &&
               std::string_view{big}.substr(static_cast<std::size_t>(line.offset), line.text.size()) == line.text;
    }
    Check(same, "chunked scan finds exactly the expected lines at the right offsets");
    Check(result.lines_scanned == number + 1 && result.bytes_scanned == big.size() && !result.truncated,
          "chunked scan counts every line and byte");
    Check(reads >= 3 && largest_read <= 4u * 1024 * 1024, "the source is read in bounded chunks");

    const auto formatted = notepad_colon::FormatFilteredLines(FilterLines("a\nb\nc\nd\ne\nf\ng\nh\ni\nabc\n", "a"));
    Check(formatted == " 1: a\r\n10: abc\r\n", "formatted output right-aligns line numbers");
}

void TestHighlightRules() {
    using notepad_colon::HighlightColor;
    std::string error;
    const auto rules = notepad_colon::ParseHighlightRules(" red = ERROR|FATAL ; orange=WARN;gray=~debug | Trace ;", &error);
    Check(rules && rules->size() == 3 && (*rules)[0].color == HighlightColor::red &&
          (*rules)[0].terms == std::vector<std::string>{"ERROR", "FATAL"} && (*rules)[0].match_case &&
          !(*rules)[2].match_case && (*rules)[2].terms == std::vector<std::string>{"debug", "trace"},
          "rules parse with alternatives, spacing and the case-insensitive marker");
    if (rules) {
        Check(notepad_colon::MatchHighlightRule("12:00 ERROR boom", *rules) == std::optional<std::size_t>{0} &&
              notepad_colon::MatchHighlightRule("12:00 error boom", *rules) == std::nullopt &&
              notepad_colon::MatchHighlightRule("WARN then ERROR", *rules) == std::optional<std::size_t>{0} &&
              notepad_colon::MatchHighlightRule("a WARNING", *rules) == std::optional<std::size_t>{1} &&
              notepad_colon::MatchHighlightRule("x DeBuG y", *rules) == std::optional<std::size_t>{2} &&
              notepad_colon::MatchHighlightRule("", *rules) == std::nullopt,
              "the first matching rule wins and case sensitivity is per rule");
        const auto text = notepad_colon::FormatHighlightRules(*rules);
        Check(text == "red=ERROR|FATAL; orange=WARN; gray=~debug|trace", "rules format canonically");
        const auto again = notepad_colon::ParseHighlightRules(text);
        Check(again && notepad_colon::FormatHighlightRules(*again) == text, "format and parse round-trip");
    }
    Check(!notepad_colon::ParseHighlightRules("pink=ERROR", &error) && error.find("pink") != std::string::npos,
          "unknown colours are rejected with a message");
    Check(!notepad_colon::ParseHighlightRules("ERROR", &error) && !notepad_colon::ParseHighlightRules("red=", &error) &&
          !notepad_colon::ParseHighlightRules("red= | ", &error), "malformed rules are rejected");
    const auto empty = notepad_colon::ParseHighlightRules("  ;  ");
    Check(empty && empty->empty(), "an empty rule list is valid and disables highlighting");
    const auto defaults = notepad_colon::DefaultHighlightRules();
    Check(notepad_colon::MatchHighlightRule("java.lang.NullPointerException", defaults) == std::optional<std::size_t>{0} &&
          notepad_colon::MatchHighlightRule("[WARN] slow", defaults) == std::optional<std::size_t>{1} &&
          notepad_colon::MatchHighlightRule("[INFO] ok", defaults) == std::nullopt,
          "default rules classify common log levels");
    const auto reparsed = notepad_colon::ParseHighlightRules(notepad_colon::FormatHighlightRules(defaults));
    Check(reparsed && reparsed->size() == defaults.size(), "default rules survive a round-trip");
}
}  // namespace

void TestChangeReview() {
    using notepad_colon::CompareText;
    using notepad_colon::GroupChangeHunks;
    using notepad_colon::MergeReviewedText;
    const std::wstring before = L"alpha\r\nbeta\r\ngamma\r\ndelta\r\nepsilon\r\nzeta\r\n";
    const std::wstring after = L"alpha\r\nBETA\r\nbeta two\r\ngamma\r\nepsilon\r\nzeta\r\nomega\r\n";
    const auto comparison = CompareText(before, after);
    const auto hunks = GroupChangeHunks(comparison);
    Check(hunks.size() == 3, "three separate hunks");
    if (hunks.size() == 3) {
        Check(hunks[0].left_line == 2 && hunks[0].right_line == 2 && hunks[0].removed == 1 &&
                  hunks[0].added == 2, "replacement hunk counts both sides");
        Check(hunks[1].removed == 1 && hunks[1].added == 0 && hunks[1].left_line == 4,
              "deletion hunk");
        Check(hunks[2].removed == 0 && hunks[2].added == 1 && hunks[2].right_line == 7,
              "insertion hunk at the end");
    }
    Check(MergeReviewedText(before, after, comparison, {false, false, false}) == after,
          "accepting every hunk yields the new text");
    Check(MergeReviewedText(before, after, comparison, {true, true, true}) == before,
          "rejecting every hunk restores the old text");
    Check(MergeReviewedText(before, after, comparison, {true, false, false}) ==
              L"alpha\r\nbeta\r\ngamma\r\nepsilon\r\nzeta\r\nomega\r\n", "rejecting only the first hunk");
    Check(MergeReviewedText(before, after, comparison, {false, true, false}) ==
              L"alpha\r\nBETA\r\nbeta two\r\ngamma\r\ndelta\r\nepsilon\r\nzeta\r\nomega\r\n",
          "rejecting only the deletion puts the line back");
    Check(MergeReviewedText(before, after, comparison, {false, false, true}) ==
              L"alpha\r\nBETA\r\nbeta two\r\ngamma\r\nepsilon\r\nzeta\r\n", "rejecting only the addition");
    Check(MergeReviewedText(before, after, comparison, {}) == after,
          "missing decisions mean accepted");

    // A final line without a line break must not be glued to what follows it.
    const std::wstring short_before = L"one\ntwo";
    const std::wstring short_after = L"one\ntwo\nthree\n";
    const auto short_comparison = CompareText(short_before, short_after);
    Check(MergeReviewedText(short_before, short_after, short_comparison, {true}) == L"one\ntwo\n" ||
              MergeReviewedText(short_before, short_after, short_comparison, {true}) == L"one\ntwo",
          "rejected trailing addition");
    const std::wstring tail_before = L"one\ntwo\nlast";
    const std::wstring tail_after = L"one\nlast";
    const auto tail_comparison = CompareText(tail_before, tail_after);
    Check(MergeReviewedText(tail_before, tail_after, tail_comparison, {true}) == tail_before,
          "rejected deletion before an unterminated last line");
    Check(GroupChangeHunks(CompareText(L"same\n", L"same\n")).empty(), "identical text has no hunks");
    Check(MergeReviewedText(L"", L"new\n", CompareText(L"", L"new\n"), {true}).empty(),
          "rejecting everything added to an empty file");

    // Head and tail trimming: a change deep inside a long file stays one hunk,
    // and files too large to align still merge correctly.
    std::wstring long_before, long_after;
    for (int line = 0; line < 6000; ++line) {
        const auto text = L"line " + std::to_wstring(line) + L"\n";
        long_before += text;
        long_after += line == 3000 ? L"changed\n" : text;
    }
    const auto long_comparison = CompareText(long_before, long_after);
    const auto long_hunks = GroupChangeHunks(long_comparison);
    Check(long_comparison.changed_lines == 1 && long_hunks.size() == 1 &&
              long_hunks.front().left_line == 3001, "single change inside a long file");
    Check(MergeReviewedText(long_before, long_after, long_comparison, {true}) == long_before,
          "long file hunk rejected");
    std::wstring huge_before, huge_after;
    for (int line = 0; line < 5000; ++line) {
        huge_before += L"a" + std::to_wstring(line) + L"\n";
        huge_after += L"b" + std::to_wstring(line) + L"\n";
    }
    const auto huge_comparison = CompareText(L"head\n" + huge_before + L"tail\n",
                                             L"head\n" + huge_after + L"tail\n");
    Check(GroupChangeHunks(huge_comparison).size() == 1 &&
              huge_comparison.lines.front().kind == notepad_colon::DifferenceKind::equal &&
              huge_comparison.lines.back().kind == notepad_colon::DifferenceKind::equal,
          "region too large to align becomes one hunk");
    Check(MergeReviewedText(L"head\n" + huge_before + L"tail\n", L"head\n" + huge_after + L"tail\n",
                            huge_comparison, {true}) == L"head\n" + huge_before + L"tail\n" &&
              MergeReviewedText(L"head\n" + huge_before + L"tail\n", L"head\n" + huge_after + L"tail\n",
                                huge_comparison, {false}) == L"head\n" + huge_after + L"tail\n",
          "oversized hunk merges both ways");
}

void TestColumnTools() {
    using notepad_colon::ExtractColumn;
    using notepad_colon::HeaderRow;
    using notepad_colon::SortDelimitedRows;
    Check(notepad_colon::UnquoteDelimitedField("\"a \"\"quoted\"\" b\"") == "a \"quoted\" b",
          "quoted field unescaped");
    Check(notepad_colon::UnquoteDelimitedField(" plain ") == "plain", "plain field trimmed");

    const std::string table =
        "id,name,city\r\n1,alpha,\"Paris, FR\"\r\n2,beta,Rome\r\n\r\nshort\r\n3,gamma,\"say \"\"hi\"\"\"\r\n";
    const auto city = ExtractColumn(table, ',', 2);
    Check(city.text == "city\r\nParis, FR\r\nRome\r\nsay \"hi\"\r\n" && city.rows == 4 &&
              city.missing == 1 && !city.truncated, "column extracted and unquoted");
    Check(ExtractColumn(table, ',', 2, {.unquote = false}).text ==
              "city\r\n\"Paris, FR\"\r\nRome\r\n\"say \"\"hi\"\"\"\r\n", "column extracted verbatim");
    Check(ExtractColumn(table, ',', 9).rows == 0, "column past the end is empty");
    const auto limited = ExtractColumn(table, ',', 0, {.maximum_rows = 2});
    Check(limited.truncated && limited.text == "id\r\n1\r\n", "row limit stops extraction");
    Check(ExtractColumn("a\tb\nc\td", '\t', 1).text == "b\r\nd\r\n", "last row without a line break");

    // Rows that span chunk boundaries (4 MiB chunks) must come out whole.
    std::string big;
    std::string expected;
    for (int row = 0; row < 300000; ++row) {
        const auto number = std::to_string(row);
        big += "row," + number + ",padding-padding-padding\n";
        expected += number + "\r\n";
    }
    const auto streamed = ExtractColumn(big, ',', 1);
    Check(streamed.rows == 300000 && streamed.text == expected, "extraction across chunk boundaries");
    std::stop_source cancelled;
    cancelled.request_stop();
    Check(ExtractColumn(big, ',', 1, {}, cancelled.get_token()).cancelled, "extraction can be cancelled");

    const std::string scores = "name,score\r\nbeta,10\r\nalpha,9\r\ngamma,100\r\ndelta,n/a\r\n";
    Check(notepad_colon::HasHeaderRow(scores, ','), "header row detected");
    Check(!notepad_colon::HasHeaderRow("1,2\r\n3,4\r\n", ','), "numeric first row is data");
    Check(!notepad_colon::HasHeaderRow("a,b\r\nc,d\r\n", ','), "all-text table has no detectable header");
    Check(SortDelimitedRows(scores, ',', 1, false) ==
              "name,score\r\nalpha,9\r\nbeta,10\r\ngamma,100\r\ndelta,n/a\r\n",
          "numbers sort numerically, before text, under the header");
    Check(SortDelimitedRows(scores, ',', 1, true) ==
              "name,score\r\ndelta,n/a\r\ngamma,100\r\nbeta,10\r\nalpha,9\r\n", "descending order");
    Check(SortDelimitedRows(scores, ',', 0, false) ==
              "name,score\r\nalpha,9\r\nbeta,10\r\ndelta,n/a\r\ngamma,100\r\n", "text column");
    Check(SortDelimitedRows(scores, ',', 0, false, HeaderRow::absent) ==
              "alpha,9\r\nbeta,10\r\ndelta,n/a\r\ngamma,100\r\nname,score\r\n", "header treated as data on request");
    Check(SortDelimitedRows("b,2\na,1", ',', 0, false) == "a,1\nb,2",
          "missing final line break stays missing");
    Check(SortDelimitedRows("x,1\ny,1\nz,0\n", ',', 1, false) == "z,0\nx,1\ny,1\n", "equal keys keep their order");
    Check(SortDelimitedRows("only\n", ',', 0, false) == "only\n", "single row unchanged");
    Check(SortDelimitedRows("k,v\n\"b, x\",2\n\"a, y\",1\n", ',', 0, false, HeaderRow::present) ==
              "k,v\n\"a, y\",1\n\"b, x\",2\n", "quoted fields sort by their value");

    using notepad_colon::FilteredLineNumber;
    Check(FilteredLineNumber("   12: text") == std::optional<std::uint64_t>{12}, "filtered row number");
    Check(FilteredLineNumber("7: 9: nested") == std::optional<std::uint64_t>{7}, "first number wins");
    Check(!FilteredLineNumber("text 12: no") && !FilteredLineNumber("12 no colon") &&
              !FilteredLineNumber("0: zero") && !FilteredLineNumber(""), "rows without a line number");
}

int main() {
    TestLanguages();
    TestEncodingDetection();
    TestReplaceInText();
    TestReplaceInWorkspace();
    TestLocalization();
    TestShellMenus();
    TestDelimited();
    TestLineFilter();
    TestHighlightRules();
    TestChangeReview();
    TestColumnTools();
    if (failures == 0) std::cout << "feature tests passed\n";
    return failures == 0 ? 0 : 1;
}
