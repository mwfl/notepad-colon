#include <notepad_colon/document.h>
#include <notepad_colon/comparison.h>
#include <notepad_colon/configuration.h>
#include <notepad_colon/editing.h>
#include <notepad_colon/session.h>
#include <notepad_colon/text.h>
#include <notepad_colon/language.h>
#include <notepad_colon/large_file.h>
#include <notepad_colon/macro.h>
#include <notepad_colon/output.h>
#include <notepad_colon/preferences.h>
#include <notepad_colon/recovery.h>
#include <notepad_colon/workspace.h>

#include <windows.h>

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
    notepad_colon::Configuration decoded_configuration;
    Check(notepad_colon::DeserializeConfiguration(
              notepad_colon::SerializeConfiguration(configuration), decoded_configuration) &&
              decoded_configuration.preferences == configuration.preferences &&
              decoded_configuration.shortcuts == configuration.shortcuts,
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
    Check(notepad_colon::ClassifyFileSize(256ull * 1024 * 1024 + 1) ==
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
    original.documents.push_back({L"C:\\work\\ä½ å¥½.cpp", L"", notepad_colon::Encoding::utf8,
                                  notepad_colon::LineEnding::lf, {4, 9, 2}, false});
    original.documents.push_back({{}, L"unsaved\ntext\tΩ", notepad_colon::Encoding::utf8,
                                  notepad_colon::LineEnding::crlf, {0, 7, 0}, true});
    const auto encoded = notepad_colon::SerializeSession(original);
    notepad_colon::Session decoded;
    Check(notepad_colon::DeserializeSession(encoded, decoded), "serialized session must parse");
    Check(decoded.documents.size() == 2 && decoded.active_index == 1, "session shape must round trip");
    Check(decoded.workspace_path == original.workspace_path, "workspace path must round trip");
    if (decoded.documents.size() == 2) {
        Check(decoded.documents[0].path == original.documents[0].path, "Unicode path must round trip");
    Check(decoded.documents[1].recovery_text == original.documents[1].recovery_text,
              "recovery text must round trip");
    }
    Check(!notepad_colon::DeserializeSession("bad", decoded), "invalid sessions must be rejected");
    Check(notepad_colon::DeserializeSession("NPCSESSION\t1\t0\n", decoded),
          "version 1 sessions must remain readable");

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
    std::error_code ignored;
    std::filesystem::remove(session_path, ignored);

    using notepad_colon::Language;
    Check(notepad_colon::DetectLanguage(L"sample.CPP") == Language::cpp, "C++ extension detection");
    Check(notepad_colon::DetectLanguage(L"CMakeLists.txt") == Language::cmake, "CMake filename detection");
    Check(notepad_colon::DetectLanguage(L"script.ps1") == Language::powershell, "PowerShell detection");
    Check(notepad_colon::DetectLanguage(L"data.yaml") == Language::yaml, "YAML detection");
    Check(notepad_colon::DetectLanguage(L"README") == Language::plain_text, "plain text fallback");
    Check(notepad_colon::LexerName(Language::python) == "python", "Python lexer mapping");

    const auto workspace_path = std::filesystem::temp_directory_path() /
        (L"notepad-colon-workspace-test-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(workspace_path / L"src");
    std::filesystem::create_directories(workspace_path / L"build");
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
    std::stop_source cancelled;
    cancelled.request_stop();
    Check(notepad_colon::SearchWorkspace(workspace_path, L"needle", {}, cancelled.get_token()).cancelled,
          "pre-cancelled folder search must report cancellation");
    const auto before_state = notepad_colon::CaptureFileState(workspace_path / L"src" / L"one.txt");
    Check(before_state.exists && before_state.size > 0, "file monitor state must capture a file");
    std::filesystem::remove_all(workspace_path, ignored);
    return failures == 0 ? 0 : 1;
}
