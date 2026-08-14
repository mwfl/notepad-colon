#include <notepad_colon/document.h>
#include <notepad_colon/session.h>
#include <notepad_colon/text.h>
#include <notepad_colon/language.h>
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
