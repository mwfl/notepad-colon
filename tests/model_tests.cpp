#include <notepad_colon/document.h>
#include <notepad_colon/comparison.h>
#include <notepad_colon/configuration.h>
#include <notepad_colon/editing.h>
#include <notepad_colon/editor_config.h>
#include <notepad_colon/encoding_analysis.h>
#include <notepad_colon/session.h>
#include <notepad_colon/session_writer.h>
#include <notepad_colon/text.h>
#include <notepad_colon/tree_sitter_document.h>
#include <notepad_colon/language.h>
#include <notepad_colon/language_registry.h>
#include <notepad_colon/lightweight_completion.h>
#include <notepad_colon/large_file_buffer.h>
#include <notepad_colon/large_file.h>
#include <notepad_colon/macro.h>
#include <notepad_colon/mapped_file.h>
#include <notepad_colon/output.h>
#include <notepad_colon/preferences.h>
#include <notepad_colon/recovery.h>
#include <notepad_colon/workspace.h>
#include <notepad_colon/workspace_state.h>

#include <windows.h>

#include <array>
#include <iostream>
#include <fstream>

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        ++failures;
    }
}
}

int main() {
    {
        constexpr std::string_view source =
            "def compute_value():\n    print(compute_value())\ncomp";
        const std::array symbols{notepad_colon::DocumentSymbol{
            "compute_value", "function", 4, 17}};
        const auto completion = notepad_colon::CompleteLocally(
            source, source.size(), notepad_colon::Language::python, symbols);
        Check(completion.prefix_bytes == 4 &&
                  std::ranges::find(completion.candidates, "compute_value") !=
                      completion.candidates.end(),
              "local completion combines current-document words and Tree-sitter symbols");
        std::string oversized(8u * 1024u * 1024u + 1, 'a');
        Check(notepad_colon::CompleteLocally(oversized, oversized.size(),
                  notepad_colon::Language::python).candidates.empty(),
              "local completion refuses large documents");
    }
    {
        const auto root = std::filesystem::temp_directory_path() /
            (L"notepad-colon-editorconfig-" + std::to_wstring(::GetCurrentProcessId()));
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::filesystem::create_directories(root / L"src");
        std::ofstream(root / L".editorconfig") <<
            "root = true\n[*]\nindent_style = space\nindent_size = 2\nend_of_line = lf\n"
            "trim_trailing_whitespace = true\ninsert_final_newline = true\n"
            "[*.cpp]\nindent_size = 4\ncharset = utf-8\n";
        const auto settings = notepad_colon::ResolveEditorConfig(root / L"src" / L"sample.cpp");
        Check(settings.use_tabs && !*settings.use_tabs && settings.indent_size == 4u &&
                  settings.line_ending == notepad_colon::LineEnding::lf &&
                  settings.encoding == notepad_colon::EncodingKind::utf8 &&
                  settings.trim_trailing_whitespace == true &&
                  settings.insert_final_newline == true,
              ".editorconfig resolves inherited and language-specific properties");
        std::filesystem::remove_all(root, ignored);
    }
    {
        constexpr std::string_view json = R"({"name":"colon","enabled":true,"count":4})";
        notepad_colon::TreeSitterDocument syntax;
        Check(syntax.ConfigureJson() && syntax.Parse(json) && !syntax.HasErrors() &&
                  std::ranges::any_of(syntax.Highlights(0, static_cast<std::uint32_t>(json.size())),
                      [](const auto& span) { return span.kind == notepad_colon::SyntaxKind::property; }),
              "Tree-sitter JSON grammar parses and highlights properties");
    }
    {
        constexpr std::string_view source =
            "namespace demo { class Widget {}; int compute(int value) { "
            "return value + 42; } } // tree-sitter\n";
        notepad_colon::TreeSitterDocument syntax;
        Check(syntax.ConfigureCpp() && syntax.Parse(source),
              "Tree-sitter C++ parser configures and parses");
        const auto highlights = syntax.Highlights(0, static_cast<std::uint32_t>(source.size()));
        Check(std::ranges::any_of(highlights, [](const auto& span) {
                  return span.kind == notepad_colon::SyntaxKind::keyword;
              }) && std::ranges::any_of(highlights, [](const auto& span) {
                  return span.kind == notepad_colon::SyntaxKind::comment;
              }), "Tree-sitter emits semantic keyword and comment ranges");
        const auto symbols = syntax.Symbols(source);
        Check(std::ranges::any_of(symbols, [](const auto& symbol) {
                  return symbol.name == "compute" && symbol.kind == "function";
              }) && std::ranges::any_of(symbols, [](const auto& symbol) {
                  return symbol.name == "Widget" && symbol.kind == "class";
              }), "Tree-sitter extracts document symbols");
        const auto value = source.find("42");
        std::string edited{source};
        edited.replace(value, 2, "420");
        const notepad_colon::SyntaxEdit edit{
            static_cast<std::uint32_t>(value), static_cast<std::uint32_t>(value + 2),
            static_cast<std::uint32_t>(value + 3), 0, static_cast<std::uint32_t>(value),
            0, static_cast<std::uint32_t>(value + 2), 0,
            static_cast<std::uint32_t>(value + 3)};
        Check(syntax.Reparse(edited, edit) && !syntax.HasErrors(),
              "Tree-sitter incrementally reparses an edit");
    }
    {
        struct SyntaxCase { notepad_colon::Language language; std::string source; std::string symbol; };
        const std::array cases{
            SyntaxCase{notepad_colon::Language::python,
                       "class Widget:\n    def compute(self):\n        return 42\n", "Widget"},
            SyntaxCase{notepad_colon::Language::javascript,
                       "class Widget { compute() { return 42; } }\n", "Widget"},
            SyntaxCase{notepad_colon::Language::typescript,
                       "interface Widget { compute(): number }\n", "Widget"}};
        for (const auto& value : cases) {
            notepad_colon::TreeSitterDocument syntax;
            const bool configured = value.language == notepad_colon::Language::python
                ? syntax.ConfigurePython() : value.language == notepad_colon::Language::javascript
                    ? syntax.ConfigureJavaScript() : syntax.ConfigureTypeScript();
            Check(configured && syntax.Parse(value.source) &&
                      !syntax.Highlights(0, static_cast<std::uint32_t>(value.source.size())).empty() &&
                      std::ranges::any_of(syntax.Symbols(value.source), [&](const auto& item) {
                          return item.name == value.symbol;
                      }), "Python, JavaScript, and TypeScript have built-in Tree-sitter support");
        }
        notepad_colon::TreeSitterDocument tsx;
        constexpr std::string_view source = "export function App() { return <main>Hello</main>; }\n";
        Check(tsx.ConfigureTypeScript(true) && tsx.Parse(source) && !tsx.HasErrors(),
              "TSX uses the dedicated built-in grammar");
    }
    {
        const auto source = std::filesystem::temp_directory_path() /
            (L"notepad-colon-piece-bom-" + std::to_wstring(::GetCurrentProcessId()) + L".txt");
        { std::ofstream output(source, std::ios::binary); output << "\xef\xbb\xbf" "alpha"; }
        notepad_colon::LargeFileBuffer buffer;
        const std::array marker{static_cast<std::uint8_t>('X')};
        const auto opened = buffer.Open(source);
        const auto window = opened ? buffer.ReadTextWindow(0, 64,
            notepad_colon::EncodingKind::utf8_bom) : std::nullopt;
        const auto inserted = window && buffer.Insert(window->decoded_offset, marker);
        const auto merged = buffer.Read(0, 9);
        Check(window && window->decoded_offset == 3 && window->text == L"alpha" && inserted &&
                  std::string(merged.begin(), merged.end()) == "\xef\xbb\xbf" "Xalpha",
              "UTF-8 BOM large window maps editor byte zero after the BOM");
        std::error_code ignored; std::filesystem::remove(source, ignored);
    }
    {
        const auto source = std::filesystem::temp_directory_path() /
            (L"notepad-colon-piece-source-" + std::to_wstring(::GetCurrentProcessId()) + L".txt");
        const auto saved = std::filesystem::temp_directory_path() /
            (L"notepad-colon-piece-saved-" + std::to_wstring(::GetCurrentProcessId()) + L".txt");
        { std::ofstream output(source, std::ios::binary); output << "alpha beta gamma"; }
        notepad_colon::LargeFileBuffer buffer;
        const std::array inserted{static_cast<std::uint8_t>('X'), static_cast<std::uint8_t>('Y')};
        const bool edited = buffer.Open(source) && buffer.Replace(6, 4, inserted);
        const auto merged = buffer.Read(0, 64);
        Check(edited && std::string(merged.begin(), merged.end()) == "alpha XY gamma",
              "piece table edits mapped source without loading the whole file");
        Check(buffer.IsModified() && buffer.SaveAs(saved) && !buffer.IsModified(),
              "piece table streams edits to an atomic replacement file");
        std::ifstream input(saved, std::ios::binary);
        const std::string result{std::istreambuf_iterator<char>{input}, {}};
        Check(result == "alpha XY gamma", "piece table save preserves merged content");
        { std::ofstream external(saved, std::ios::binary | std::ios::app); external << '!'; }
        Check(buffer.Insert(0, inserted) && !buffer.SaveAs(saved),
              "piece table refuses to replace a source changed externally during editing");
        std::error_code ignored;
        std::filesystem::remove(source, ignored);
        std::filesystem::remove(saved, ignored);
    }
    {
        const auto directory = std::filesystem::temp_directory_path() /
            (L"notepad-colon-language-test-" + std::to_wstring(::GetCurrentProcessId()));
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::filesystem::create_directories(directory);
        std::ofstream(directory / L"acme-highlights.scm", std::ios::binary) <<
            "(comment) @comment\n[\"return\"] @keyword\n";
        std::ofstream(directory / L"acme.json", std::ios::binary) <<
            R"({"id":"acme-script","name":"Acme Script","extensions":[".acme"],"fallbackLexer":"cpp","treeSitter":{"grammar":"cpp","highlights":"acme-highlights.scm"}})";
        std::ofstream(directory / L"data-highlights.scm", std::ios::binary) <<
            "(string) @string\n(number) @number\n(pair key: (string) @property)\n";
        std::ofstream(directory / L"data.json", std::ios::binary) <<
            R"({"id":"acme-data","name":"Acme Data","extensions":[".adata"],"fallbackLexer":"json","treeSitter":{"grammar":"json","highlights":"data-highlights.scm"}})";
        std::filesystem::copy_file(std::filesystem::path{__FILE__}.parent_path() /
            L"fixtures" / L"tree-sitter-json.wasm", directory / L"tree-sitter-json.wasm",
            std::filesystem::copy_options::overwrite_existing);
        std::ofstream(directory / L"wasm.json", std::ios::binary) <<
            R"({"id":"wasm-data","name":"Wasm Data","extensions":[".wdata"],"fallbackLexer":"json","treeSitter":{"grammar":"wasm","language":"json","module":"tree-sitter-json.wasm","highlights":"data-highlights.scm"}})";
        { const std::array<char, 8> invalid_wasm{0, 'a', 's', 'm', 2, 0, 0, 0};
          std::ofstream bad_module(directory / L"bad.wasm", std::ios::binary);
          bad_module.write(invalid_wasm.data(), invalid_wasm.size()); }
        std::ofstream(directory / L"bad-wasm.json", std::ios::binary) <<
            R"({"id":"bad-wasm","name":"Bad Wasm","extensions":[".badwasm"],"treeSitter":{"grammar":"wasm","language":"json","module":"bad.wasm","highlights":"data-highlights.scm"}})";
        std::ofstream(directory / L"unsafe.json", std::ios::binary) <<
            R"({"id":"unsafe","name":"Unsafe","extensions":[".unsafe"],"treeSitter":{"library":"../outside.dll","highlights":"../outside.scm"}})";
        notepad_colon::LanguageRegistry registry;
        Check(registry.LoadDirectory(directory) == 3 && registry.Find("acme-script") &&
                  registry.Detect(L"sample.acme") == registry.Find("acme-script"),
              "custom language definitions load and participate in detection");
        const auto* acme = registry.Find("acme-script");
        notepad_colon::TreeSitterDocument custom_syntax;
        Check(acme && acme->tree_sitter &&
                  custom_syntax.ConfigureCpp(acme->tree_sitter->highlights_query) &&
                  custom_syntax.Parse("int run() { return 1; } // custom\n") &&
                  std::ranges::any_of(custom_syntax.Highlights(0, 40), [](const auto& span) {
                      return span.kind == notepad_colon::SyntaxKind::keyword;
                  }), "custom language queries drive a sandboxed built-in Tree-sitter grammar");
        Check(registry.Errors().size() == 2 && !registry.Find("unsafe") && !registry.Find("bad-wasm"),
              "custom language definitions reject traversal and invalid Wasm versions");
        const auto* data = registry.Find("acme-data");
        notepad_colon::TreeSitterDocument data_syntax;
        Check(data && data->tree_sitter && data->tree_sitter->grammar == "json" &&
                  data_syntax.ConfigureJson(data->tree_sitter->highlights_query) &&
                  data_syntax.Parse(R"({"answer":42})"),
              "custom language definition selects the sandboxed JSON grammar");
        const auto* wasm_data = registry.Find("wasm-data");
        Check(wasm_data && wasm_data->tree_sitter &&
                  wasm_data->tree_sitter->wasm_language_name == "json" &&
                  wasm_data->tree_sitter->wasm_bytes.size() > 8,
              "custom language definition validates and loads a bounded Wasm grammar");
        registry.ResetBuiltins();
        Check(!registry.Find("acme-script") && registry.Find("cpp") &&
                  registry.Detect(L"source.cpp") == registry.Find("cpp"),
              "language registry reload resets to stable builtins");
        std::filesystem::remove_all(directory, ignored);
    }
    const std::vector<std::uint8_t> utf8_bytes{'a', '\r', '\n', 0xe2, 0x80, 0xae, 'b', '\n'};
    const auto utf8_analysis = notepad_colon::AnalyzeEncoding(utf8_bytes);
    Check(utf8_analysis.encoding == notepad_colon::EncodingKind::utf8 && utf8_analysis.valid &&
              utf8_analysis.eol.crlf == 1 && utf8_analysis.eol.lf == 1 &&
              utf8_analysis.eol.Mixed() && utf8_analysis.unicode_risks.size() == 1,
          "strict UTF-8 analysis must report mixed EOL and bidi controls");
    const std::vector<std::uint8_t> invalid_utf8{0xc0, 0xaf, 'x'};
    const auto ansi_analysis = notepad_colon::AnalyzeEncoding(invalid_utf8, 1252);
    Check(ansi_analysis.encoding == notepad_colon::EncodingKind::ansi &&
              ansi_analysis.invalid_byte_offsets.size() == 2 && ansi_analysis.code_page == 1252,
          "invalid UTF-8 must preserve byte offsets and select the requested ANSI code page");
    const auto encoded_utf16 = notepad_colon::EncodeText(L"alpha Ω", notepad_colon::EncodingKind::utf16_be);
    Check(encoded_utf16 && encoded_utf16->size() >= 4 && (*encoded_utf16)[0] == 0xfe &&
              notepad_colon::DecodeBytes(*encoded_utf16, notepad_colon::EncodingKind::utf16_be) == L"alpha Ω",
          "UTF-16 big-endian conversion must round trip with BOM");
    Check(!notepad_colon::EncodeText(L"Ω", notepad_colon::EncodingKind::ansi, 1252),
          "lossy ANSI conversion must be rejected");
    const auto encoded_path = std::filesystem::temp_directory_path() /
        (L"notepad-colon-encoding-test-" + std::to_wstring(::GetCurrentProcessId()) + L".txt");
    Check(notepad_colon::WriteEncodedFileAtomic(encoded_path, L"café", notepad_colon::EncodingKind::ansi, 1252),
          "representable ANSI text must save atomically");
    const auto encoded_state = notepad_colon::CaptureFileState(encoded_path);
    {
        std::ofstream(encoded_path, std::ios::binary | std::ios::app) << "external";
    }
    Check(!notepad_colon::WriteEncodedFileAtomic(encoded_path, L"overwrite",
              notepad_colon::EncodingKind::ansi, 1252,
              notepad_colon::EncodedWriteExpectation{encoded_state.size, encoded_state.last_write,
                                                       encoded_state.exists}),
          "ANSI atomic save must reject stale disk expectations");
    std::error_code encoded_ignored; std::filesystem::remove(encoded_path, encoded_ignored);
    const auto statistics = notepad_colon::CalculateStatistics(L"one two\r\n\r\n三");
    Check(statistics.words == 3 && statistics.lines == 3 && statistics.non_blank_lines == 2 &&
              statistics.characters_without_whitespace == 7 && statistics.utf8_bytes > statistics.characters,
          "document statistics count words lines and UTF-8 bytes");
    Check(notepad_colon::EscapeHtml(L"<&\"'>") == L"&lt;&amp;&quot;&#39;&gt;", "HTML escaping");
    const auto html = notepad_colon::ExportHtmlDocument(L"A&B", L"<code>", true);
    Check(html.find(L"A&amp;B") != std::wstring::npos &&
              html.find(L"&lt;code&gt;") != std::wstring::npos &&
              html.find(L"#1e1e1e") != std::wstring::npos, "standalone HTML export");
    const auto shortcut = notepad_colon::ParseShortcut(7, L"Ctrl+Shift+F5");
    Check(shortcut && notepad_colon::FormatShortcut(*shortcut) == L"Ctrl+Shift+F5",
          "shortcut parses and formats");
    Check(!notepad_colon::ParseShortcut(7, L"Ctrl+NoSuchKey"), "invalid shortcut rejected");
    Check(notepad_colon::FindShortcutConflicts({{1, FCONTROL, 'A'}, {2, FCONTROL, 'A'}}).size() == 1,
          "shortcut conflicts detected");
    notepad_colon::Configuration configuration;
    configuration.preferences.font_name = L"Cascadia Mono";
    configuration.shortcuts = {{1, static_cast<std::uint8_t>(FVIRTKEY | FCONTROL), 'N'}};
    configuration.search_history = {L"needle", L"alpha\\s+beta"};
    notepad_colon::Configuration decoded_configuration;
    Check(notepad_colon::DeserializeConfiguration(
              notepad_colon::SerializeConfiguration(configuration), decoded_configuration) &&
              decoded_configuration.preferences == configuration.preferences &&
              decoded_configuration.shortcuts == configuration.shortcuts &&
              decoded_configuration.search_history == configuration.search_history,
          "configuration round trip");
    notepad_colon::MacroRecorder macro_recorder;
    macro_recorder.Start();
    macro_recorder.RecordText(L"ab"); macro_recorder.RecordText(L"c");
    macro_recorder.RecordCommand(42); macro_recorder.RecordDeleteBackward(1);
    const auto recorded_actions = macro_recorder.Stop();
    Check(recorded_actions.size() == 3 && recorded_actions[0].text == L"abc" &&
              recorded_actions[1].command_id == 42, "macro recorder coalesces typed text");
    const std::vector<notepad_colon::SavedMacro> saved_macros{{L"Unicode 宏", recorded_actions}};
    const auto encoded_macros = notepad_colon::SerializeMacros(saved_macros);
    std::vector<notepad_colon::SavedMacro> decoded_macros;
    Check(notepad_colon::DeserializeMacros(encoded_macros, decoded_macros) &&
              decoded_macros == saved_macros, "macro serialization round trip");
    Check(!notepad_colon::DeserializeMacros("bad", decoded_macros), "invalid macro file rejected");
    const auto comparison = notepad_colon::CompareText(L"same\nold value\ntail", L"same\nnew value\ntail");
    Check(!comparison.identical && comparison.changed_lines == 1, "one modified line detected");
    Check(comparison.lines.size() == 3 &&
              comparison.lines[1].kind == notepad_colon::DifferenceKind::modified,
          "modified lines aligned");
    Check(comparison.lines[1].left_change.length == 3 &&
              comparison.lines[1].right_change.length == 3,
          "intra-line changed spans detected");
    Check(notepad_colon::CompareText(L"One  two", L"one two",
              {.ignore_case = true, .ignore_whitespace = true}).identical,
          "comparison ignore options");
    Check(notepad_colon::CompareText(L"one\r\ntwo\r\n", L"one\ntwo\n").identical,
          "line endings ignored by default");
    Check(!notepad_colon::CompareText(L"one\r\ntwo\r\n", L"one\ntwo\n",
              {.ignore_line_endings = false}).identical,
          "line ending differences can be compared");
    const auto inserted_comparison = notepad_colon::CompareText(L"one\nthree", L"one\ntwo\nthree");
    Check(inserted_comparison.changed_lines == 1 &&
              inserted_comparison.lines[1].kind == notepad_colon::DifferenceKind::inserted &&
              inserted_comparison.lines[1].left_line == 2,
          "inserted line retains destination position");
    const auto deleted_comparison = notepad_colon::CompareText(L"one\ntwo\nthree", L"one\nthree");
    Check(deleted_comparison.changed_lines == 1 &&
              deleted_comparison.lines[1].kind == notepad_colon::DifferenceKind::deleted &&
              deleted_comparison.lines[1].right_line == 2,
          "deleted line retains destination position");
    using notepad_colon::LineOrder;
    Check(notepad_colon::SortLines(L"beta\nAlpha\nalpha\n", LineOrder::ascending, true, true) ==
              L"Alpha\nbeta\n", "case-folded sort and dedupe");
    Check(notepad_colon::SortLines(L"a\r\nb\r\nc", LineOrder::reverse) == L"c\r\nb\r\na",
          "reverse lines preserves CRLF");
    Check(notepad_colon::RemoveBlankLines(L"one\n \t\ntwo\n") == L"one\ntwo\n",
          "blank lines removed");
    Check(notepad_colon::TrimTrailingWhitespace(L"one  \n two\t\n") == L"one\n two\n",
          "trailing whitespace removed");
    Check(notepad_colon::JoinLines(L" one \n\ntwo\n") == L"one two", "lines joined");
    Check(notepad_colon::SplitLines(L"alpha beta gamma", 10) == L"alpha beta\ngamma",
          "long line wrapped at word boundary");
    Check(notepad_colon::TabsToSpaces(L"a\tb", 4) == L"a   b", "tabs use tab stops");
    Check(notepad_colon::SpacesToTabs(L"a   b", 4) == L"a\tb", "spaces use tab stops");
    Check(notepad_colon::ConvertCase(L"hello WORLD. next ONE!", notepad_colon::LetterCase::sentence) ==
              L"Hello world. Next one!", "sentence case conversion");
    const auto escaped = notepad_colon::EscapeJsonString(L"a\n\"b\"");
    Check(escaped == L"a\\n\\\"b\\\"", "JSON escape");
    Check(notepad_colon::UnescapeJsonString(escaped) == std::optional<std::wstring>{L"a\n\"b\""},
          "JSON unescape");
    Check(notepad_colon::Base64Encode("hello") == "aGVsbG8=", "base64 encode");
    Check(notepad_colon::Base64Decode("aGVsbG8=") == std::optional<std::string>{"hello"},
          "base64 decode");
    Check(!notepad_colon::Base64Decode("bad"), "invalid base64 rejected");
    Check(notepad_colon::UrlEncode("hello world/\xE4") == "hello%20world%2F%E4", "URL encode bytes");
    Check(notepad_colon::UrlDecode("hello%20world%2F") == std::optional<std::string>{"hello world/"},
          "URL decode bytes");
    Check(!notepad_colon::UrlDecode("%Q0"), "invalid URL escape rejected");
    Check(notepad_colon::GenerateSequence(3, 3, 2, L",") == L"3,5,7", "sequence generation");
    Check(notepad_colon::EnsureFinalNewline(L"text", L"\r\n") == L"text\r\n",
          "final newline inserted");
    const auto recovery_path = std::filesystem::temp_directory_path() /
        (L"notepad-colon-recovery-test-" + std::to_wstring(::GetCurrentProcessId()));
    notepad_colon::RecoveryStore recovery(recovery_path, 2);
    Check(recovery.Save(L"doc", L"Draft", L"C:\\work\\draft.txt", L"first"),
          "recovery snapshot saves");
    Check(recovery.Save(L"doc", L"Draft", L"C:\\work\\draft.txt", L"second"),
          "second recovery snapshot saves");
    const auto tied_recovery_time = std::filesystem::file_time_type::clock::now();
    for (const auto& entry : std::filesystem::directory_iterator(recovery_path))
        if (entry.path().extension() == L".recovery")
            std::filesystem::last_write_time(entry.path(), tied_recovery_time);
    const auto recovery_items = recovery.List();
    Check(recovery_items.size() == 2, "recovery snapshots listed within retention");
    if (!recovery_items.empty())
        Check(recovery.Load(recovery_items.front()) == std::optional<std::wstring>{L"second"},
              "latest recovery snapshot loads");
    std::error_code recovery_error;
    std::filesystem::remove_all(recovery_path, recovery_error);
    notepad_colon::Preferences preferences;
    Check(notepad_colon::ValidatePreferences(preferences), "default preferences must be valid");
    preferences.font_name.clear();
    preferences.font_size = 100;
    preferences.tab_width = 0;
    preferences.auto_save_seconds = 1;
    preferences.theme = static_cast<notepad_colon::ThemePreference>(99);
    Check(!notepad_colon::ValidatePreferences(preferences), "invalid preferences must be rejected");
    Check(notepad_colon::SanitizePreferences(preferences) == notepad_colon::Preferences{},
          "invalid preferences must sanitize to safe defaults");
    using notepad_colon::FileOpenMode;
    Check(notepad_colon::ClassifyFileSize(32ull * 1024 * 1024) == FileOpenMode::editable,
          "32 MiB must remain editable");
    Check(notepad_colon::ClassifyFileSize(32ull * 1024 * 1024 + 1) ==
              FileOpenMode::protected_read_only,
          "files over the edit limit must be protected");
    Check(notepad_colon::ClassifyFileSize(4ull * 1024 * 1024 * 1024 + 1) ==
              FileOpenMode::unsupported,
          "files beyond the supported limit must be rejected");
    Check(notepad_colon::ClassifyFileSize(1, {2, 1}) == FileOpenMode::unsupported,
          "invalid policy must fail closed");
    notepad_colon::Workspace workspace;
    const auto first = workspace.AddUntitled();
    const auto second = workspace.AddUntitled();
    Check(first != second, "document identifiers must be unique");
    Check(workspace.Documents().size() == 2, "untitled documents must be retained");
    Check(workspace.ActiveId() == second, "new document must become active");
    Check(workspace.Documents()[0].DisplayName() == L"Untitled", "first untitled title");
    Check(workspace.Documents()[1].DisplayName() == L"Untitled 2", "second untitled title");

    const auto path_id = workspace.AddPath(L"C:\\Temp\\Example.cpp");
    const auto duplicate = workspace.AddPath(L"c:\\temp\\example.cpp");
    Check(path_id && path_id == duplicate, "Windows paths must deduplicate case-insensitively");
    Check(workspace.Documents().size() == 3, "duplicate path must not add a document");
    Check(workspace.Remove(*path_id), "active path document must be removable");
    Check(workspace.Active() != nullptr, "removal must choose a neighboring active document");

    notepad_colon::Session original;
    original.active_index = 1;
    original.workspace_path = L"C:\\work";
    original.workspace_paths = {L"C:\\work", L"D:\\shared"};
    original.documents.push_back({L"C:\\work\\ä½ å¥½.cpp", L"", notepad_colon::Encoding::utf8,
                                  notepad_colon::LineEnding::lf, {4, 9, 2}, false});
    original.documents.push_back({{}, L"unsaved\ntext\tΩ", notepad_colon::Encoding::utf8,
                                  notepad_colon::LineEnding::crlf, {0, 7, 0}, true});
    const auto encoded = notepad_colon::SerializeSession(original);
    notepad_colon::Session decoded;
    Check(notepad_colon::DeserializeSession(encoded, decoded), "serialized session must parse");
    Check(decoded.documents.size() == 2 && decoded.active_index == 1, "session shape must round trip");
    Check(decoded.workspace_path == original.workspace_path, "workspace path must round trip");
    Check(decoded.workspace_paths == original.workspace_paths, "multiple workspace roots must round trip");
    if (decoded.documents.size() == 2) {
        Check(decoded.documents[0].path == original.documents[0].path, "Unicode path must round trip");
    Check(decoded.documents[1].recovery_text == original.documents[1].recovery_text,
              "recovery text must round trip");
    }
    Check(!notepad_colon::DeserializeSession("bad", decoded), "invalid sessions must be rejected");
    Check(notepad_colon::DeserializeSession("NPCSESSION\t1\t0\n", decoded),
          "version 1 sessions must remain readable");
    Check(notepad_colon::DeserializeSession("NPCSESSION\t2\t0\tC:\\5c;old\n", decoded) &&
              decoded.workspace_paths.size() == 1,
          "version 2 workspace sessions must migrate to multiple roots");

    const std::wstring mixed = L"one\r\ntwo\nthree\rfour";
    Check(notepad_colon::DetectLineEnding(mixed) == notepad_colon::LineEnding::crlf,
          "CRLF must win mixed line-ending detection");
    Check(notepad_colon::NormalizeLineEndings(mixed, notepad_colon::LineEnding::lf) ==
              L"one\ntwo\nthree\nfour",
          "line endings must normalize to LF");
    Check(notepad_colon::NormalizeLineEndings(mixed, notepad_colon::LineEnding::crlf) ==
              L"one\r\ntwo\r\nthree\r\nfour",
          "line endings must normalize to CRLF");
    Check(notepad_colon::CountLines(mixed) == 4, "mixed endings must count as four lines");

    const auto session_path = std::filesystem::temp_directory_path() /
        (L"notepad-colon-session-test-" + std::to_wstring(::GetCurrentProcessId()) + L".state");
    Check(notepad_colon::SaveSessionAtomic(session_path, original), "session file must save atomically");
    notepad_colon::Session file_session;
    Check(notepad_colon::LoadSession(session_path, file_session), "saved session file must load");
    Check(file_session.documents.size() == original.documents.size(), "session file shape must round trip");
    {
        std::ofstream corrupt(session_path, std::ios::binary | std::ios::trunc);
        corrupt << "NPCSESSION\t3\nD\tbroken";
    }
    Check(!notepad_colon::LoadSession(session_path, file_session),
          "truncated session file must fail closed");
    {
        notepad_colon::SessionWriter writer;
        for (int revision = 0; revision < 20; ++revision) {
            original.documents.front().recovery_text = L"revision-" + std::to_wstring(revision);
            writer.Queue(session_path, original);
        }
        Check(writer.Flush(), "background session writer must flush the newest snapshot");
    }
    Check(notepad_colon::LoadSession(session_path, file_session) &&
              file_session.documents.front().recovery_text == L"revision-19",
          "background session writer must coalesce to the newest revision");
    {
        notepad_colon::SessionWriter writer;
        original.documents.front().recovery_text = L"shutdown-flush";
        writer.Queue(session_path, original);
    }
    Check(notepad_colon::LoadSession(session_path, file_session) &&
              file_session.documents.front().recovery_text == L"shutdown-flush",
          "session writer destruction must flush an in-flight close snapshot");
    Check(!notepad_colon::SaveSessionAtomic(std::filesystem::temp_directory_path(), original),
          "atomic session save must fail cleanly when destination is a directory");
    std::error_code ignored;
    std::filesystem::remove(session_path, ignored);

    using notepad_colon::Language;
    Check(notepad_colon::DetectLanguage(L"sample.CPP") == Language::cpp, "C++ extension detection");
    Check(notepad_colon::DetectLanguage(L"CMakeLists.txt") == Language::cmake, "CMake filename detection");
    Check(notepad_colon::DetectLanguage(L"script.ps1") == Language::powershell, "PowerShell detection");
    Check(notepad_colon::DetectLanguage(L"module.cjs") == Language::javascript &&
              notepad_colon::DetectLanguage(L"module.mts") == Language::typescript &&
              notepad_colon::DetectLanguage(L"component.TSX") == Language::typescript,
          "modern JavaScript and TypeScript extensions are detected");
    Check(notepad_colon::DetectLanguage(L"data.yaml") == Language::yaml, "YAML detection");
    Check(notepad_colon::DetectLanguage(L"README") == Language::plain_text, "plain text fallback");
    Check(notepad_colon::LexerName(Language::python) == "python", "Python lexer mapping");
    const auto languages = notepad_colon::AllLanguages();
    Check(languages.size() == 19, "all supported languages are enumerable");
    for (const auto language : languages) {
        const auto& profile = notepad_colon::GetLanguageProfile(language);
        Check(profile.language == language && !profile.name.empty(),
              "language profiles retain their identity and display name");
        Check(language == Language::plain_text || !profile.lexer.empty(),
              "source language profiles provide a lexer");
        Check(profile.lexer == notepad_colon::LexerName(language),
              "language profile and lexer lookup agree");
    }
    Check(notepad_colon::GetLanguageProfile(Language::javascript).primary_keywords.find("function") !=
              std::string_view::npos,
          "JavaScript receives language-specific keywords");
    Check(notepad_colon::GetLanguageProfile(Language::csharp).primary_keywords.find("namespace") !=
              std::string_view::npos,
          "C# receives language-specific keywords");
    Check(notepad_colon::GetLanguageProfile(Language::rust).primary_keywords.find(" fn ") !=
              std::string_view::npos,
          "Rust receives language-specific keywords");

    const auto workspace_path = std::filesystem::temp_directory_path() /
        (L"notepad-colon-workspace-test-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(workspace_path / L"src");
    std::filesystem::create_directories(workspace_path / L"build");
    notepad_colon::WorkspaceCatalog catalog;
    Check(catalog.AddRoot(workspace_path) && !catalog.AddRoot(workspace_path),
          "workspace catalog adds unique roots");
    catalog.SetFavorite(workspace_path, true);
    Check(catalog.Roots().size() == 1 && catalog.Recent().size() == 1 && catalog.Favorites().size() == 1,
          "workspace catalog tracks roots history and favorites");
    notepad_colon::WorkspaceCatalog decoded_catalog;
    Check(notepad_colon::DeserializeWorkspaceCatalog(
              notepad_colon::SerializeWorkspaceCatalog(catalog), decoded_catalog) &&
              decoded_catalog.Roots().size() == 1 && decoded_catalog.Favorites().size() == 1,
          "workspace catalog round trip");
    const auto catalog_path = workspace_path / L"catalog.state";
    notepad_colon::WorkspaceCatalog file_catalog;
    Check(notepad_colon::SaveWorkspaceCatalogAtomic(catalog_path, catalog) &&
              notepad_colon::LoadWorkspaceCatalog(catalog_path, file_catalog) &&
              file_catalog.Favorites().size() == 1,
          "workspace catalog must persist atomically");
    std::filesystem::remove(catalog_path, ignored);
    Check(notepad_colon::IsWithinWorkspaceRoots(workspace_path / L"src" / L"one.txt", {workspace_path}) &&
              !notepad_colon::IsWithinWorkspaceRoots(workspace_path.parent_path() / L"outside.txt", {workspace_path}),
          "workspace root boundary validation");
    Check(!notepad_colon::IsValidWorkspaceName(L"..") &&
              !notepad_colon::IsValidWorkspaceName(L"bad?.txt") &&
              notepad_colon::IsValidWorkspaceName(L"good.txt"), "workspace item names validated");
    Check(notepad_colon::CreateWorkspaceItem(workspace_path / L"src", L"created.txt", false, {workspace_path}) &&
              notepad_colon::RenameWorkspaceItem(workspace_path / L"src" / L"created.txt", L"renamed.txt", {workspace_path}),
          "workspace create and rename stay within roots");
    Check(!notepad_colon::RenameWorkspaceItem(workspace_path, L"renamed-root", {workspace_path}) &&
              !notepad_colon::RecycleWorkspaceItem(workspace_path, {workspace_path}),
          "workspace roots must be protected from destructive item operations");
    std::filesystem::remove(workspace_path / L"src" / L"renamed.txt", ignored);
    {
        std::ofstream(workspace_path / L"src" / L"one.txt", std::ios::binary)
            << "alpha needle beta\nNeedlework must not whole-word match\nneedle again\n";
        std::ofstream(workspace_path / L"src" / L"two.txt", std::ios::binary)
            << "NEEDLE uppercase\n";
        std::ofstream(workspace_path / L"build" / L"ignored.txt", std::ios::binary)
            << "needle ignored\n";
        std::ofstream binary(workspace_path / L"binary.dat", std::ios::binary);
        const char bytes[]{'a', '\0', 'n'};
        binary.write(bytes, sizeof(bytes));
    }
    const auto scan = notepad_colon::ScanWorkspace(workspace_path);
    Check(scan.entries.size() == 4, "workspace scan must include src, two text files, and binary");
    Check(scan.skipped == 1, "workspace scan must skip build directory");
    notepad_colon::SearchOptions search_options;
    search_options.whole_word = true;
    const auto search_result = notepad_colon::SearchWorkspace(workspace_path, L"needle", search_options);
    Check(search_result.matches.size() == 3, "case-insensitive whole-word folder search");
    Check(search_result.files_searched == 2, "folder search must search two text files");
    Check(search_result.files_skipped >= 2, "folder search must report build and binary skips");
    search_options.match_case = true;
    const auto case_result = notepad_colon::SearchWorkspace(workspace_path, L"needle", search_options);
    Check(case_result.matches.size() == 2, "case-sensitive folder search");
    const auto advanced_search_path = workspace_path / L"advanced-search";
    std::filesystem::create_directories(advanced_search_path);
    { std::ofstream(advanced_search_path / L"included.txt", std::ios::binary)
          << "alpha123\nfirst line\nsecond line\n";
      std::ofstream(advanced_search_path / L"ignored.log", std::ios::binary)
          << "alpha999 ignored\n";
      std::ofstream(advanced_search_path / L".gitignore", std::ios::binary)
          << "*.log\n"; }
    notepad_colon::SearchOptions regex_options;
    regex_options.regular_expression = true;
    const auto regex_result = notepad_colon::SearchWorkspace(
        advanced_search_path, LR"(alpha\d+)", regex_options);
    Check(regex_result.matches.size() == 1 && regex_result.matches.front().line == 1,
          "workspace regular expressions honor .gitignore");
    regex_options.use_gitignore = false;
    Check(notepad_colon::SearchWorkspace(
              advanced_search_path, LR"(alpha\d+)", regex_options).matches.size() == 2,
          "workspace search can explicitly include gitignored files");
    notepad_colon::SearchOptions multiline_options;
    multiline_options.multiline = true;
    Check(notepad_colon::SearchWorkspace(advanced_search_path,
              L"first line\nsecond line", multiline_options).matches.size() == 1,
          "workspace literal search supports multiline matches");
    regex_options.regular_expression = true;
    const auto invalid_regex = notepad_colon::SearchWorkspace(
        advanced_search_path, L"(", regex_options);
    Check(!invalid_regex.error.empty(), "invalid workspace regular expressions report an error");
    const auto open_text_result = notepad_colon::SearchText(
        L"open.py", L"alpha\nbeta alpha\n", L"alpha");
    Check(open_text_result.matches.size() == 2 && open_text_result.matches.back().line == 2,
          "open-document search reports every match with line positions");
    notepad_colon::SearchOptions filtered_options;
    filtered_options.include_globs = {L"*.txt"};
    filtered_options.exclude_globs = {L"*ignored*"};
    Check(notepad_colon::SearchWorkspace(
              advanced_search_path, L"alpha", filtered_options).matches.size() == 1,
          "workspace search honors include and exclude file globs");
    std::stop_source cancelled;
    cancelled.request_stop();
    Check(notepad_colon::SearchWorkspace(workspace_path, L"needle", {}, cancelled.get_token()).cancelled,
          "pre-cancelled folder search must report cancellation");
    const auto before_state = notepad_colon::CaptureFileState(workspace_path / L"src" / L"one.txt");
    Check(before_state.exists && before_state.size > 0, "file monitor state must capture a file");

    const auto mapped_utf8_path = workspace_path / L"mapped-utf8.txt";
    {
        std::ofstream output(mapped_utf8_path, std::ios::binary);
        const unsigned char value[]{'a', 'b', 'c', 0xe2, 0x82, 0xac, 'z'};
        output.write(reinterpret_cast<const char*>(value), sizeof(value));
    }
    notepad_colon::MappedFile mapped_utf8;
    Check(mapped_utf8.Open(mapped_utf8_path), "UTF-8 boundary fixture opens");
    const auto utf8_window = mapped_utf8.ReadTextWindow(4, 3, notepad_colon::EncodingKind::utf8);
    Check(utf8_window && utf8_window->decoded_offset == 3 && utf8_window->text == L"€z",
          "mapped UTF-8 window must retain a character crossing the requested boundary");
    mapped_utf8.Close();

    const auto mapped_utf16_path = workspace_path / L"mapped-utf16.txt";
    {
        std::ofstream output(mapped_utf16_path, std::ios::binary);
        const unsigned char value[]{0xff, 0xfe, 'A', 0, 0x3d, 0xd8, 0x00, 0xde, 'B', 0};
        output.write(reinterpret_cast<const char*>(value), sizeof(value));
    }
    notepad_colon::MappedFile mapped_utf16;
    Check(mapped_utf16.Open(mapped_utf16_path), "UTF-16 boundary fixture opens");
    const auto utf16_window = mapped_utf16.ReadTextWindow(6, 4, notepad_colon::EncodingKind::utf16_le);
    Check(utf16_window && utf16_window->decoded_offset == 4 && utf16_window->text == L"😀B",
          "mapped UTF-16 window must retain a surrogate pair crossing the requested boundary");
    mapped_utf16.Close();
    std::filesystem::remove_all(workspace_path, ignored);
    return failures == 0 ? 0 : 1;
}
