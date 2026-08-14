#include <notepad_colon/document.h>
#include <notepad_colon/session.h>

#include <iostream>

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
    original.documents.push_back({L"C:\\work\\ä½ å¥½.cpp", L"", notepad_colon::Encoding::utf8,
                                  notepad_colon::LineEnding::lf, {4, 9, 2}, false});
    original.documents.push_back({{}, L"unsaved\ntext\tΩ", notepad_colon::Encoding::utf8,
                                  notepad_colon::LineEnding::crlf, {0, 7, 0}, true});
    const auto encoded = notepad_colon::SerializeSession(original);
    notepad_colon::Session decoded;
    Check(notepad_colon::DeserializeSession(encoded, decoded), "serialized session must parse");
    Check(decoded.documents.size() == 2 && decoded.active_index == 1, "session shape must round trip");
    if (decoded.documents.size() == 2) {
        Check(decoded.documents[0].path == original.documents[0].path, "Unicode path must round trip");
        Check(decoded.documents[1].recovery_text == original.documents[1].recovery_text,
              "recovery text must round trip");
    }
    Check(!notepad_colon::DeserializeSession("bad", decoded), "invalid sessions must be rejected");
    return failures == 0 ? 0 : 1;
}
