#include "main_window.h"

namespace notepad_colon::app {

namespace {
constexpr std::size_t kEditorWindowSize = 8u * 1024 * 1024;

struct LoadedEditorFile {
    notepad_colon::EncodedFileText contents;
    std::unique_ptr<notepad_colon::LargeFileBuffer> buffer;
    std::uint64_t window_offset = 0;
    std::uint64_t decoded_offset = 0;
    bool read_only = false;
};

mwfl::FileStamp EditorStamp(const notepad_colon::EncodedWriteExpectation& state) {
    return {state.size, static_cast<std::uint64_t>(state.last_write.time_since_epoch().count()),
            state.file_index, state.volume_serial};
}

std::optional<LoadedEditorFile> LoadEditorFile(const std::filesystem::path& path,
    unsigned int legacy_code_page, bool pinned, std::optional<notepad_colon::EncodingKind> fallback,
    std::uint64_t offset = 0, bool follow_tail = false, bool recovery_copy = false) {
    using namespace notepad_colon;
    const auto before = ReadEncodedFileState(path);
    if (!before || !before->exists) return std::nullopt;
    const auto mode = ClassifyFileSize(before->size);
    if (mode == FileOpenMode::unsupported && !recovery_copy) return std::nullopt;
    const auto forced = pinned && legacy_code_page ? std::optional{EncodingKind::ansi} : std::nullopt;
    LoadedEditorFile loaded;
    if (mode == FileOpenMode::editable) {
        auto contents = ReadEncodedFileText(path, LargeFilePolicy{}.editable_limit,
                                            legacy_code_page, forced, fallback, follow_tail);
        if (!contents || (follow_tail ? !IsSameFileOrAppend(*before, contents->state) : contents->state != *before))
            return std::nullopt;
        if (follow_tail && contents->analysis.encoding != EncodingKind::utf8 &&
            contents->analysis.encoding != EncodingKind::utf8_bom) return std::nullopt;
        loaded.contents = std::move(*contents);
        return loaded;
    }
    loaded.buffer = std::make_unique<LargeFileBuffer>();
    if (!loaded.buffer->Open(path, false, follow_tail)) return std::nullopt;
    if (follow_tail) {
        const auto captured = loaded.buffer->SourceState();
        const auto header = loaded.buffer->Read(0, 3);
        const bool bom = header.size() == 3 && header[0] == 0xef && header[1] == 0xbb && header[2] == 0xbf;
        const auto encoding = bom ? EncodingKind::utf8_bom : EncodingKind::utf8;
        const auto tail = captured.size > kEditorWindowSize ? captured.size - kEditorWindowSize : 0;
        const auto window = loaded.buffer->ReadTextWindow(tail, kEditorWindowSize, encoding);
        const auto current = ReadEncodedFileState(path);
        if (!window || window->text.find(L'\0') != std::wstring::npos || !current ||
            !IsSameFileOrAppend(captured, *current)) return std::nullopt;
        loaded.contents.text = window->text;
        loaded.contents.analysis = AnalyzeDecodedText(window->text, encoding, CP_UTF8, bom);
        loaded.contents.state = captured;
        loaded.window_offset = window->requested_offset;
        loaded.decoded_offset = window->decoded_offset;
        return loaded;
    }
    const auto sample = loaded.buffer->Read(0, kEditorWindowSize + 4);
    if (sample.empty()) return std::nullopt;
    const bool partial = before->size > sample.size();
    auto decoded = DecodeFileText(sample, legacy_code_page, forced, fallback);
    // A UTF-8 sample can end in the middle of a scalar. Prefer its valid
    // Unicode prefix over misclassifying the final partial scalar as ANSI.
    if (partial && !forced && (!decoded || decoded->analysis.encoding == EncodingKind::ansi)) {
        const bool bom = sample.size() >= 3 && sample[0] == 0xef && sample[1] == 0xbb && sample[2] == 0xbf;
        for (std::size_t trim = 1; trim <= 3 && trim < sample.size(); ++trim) {
            const auto lead = sample[sample.size() - trim];
            const auto width = lead >= 0xc2 && lead <= 0xdf ? 2u : lead >= 0xe0 && lead <= 0xef ? 3u :
                lead >= 0xf0 && lead <= 0xf4 ? 4u : 0u;
            if (width <= trim) continue;
            bool partial_scalar = true;
            for (std::size_t index = sample.size() - trim + 1; index < sample.size(); ++index)
                if ((sample[index] & 0xc0u) != 0x80u) partial_scalar = false;
            if (trim >= 2) {
                const auto second = sample[sample.size() - trim + 1];
                if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
                    (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second >= 0x90)) partial_scalar = false;
            }
            if (!partial_scalar) continue;
            auto unicode = DecodeFileText(std::span<const std::uint8_t>{sample}.first(sample.size() - trim),
                                           0, bom ? EncodingKind::utf8_bom : EncodingKind::utf8);
            if (unicode) { decoded = std::move(unicode); break; }
        }
    }
    for (std::size_t trim = 1; !decoded && partial && trim <= 4 && trim < sample.size(); ++trim)
        decoded = DecodeFileText(std::span<const std::uint8_t>{sample}.first(sample.size() - trim),
                                  legacy_code_page, forced, fallback);
    if (!decoded) return std::nullopt;
    const auto& analysis = decoded->analysis;
    loaded.read_only = analysis.encoding != EncodingKind::utf8 && analysis.encoding != EncodingKind::utf8_bom;
    if (follow_tail) offset = before->size > kEditorWindowSize ? before->size - kEditorWindowSize : 0;
    offset = (std::min)(offset, static_cast<std::uint64_t>(before->size - 1));
    const auto window = loaded.buffer->ReadTextWindow(offset, kEditorWindowSize, analysis.encoding, analysis.code_page);
    if (!window || window->text.find(L'\0') != std::wstring::npos || ReadEncodedFileState(path) != before)
        return std::nullopt;
    loaded.contents.text = window->text;
    loaded.contents.analysis = std::move(decoded->analysis);
    loaded.contents.state = *before;
    loaded.window_offset = window->requested_offset;
    loaded.decoded_offset = window->decoded_offset;
    return loaded;
}
}  // namespace

bool MainWindow::OpenPath(const std::filesystem::path& path, unsigned int ansi_code_page,
                           bool pin_legacy_encoding, bool recovery_copy,
                           std::optional<notepad_colon::EncodingKind> known_encoding) {
    if (ReplacementOwnsPath(path)) {
        status_.SetText(Tr(L"This file is part of a running replacement; wait for it to finish")); return false;
    }
    for (const auto& metadata : workspace_.GetDocuments()) {
        if (!metadata.path.empty() && SamePath(metadata.path, path)) {
            workspace_.Activate(metadata.id);
            SynchronizeTabs(true);
            SyncPresentation(Tr(L"Already open"));
            return true;
        }
    }
    const auto editor_config = notepad_colon::ResolveEditorConfig(path);
    const auto initial_state = notepad_colon::ReadEncodedFileState(path);
    if (initial_state && !recovery_copy &&
        notepad_colon::ClassifyFileSize(initial_state->size) == notepad_colon::FileOpenMode::unsupported) {
        status_.SetText(Tr(L"File exceeds the 4 GiB mapped-view limit"));
        return false;
    }
    auto loaded = LoadEditorFile(path, ansi_code_page, pin_legacy_encoding,
        known_encoding ? known_encoding : editor_config.encoding, 0, false, recovery_copy);
    if (!loaded) {
        status_.SetText(Tr(L"Could not read the file losslessly; check its encoding, size, and whether another program is writing it"));
        return false;
    }
    const bool protected_mode = loaded->buffer != nullptr;
    const bool large_editable = protected_mode && !loaded->read_only;
    auto& analysis = loaded->contents.analysis;
    const auto& loaded_text = loaded->contents.text;
    const auto loaded_encoding = ToMwflEncoding(analysis.encoding);
    const auto loaded_stamp = EditorStamp(loaded->contents.state);
    const mwfl::DocumentId id{next_id_++};
    auto editor = std::make_unique<mwfl::ScintillaEditor>();
    if (!editor->Create(GetHwnd(), mwfl::ControlId{static_cast<WORD>(200 + id.value)},
                        mwfl::RectDip{}, runtime_) ||
        !editor->ConfigureCodeEditing() || !notepad_colon::SetEditorTextBaseline(*editor, loaded_text)) return false;
    notepad_colon::ConfigureAdvancedEditing(*editor);
    notepad_colon::ApplyPreferences(*editor, preferences_, IsDark());
    if (editor_config.indent_size) {
        editor->Send(SCI_SETTABWIDTH, *editor_config.indent_size);
        editor->Send(SCI_SETINDENT, *editor_config.indent_size);
    }
    if (editor_config.use_tabs) editor->Send(SCI_SETUSETABS, *editor_config.use_tabs);
    const bool has_newlines = analysis.eol.crlf || analysis.eol.lf || analysis.eol.cr;
    const auto actual_ending = has_newlines ? notepad_colon::DetectLineEnding(loaded_text) :
        editor_config.line_ending.value_or(notepad_colon::LineEnding::crlf);
    editor->Send(SCI_SETEOLMODE, actual_ending == notepad_colon::LineEnding::lf ? SC_EOL_LF :
        actual_ending == notepad_colon::LineEnding::cr ? SC_EOL_CR : SC_EOL_CRLF);
    const auto* registered_language = language_registry_.Detect(path);
    const auto language = registered_language && registered_language->builtin
        ? *registered_language->builtin : notepad_colon::Language::plain_text;
    const bool configured = registered_language && !registered_language->builtin
        ? notepad_colon::ConfigureRegisteredLanguage(*editor, lexilla_, *registered_language, IsDark())
        : notepad_colon::ConfigureLanguage(*editor, lexilla_, language, IsDark(),
              protected_mode ? notepad_colon::SyntaxPerformanceMode::lightweight
                             : notepad_colon::SyntaxPerformanceMode::full);
    if (!configured) return false;
    if (protected_mode && !large_editable && !editor->SetReadOnly(true)) return false;
    editor->SetSavePoint();
    const auto title = path.filename().wstring();
    if (!workspace_.Add({id, title, path})) return false;
    if (adapter_.BindPage(id, editor->GetHwnd()) != mwfl::DocumentTabStatus::success) return false;
    mwfl::SetAccessibleName(editor->GetHwnd(), title.c_str());
    documents_.push_back({id, std::move(editor), loaded_encoding,
                          actual_ending, loaded_stamp,
                          protected_mode && !large_editable, language,
                          loaded->contents.state});
    documents_.back().detected_encoding = analysis.encoding;
    documents_.back().ansi_code_page = analysis.code_page;
    documents_.back().editor_config = editor_config;
    documents_.back().encoding_analysis = analysis;
    documents_.back().legacy_encoding_override = ansi_code_page != 0 && pin_legacy_encoding;
    documents_.back().unicode_bom = analysis.has_bom;
    documents_.back().large_buffer = std::move(loaded->buffer);
    documents_.back().mapped_offset = loaded->window_offset;
    documents_.back().mapped_decoded_offset = loaded->decoded_offset;
    if (registered_language) documents_.back().language_id = registered_language->id;
    InitializeSyntaxTree(documents_.back());
    workspace_.Activate(id);
    SynchronizeTabs(true);
    if (analysis.eol.Mixed() || !analysis.unicode_risks.empty()) {
        status_.SetText(Tr(L"Opened with text safety warnings; use Document Encoding Information"));
    } else SyncPresentation(protected_mode
        ? (large_editable ? Tr(L"Opened in large-file streaming edit mode")
                          : Tr(L"Opened in large-file read-only mode"))
        : Tr(L"Opened"));
    ApplyAutomaticLineTools(documents_.back(), path);
    RememberPath(path);
    static_cast<void>(SaveSessionSnapshot());
    return true;
}

void MainWindow::OpenInteractive() {
    const auto selected = mwfl::ShowOpenFileDialog({.owner = GetHwnd(), .title = Tr(L"Open"),
        .filters = {{Tr(L"Text and source files"), L"*.txt;*.md;*.cpp;*.h;*.json;*.xml;*.ini;*.yaml;*.yml;*.ps1"},
                    {Tr(L"All files"), L"*.*"}}});
    if (selected.accepted) static_cast<void>(OpenPath(selected.path));
}

bool MainWindow::SaveDocument(EditorDocument& document, bool choose_path) {
    if (document.read_only) {
        status_.SetText(Tr(L"Large-file protection is read-only"));
        return false;
    }
    const auto* metadata = workspace_.Find(document.id);
    if (!metadata) return false;
    auto path = metadata->path;
    if (choose_path || path.empty()) {
        if (IsTestMode()) {
            path = std::filesystem::temp_directory_path() /
                (L"notepad-colon-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
                 std::to_wstring(document.id.value) + L".txt");
        } else {
            const auto selected = mwfl::ShowSaveFileDialog({.owner = GetHwnd(), .title = Tr(L"Save"),
                .filters = {{Tr(L"Text files"), L"*.txt"}, {Tr(L"All files"), L"*.*"}},
                .default_extension = L"txt", .path_must_exist = false});
            if (!selected.accepted) return false;
            path = selected.path;
        }
    }
    if (ReplacementOwnsPath(path)) {
        status_.SetText(Tr(L"This file is part of a running replacement; wait for it to finish")); return false;
    }
    if (path == metadata->path && document.external_changed) {
        status_.SetText(Tr(L"The file changed on disk; review it or use Save As before overwriting"));
        return false;
    }
    if (document.large_buffer) return SaveLargeDocument(document, path, *metadata);
    auto text = document.editor->GetText();
    if (!text) return false;
    const auto trim_trailing = document.editor_config.trim_trailing_whitespace
        .value_or(preferences_.trim_trailing_whitespace_on_save);
    const auto final_newline = document.editor_config.insert_final_newline
        .value_or(preferences_.ensure_final_newline);
    auto prepared = trim_trailing
        ? notepad_colon::TrimTrailingWhitespace(*text) : *text;
    const auto requested_ending = document.line_ending_override ? document.line_ending_override : document.editor_config.line_ending;
    if (requested_ending)
        prepared = notepad_colon::NormalizeLineEndings(
            prepared, *requested_ending);
    if (final_newline) {
        const auto ending = requested_ending.value_or(notepad_colon::DetectLineEnding(prepared));
        const auto newline = ending == notepad_colon::LineEnding::crlf ? L"\r\n" :
                             ending == notepad_colon::LineEnding::cr ? L"\r" : L"\n";
        prepared = notepad_colon::EnsureFinalNewline(prepared, newline);
    }
    if (prepared != *text) {
        const auto selection = document.editor->GetSelection();
        if (!notepad_colon::ReplaceDocumentText(*document.editor, prepared, selection)) return false;
        text = std::move(prepared);
    }
    if (preferences_.create_backup_before_save && std::filesystem::exists(path)) {
        auto backup = path;
        backup += L".bak";
        if (!::CopyFileW(path.c_str(), backup.c_str(), FALSE)) {
            status_.SetText(Tr(L"Backup could not be created; save cancelled"));
            return false;
        }
    }
    const auto expected = path == metadata->path ? std::optional{document.file_state} : std::nullopt;
    notepad_colon::EncodedWriteExpectation written;
    if (!notepad_colon::WriteEncodedFileAtomic(path, *text, document.detected_encoding,
            document.ansi_code_page, expected, document.unicode_bom, &written)) {
        status_.SetText(Tr(L"Save failed: the text is not representable, the file changed, or the destination could not be written"));
        return false;
    }
    document.file_state = written;
    document.stamp = EditorStamp(written);
    document.external_changed = false;
    document.line_ending = notepad_colon::DetectLineEnding(*text);
    document.editor->Send(SCI_SETEOLMODE, document.line_ending == notepad_colon::LineEnding::lf ? SC_EOL_LF :
        document.line_ending == notepad_colon::LineEnding::cr ? SC_EOL_CR : SC_EOL_CRLF);
    document.editor->SetSavePoint();
    workspace_.Rename(document.id, path.filename().wstring(), path);
    const auto* registered_language = language_registry_.Detect(path);
    const auto language = registered_language && registered_language->builtin
        ? *registered_language->builtin : notepad_colon::Language::plain_text;
    const auto language_id = registered_language ? registered_language->id : std::string{"plain-text"};
    if (language_id != document.language_id) {
        document.language = language;
        document.language_id = language_id;
        if (registered_language && !registered_language->builtin)
            static_cast<void>(notepad_colon::ConfigureRegisteredLanguage(
                *document.editor, lexilla_, *registered_language, IsDark()));
        else static_cast<void>(notepad_colon::ConfigureLanguage(
                *document.editor, lexilla_, language, IsDark()));
        InitializeSyntaxTree(document);
    }
    workspace_.SetDirty(document.id, false);
    RememberPath(path);
    SynchronizeTabs(false);
    SyncPresentation(Tr(L"Saved"));
    static_cast<void>(SaveSessionSnapshot());
    return true;
}

// A path from the command line, Explorer or drag and drop may be a folder:
// folders become workspace roots, everything else opens as a document.
bool MainWindow::OpenPathOrFolder(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::is_directory(path, error) && !error) {
        StartWorkspaceScan(path);
        return true;
    }
    return OpenPath(path);
}

void MainWindow::ReportShellMenus(bool install) {
    const notepad_colon::ShellMenuSpec spec{
        ExecutablePath(), Tr(L"Edit with Notepad::"), Tr(L"Open folder in Notepad::")};
    const auto status = install
        ? notepad_colon::RegisterShellMenus(HKEY_CURRENT_USER, notepad_colon::kUserClassesKey, spec)
        : notepad_colon::RemoveShellMenus(HKEY_CURRENT_USER, notepad_colon::kUserClassesKey);
    if (status == notepad_colon::ShellMenuStatus::success) {
        // Explorer caches verbs; tell it the association data changed.
        ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        status_.SetText(install ? Tr(L"Explorer context menu added for this user")
                                : Tr(L"Explorer context menu removed"));
    } else if (status == notepad_colon::ShellMenuStatus::conflict) {
        status_.SetText(Tr(L"Context menu unchanged: another program owns that entry"));
    } else {
        status_.SetText(Tr(L"Context menu could not be changed"));
    }
}

void MainWindow::ReportAssociation(bool install) {
    const auto result = install ? mwfl::RegisterPerUserFileAssociation(TextAssociation())
                                : mwfl::RemovePerUserFileAssociation(TextAssociation());
    status_.SetText(result ? (install ? Tr(L".txt association registered for this user")
                                      : Tr(L"Owned .txt association removed"))
                           : Tr(L"Association unchanged; Windows ownership/conflict protected"));
}

void MainWindow::UpdateLargeBuffer(EditorDocument& document, const SCNotification& notification) noexcept {
    if (!document.large_buffer || document.loading_large_window ||
        (document.detected_encoding != notepad_colon::EncodingKind::utf8 &&
         document.detected_encoding != notepad_colon::EncodingKind::utf8_bom) ||
        (notification.modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) == 0)
        return;
    const auto position = static_cast<std::uint64_t>((std::max<Sci_Position>)(
        0, notification.position));
    const auto length = static_cast<std::size_t>((std::max<Sci_Position>)(
        0, notification.length));
    const auto logical = document.mapped_decoded_offset + position;
    bool updated = false;
    if ((notification.modificationType & SC_MOD_INSERTTEXT) != 0 && notification.text)
        updated = document.large_buffer->Insert(logical,
            {reinterpret_cast<const std::uint8_t*>(notification.text), length});
    else if ((notification.modificationType & SC_MOD_DELETETEXT) != 0)
        updated = document.large_buffer->Erase(logical, length);
    if (!updated) {
        static_cast<void>(document.editor->SetReadOnly(true));
        document.read_only = true;
        status_.SetText(Tr(L"Large-file edit could not be recorded; switched to read-only protection"));
    } else workspace_.SetDirty(document.id, true);
}

bool MainWindow::SaveActive(bool choose_path) {
    auto* document = ActiveDocument();
    return document && SaveDocument(*document, choose_path);
}

bool MainWindow::SaveAll() {
    for (auto& document : documents_) {
        const auto* metadata = workspace_.Find(document.id);
        if (metadata && metadata->dirty && !SaveDocument(document, false)) return false;
    }
    return true;
}

bool MainWindow::SaveLargeDocument(EditorDocument& document, const std::filesystem::path& path,
                       const mwfl::WorkspaceDocument& metadata) {
    if (!document.large_buffer ||
        (document.detected_encoding != notepad_colon::EncodingKind::utf8 &&
         document.detected_encoding != notepad_colon::EncodingKind::utf8_bom))
        return false;
    if (path == metadata.path && !notepad_colon::MatchesEncodedFileState(
            document.file_state, notepad_colon::CaptureFileState(path))) {
        status_.SetText(Tr(L"Large-file save cancelled because the disk file changed"));
        return false;
    }
    if (preferences_.create_backup_before_save && std::filesystem::exists(path)) {
        auto backup = path;
        backup += L".bak";
        if (!::CopyFileW(path.c_str(), backup.c_str(), FALSE)) {
            status_.SetText(Tr(L"Large-file backup could not be created; save cancelled"));
            return false;
        }
    }
    if (!document.large_buffer->SaveAs(path)) {
        const auto error = ::GetLastError();
        status_.SetText(Tr(L"Large-file save could not be completed; the editor edits were retained") +
            L" (" + std::to_wstring(error) + L")");
        return false;
    }
    document.file_state = document.large_buffer->SourceState();
    if (notepad_colon::ReadEncodedFileState(path) != std::optional{document.file_state}) {
        document.external_changed = true;
        status_.SetText(Tr(L"The file changed on disk; review it or use Save As before overwriting"));
        return false;
    }
    document.stamp = EditorStamp(document.file_state);
    document.external_changed = false;
    document.recovery_snapshot.reset();
    document.recovery_revision = UINT64_MAX;
    document.editor->SetSavePoint();
    workspace_.Rename(document.id, path.filename().wstring(), path);
    workspace_.SetDirty(document.id, false);
    RememberPath(path);
    SynchronizeTabs(false);
    SyncPresentation(Tr(L"Large file saved by streaming modified pieces"));
    static_cast<void>(SaveSessionSnapshot());
    return true;
}

void MainWindow::AutoSaveIfDue() {
    if (!preferences_.auto_save || IsTestMode()) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - last_auto_save_ < std::chrono::seconds{preferences_.auto_save_seconds}) return;
    last_auto_save_ = now;
    std::size_t saved = 0;
    for (auto& document : documents_) {
        const auto* metadata = workspace_.Find(document.id);
        if (!metadata || metadata->path.empty() || !metadata->dirty || document.read_only ||
            document.external_changed) continue;
        saved += SaveDocument(document, false) ? 1u : 0u;
    }
    if (saved) status_.SetText(Tr(L"Auto-saved ") + std::to_wstring(saved) + Tr(L" document(s)"));
}

bool MainWindow::CloseActive(bool discard) {
    const auto id = workspace_.GetActiveId();
    if (!id) return false;
    const auto* metadata = workspace_.Find(*id);
    if (metadata && metadata->dirty && !discard && !IsTestMode()) {
        const int answer = ::MessageBoxW(GetHwnd(), Tr(L"Save changes before closing?").c_str(),
                                          Tr(L"Notepad::").c_str(), MB_ICONWARNING | MB_YESNOCANCEL);
        if (answer == IDCANCEL || (answer == IDYES && !SaveActive(false))) return false;
    }
    auto found = std::ranges::find(documents_, *id, &EditorDocument::id);
    if (found == documents_.end()) return false;
    const HWND hwnd = found->editor->GetHwnd();
    if (!workspace_.Close(*id) || adapter_.UnbindPage(*id) != mwfl::DocumentTabStatus::success)
        return false;
    CloseSplitView(*found);
    std::erase(pinned_documents_, *id);
    documents_.erase(found);
    if (::IsWindow(hwnd)) ::DestroyWindow(hwnd);
    if (documents_.empty()) NewDocument();
    else {
        SynchronizeTabs(true);
        SyncPresentation(Tr(L"Closed"));
        static_cast<void>(SaveSessionSnapshot());
    }
    return true;
}

void MainWindow::TogglePinActive() {
    const auto id = workspace_.GetActiveId();
    if (!id) return;
    const auto found = std::ranges::find(pinned_documents_, *id);
    if (found == pinned_documents_.end()) {
        pinned_documents_.push_back(*id);
        static_cast<void>(workspace_.Move(*id, 0));
        status_.SetText(Tr(L"Tab pinned"));
    } else {
        pinned_documents_.erase(found);
        status_.SetText(Tr(L"Tab unpinned"));
    }
    SynchronizeTabs(true);
}

void MainWindow::SortTabs() {
    std::vector<mwfl::WorkspaceDocument> ordered(workspace_.GetDocuments().begin(),
                                                 workspace_.GetDocuments().end());
    std::ranges::stable_sort(ordered, [&](const auto& left, const auto& right) {
        const bool left_pinned = IsPinned(left.id), right_pinned = IsPinned(right.id);
        if (left_pinned != right_pinned) return left_pinned;
        return ::CompareStringOrdinal(left.title.c_str(), -1, right.title.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    for (std::size_t index = 0; index < ordered.size(); ++index)
        static_cast<void>(workspace_.Move(ordered[index].id, index));
    SynchronizeTabs(true); status_.SetText(Tr(L"Tabs sorted (pinned tabs first)"));
}

void MainWindow::CloseRelativeTabs(int direction) {
    const auto active = workspace_.GetActiveId();
    if (!active) return;
    const auto active_index = workspace_.FindIndex(*active);
    if (!active_index) return;
    std::vector<mwfl::DocumentId> close;
    const auto documents = workspace_.GetDocuments();
    for (std::size_t index = 0; index < documents.size(); ++index) {
        if (documents[index].id == *active || IsPinned(documents[index].id)) continue;
        if (direction == 0 || (direction < 0 && index < *active_index) ||
            (direction > 0 && index > *active_index)) close.push_back(documents[index].id);
    }
    for (const auto id : close) {
        static_cast<void>(workspace_.Activate(id)); SynchronizeTabs(true);
        if (!CloseActive()) break;
    }
    if (workspace_.Find(*active)) { static_cast<void>(workspace_.Activate(*active)); SynchronizeTabs(true); }
}

void MainWindow::OpenActiveInNewWindow() {
    const auto* metadata = workspace_.GetActiveId() ? workspace_.Find(*workspace_.GetActiveId()) : nullptr;
    if (!metadata || metadata->path.empty()) {
        status_.SetText(Tr(L"Save the document before opening it in another window")); return;
    }
    auto command_line = L"\"" + ExecutablePath().wstring() + L"\" \"" + metadata->path.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (::CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                         &startup, &process)) {
        ::CloseHandle(process.hThread); ::CloseHandle(process.hProcess);
        status_.SetText(Tr(L"Document sent to a new window"));
    } else status_.SetText(Tr(L"Could not open a new window"));
}

bool MainWindow::LoadLargeFileWindow(EditorDocument& document, std::uint64_t offset,
                         std::optional<std::pair<std::uint64_t, std::size_t>> selection) {
    const auto window = document.large_buffer->ReadTextWindow(
        offset, document.mapped_window_size, document.detected_encoding,
        document.ansi_code_page);
    if (!window || window->text.find(L'\0') != std::wstring::npos) return false;
    document.loading_large_window = true;
    document.editor->SetReadOnly(false);
    const bool replaced = notepad_colon::SetEditorTextBaseline(*document.editor, window->text);
    document.editor->SetReadOnly(document.read_only);
    document.loading_large_window = false;
    if (!replaced) return false;
    document.mapped_offset = offset;
    document.mapped_decoded_offset = window->decoded_offset;
    if (!document.large_buffer->IsModified()) document.editor->SetSavePoint();
    workspace_.SetDirty(document.id, document.large_buffer->IsModified());
    workspace_.SetUndoState(document.id, document.editor->CanUndo(), document.editor->CanRedo());
    if (selection && selection->first >= window->decoded_offset) {
        const auto relative = selection->first - window->decoded_offset;
        if (relative + selection->second <= static_cast<std::uint64_t>(document.editor->GetLength()))
            document.editor->SetSelection({static_cast<mwfl::ScintillaPosition>(relative),
                static_cast<mwfl::ScintillaPosition>(relative + selection->second)});
    }
    return true;
}

void MainWindow::MoveLargeFileWindow(bool forward) {
    auto* document = ActiveDocument();
    if (!document || !document->large_buffer) {
        status_.SetText(Tr(L"The active document is not using mapped large-file mode")); return;
    }
    const auto size = document->large_buffer->Size();
    const auto step = static_cast<std::uint64_t>(document->mapped_window_size);
    const auto offset = forward
        ? (std::min)(size, document->mapped_offset + step)
        : (document->mapped_offset > step ? document->mapped_offset - step : 0);
    if (offset == document->mapped_offset || offset >= size) {
        status_.SetText(forward ? Tr(L"End of large file") : Tr(L"Start of large file")); return;
    }
    const auto window = document->large_buffer->ReadTextWindow(
        offset, document->mapped_window_size, document->detected_encoding,
        document->ansi_code_page);
    if (!window || window->text.find(L'\0') != std::wstring::npos) {
        status_.SetText(Tr(L"Window boundary contains invalid text")); return;
    }
    document->loading_large_window = true;
    document->editor->SetReadOnly(false);
    const bool replaced = notepad_colon::SetEditorTextBaseline(*document->editor, window->text);
    document->editor->SetReadOnly(document->read_only);
    document->loading_large_window = false;
    if (!replaced) return;
    document->mapped_offset = offset;
    document->mapped_decoded_offset = window->decoded_offset;
    if (!document->large_buffer->IsModified()) document->editor->SetSavePoint();
    workspace_.SetDirty(document->id, document->large_buffer->IsModified());
    workspace_.SetUndoState(document->id, document->editor->CanUndo(), document->editor->CanRedo());
    status_.SetText(Tr(L"Large file | bytes ") + std::to_wstring(document->mapped_offset) + L"–" +
        std::to_wstring(window->byte_end) + Tr(L" of ") +
        std::to_wstring(size));
}

void MainWindow::ShowEncodingInformation() {
    const auto* document = ActiveDocument();
    if (!document) return;
    std::wstring name = L"UTF-8";
    switch (document->detected_encoding) {
    case notepad_colon::EncodingKind::utf8_bom: name = L"UTF-8 with BOM"; break;
    case notepad_colon::EncodingKind::utf16_le: name = L"UTF-16 LE"; break;
    case notepad_colon::EncodingKind::utf16_be: name = L"UTF-16 BE"; break;
    case notepad_colon::EncodingKind::ansi:
        name = notepad_colon::CodePageName(document->ansi_code_page) + L" (code page " +
               std::to_wstring(document->ansi_code_page) + L")";
        break;
    case notepad_colon::EncodingKind::binary: name = L"Binary"; break;
    case notepad_colon::EncodingKind::utf8: break;
    }
    const auto& analysis = document->encoding_analysis;
    const auto message = Tr(L"Encoding: ") + name +
        Tr(L"\nInvalid byte sequences: ") + std::to_wstring(analysis.invalid_byte_count) +
        Tr(L"\nLine endings: ") + std::to_wstring(analysis.eol.crlf) + Tr(L" CRLF, ") +
        std::to_wstring(analysis.eol.lf) + Tr(L" LF, ") + std::to_wstring(analysis.eol.cr) + Tr(L" CR") +
        Tr(L"\nMixed line endings: ") + (analysis.eol.Mixed() ? std::wstring(Tr(L"Yes")) : std::wstring(Tr(L"No"))) +
        Tr(L"\nBidi / zero-width controls: ") + std::to_wstring(analysis.unicode_risk_count);
    ::MessageBoxW(GetHwnd(), message.c_str(), Tr(L"Document Encoding Information").c_str(), MB_OK |
        ((!analysis.invalid_byte_offsets.empty() || analysis.eol.Mixed() || !analysis.unicode_risks.empty())
            ? MB_ICONWARNING : MB_ICONINFORMATION));
}

bool MainWindow::ReloadDocumentFromDisk(EditorDocument& document, bool follow_tail,
                                         std::optional<unsigned int> force_code_page) {
    const auto* metadata = workspace_.Find(document.id);
    if (!metadata || metadata->path.empty()) return false;
    const auto hint = force_code_page.value_or(
        document.detected_encoding == notepad_colon::EncodingKind::ansi ? document.ansi_code_page : 0);
    const bool pinned = force_code_page ? *force_code_page != 0 : document.legacy_encoding_override;
    const auto configuration = notepad_colon::ResolveEditorConfig(metadata->path);
    auto loaded = LoadEditorFile(metadata->path, hint, pinned, configuration.encoding,
                                  document.mapped_offset, follow_tail);
    if (!loaded) {
        status_.SetText(Tr(L"Reload failed; the current text was retained. Check the file's encoding or whether another program is writing it"));
        return false;
    }
    std::optional<std::wstring> before;
    if (!follow_tail && !force_code_page && !document.large_buffer && document.editor->GetLength() <= 8 * 1024 * 1024)
        before = document.editor->GetText();
    const auto selection = document.editor->GetSelection();
    const auto first_line = document.editor->Send(SCI_GETFIRSTVISIBLELINE);
    document.loading_large_window = true;
    document.editor->SetReadOnly(false);
    const bool replaced = notepad_colon::SetEditorTextBaseline(*document.editor, loaded->contents.text);
    if (!replaced) {
        document.editor->SetReadOnly(document.read_only);
        document.loading_large_window = false;
        return false;
    }
    document.large_buffer = std::move(loaded->buffer);
    document.mapped_offset = loaded->window_offset;
    document.mapped_decoded_offset = loaded->decoded_offset;
    document.encoding_analysis = std::move(loaded->contents.analysis);
    document.detected_encoding = document.encoding_analysis.encoding;
    document.encoding = ToMwflEncoding(document.detected_encoding);
    document.ansi_code_page = document.encoding_analysis.code_page;
    document.unicode_bom = document.encoding_analysis.has_bom;
    document.legacy_encoding_override = pinned;
    document.editor_config = configuration;
    const auto& eol = document.encoding_analysis.eol;
    document.line_ending = eol.crlf || eol.lf || eol.cr ? notepad_colon::DetectLineEnding(loaded->contents.text) :
        configuration.line_ending.value_or(notepad_colon::LineEnding::crlf);
    document.file_state = loaded->contents.state;
    document.stamp = EditorStamp(document.file_state);
    document.read_only = loaded->read_only;
    document.external_changed = false;
    document.follow_tail = follow_tail && !document.read_only &&
        (document.detected_encoding == notepad_colon::EncodingKind::utf8 || document.detected_encoding == notepad_colon::EncodingKind::utf8_bom);
    document.recovery_snapshot.reset();
    document.recovery_revision = UINT64_MAX;
    document.editor->SetReadOnly(document.read_only);
    document.loading_large_window = false;
    ++document.revision;
    document.editor->Send(SCI_SETEOLMODE, document.line_ending == notepad_colon::LineEnding::lf ? SC_EOL_LF :
        document.line_ending == notepad_colon::LineEnding::cr ? SC_EOL_CR : SC_EOL_CRLF);
    document.editor->SetSavePoint();
    workspace_.SetDirty(document.id, false);
    workspace_.SetUndoState(document.id, document.editor->CanUndo(), document.editor->CanRedo());
    if (document.follow_tail) document.editor->Send(SCI_GOTOPOS, document.editor->GetLength());
    else {
        document.editor->SetSelection(selection);
        document.editor->Send(SCI_SETFIRSTVISIBLELINE, first_line);
    }
    if (const auto* language = language_registry_.Find(document.language_id); language && !language->builtin)
        static_cast<void>(notepad_colon::ConfigureRegisteredLanguage(*document.editor, lexilla_, *language, IsDark()));
    else static_cast<void>(notepad_colon::ConfigureLanguage(*document.editor, lexilla_, document.language, IsDark(),
        (document.large_buffer || document.follow_tail) ? notepad_colon::SyntaxPerformanceMode::lightweight : notepad_colon::SyntaxPerformanceMode::full));
    InitializeSyntaxTree(document);
    if (force_code_page || document.large_buffer || !before) ClearReview(document);
    const auto changes = before ? NoteExternalReload(document, std::move(*before)) : 0;
    SyncPresentation(changes == 0 ? Tr(L"Reloaded external change")
        : metadata->title + Tr(L" changed on disk: ") + std::to_wstring(changes) + Tr(L" change(s) to review (F8)"));
    static_cast<void>(SaveSessionSnapshot());
    return true;
}

void MainWindow::ReopenActiveWithCodePage(unsigned int code_page) {
    auto* document = ActiveDocument();
    const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
    if (!document || !metadata || metadata->path.empty()) return;
    if (metadata->dirty && !IsTestMode() && ::MessageBoxW(GetHwnd(),
        Tr(L"Reopening discards unsaved edits. Continue?").c_str(), Tr(L"Reopen with Encoding").c_str(),
        MB_YESNO | MB_ICONWARNING) != IDYES) return;
    if (ReloadDocumentFromDisk(*document, false, code_page))
        SyncPresentation(code_page ? Tr(L"Reopened as ") + notepad_colon::CodePageName(code_page)
                                  : Tr(L"Reopened with automatic encoding detection"));
}

void MainWindow::SetAnsiEncoding(unsigned int code_page) {
    auto* document = ActiveDocument();
    if (!document) return;
    if (document->large_buffer || document->read_only) {
        status_.SetText(Tr(L"Whole-file encoding and line-ending conversion is unavailable for large or read-only files")); return;
    }
    const auto text = document->editor->GetText();
    if (!text || !notepad_colon::EncodeText(*text, notepad_colon::EncodingKind::ansi, code_page)) {
        if (!IsTestMode())
            ::MessageBoxW(GetHwnd(), Tr(L"Some characters cannot be represented by this code page.").c_str(),
                          Tr(L"Encoding Conversion").c_str(), MB_OK | MB_ICONWARNING);
        status_.SetText(Tr(L"Conversion cancelled: some characters are not representable"));
        return;
    }
    document->detected_encoding = notepad_colon::EncodingKind::ansi;
    document->ansi_code_page = code_page;
    document->encoding = mwfl::TextEncoding::utf8;
    document->legacy_encoding_override = true;
    workspace_.SetDirty(document->id, true);
    SyncPresentation(Tr(L"Will save as ") + notepad_colon::CodePageName(code_page));
    static_cast<void>(SaveSessionSnapshot());
}

void MainWindow::SetEncoding(mwfl::TextEncoding encoding) {
    auto* document = ActiveDocument();
    if (document && (document->large_buffer || document->read_only)) {
        status_.SetText(Tr(L"Whole-file encoding and line-ending conversion is unavailable for large or read-only files")); return;
    }
    if (!document || (document->encoding == encoding &&
        document->detected_encoding != notepad_colon::EncodingKind::ansi)) return;
    document->encoding = encoding;
    document->legacy_encoding_override = false;
    document->unicode_bom = true;
    switch (encoding) {
    case mwfl::TextEncoding::utf8: document->detected_encoding = notepad_colon::EncodingKind::utf8; break;
    case mwfl::TextEncoding::utf8_bom: document->detected_encoding = notepad_colon::EncodingKind::utf8_bom; break;
    case mwfl::TextEncoding::utf16_le: document->detected_encoding = notepad_colon::EncodingKind::utf16_le; break;
    case mwfl::TextEncoding::utf16_be: document->detected_encoding = notepad_colon::EncodingKind::utf16_be; break;
    }
    workspace_.SetDirty(document->id, true);
    SyncPresentation(Tr(L"Encoding changed"));
    static_cast<void>(SaveSessionSnapshot());
}

void MainWindow::SetLineEnding(notepad_colon::LineEnding ending) {
    auto* document = ActiveDocument();
    if (!document) return;
    if (document->large_buffer || document->read_only) {
        status_.SetText(Tr(L"Whole-file encoding and line-ending conversion is unavailable for large or read-only files")); return;
    }
    const auto text = document->editor->GetText();
    if (!text) return;
    const auto normalized = notepad_colon::NormalizeLineEndings(*text, ending);
    const auto selection = document->editor->GetSelection();
    if (!notepad_colon::ReplaceDocumentText(*document->editor, normalized, selection)) return;
    document->line_ending = ending;
    document->line_ending_override = ending;
    document->editor->Send(SCI_SETEOLMODE, ending == notepad_colon::LineEnding::lf ? SC_EOL_LF :
        ending == notepad_colon::LineEnding::cr ? SC_EOL_CR : SC_EOL_CRLF);
    workspace_.SetDirty(document->id, true);
    SyncPresentation(Tr(L"Line endings converted"));
    static_cast<void>(SaveSessionSnapshot());
}

void MainWindow::RefreshRecentCommands() {
    const auto paths = recent_.GetPaths();
    for (std::size_t index = 0; index < recent_.GetMaximumEntries(); ++index) {
        auto* command = commands_.Find({static_cast<WORD>(kRecentBase.value + index)});
        if (!command) continue;
        command->SetEnabled(index < paths.size()).SetVisible(index < paths.size());
        command->SetText(index < paths.size()
            ? L"&" + std::to_wstring(index + 1) + L" " + paths[index].wstring()
            : Tr(L"Recent file"));
    }
}

void MainWindow::RememberPath(const std::filesystem::path& path) {
    if (path.empty() || !recent_.Add(path)) return;
    RefreshRecentCommands();
    if (menu_.GetHandle()) BuildMenu();
    if (!IsTestMode())
        static_cast<void>(mwfl::SaveRecentFilesToRegistry(HKEY_CURRENT_USER, kSettingsKey, recent_));
}

void MainWindow::CheckExternalChanges(bool force) {
    if (IsTestMode() && !force) return;
    for (auto& document : documents_) {
        const auto* metadata = workspace_.Find(document.id);
        if (!metadata || metadata->path.empty()) continue;
        const auto observed = notepad_colon::ReadEncodedFileState(metadata->path);
        if (!observed) {
            document.external_changed = true;
            status_.SetText(Tr(L"The file could not be inspected; its text was retained and overwriting is blocked"));
            continue;
        }
        const auto current = *observed;
        if (notepad_colon::MatchesEncodedFileState(document.file_state, current) && !document.external_changed) continue;
        if (document.external_changed && (metadata->dirty || !current.exists)) continue;
        if (!current.exists && !IsTestMode()) {
            const auto answer = ::MessageBoxW(GetHwnd(),
                (metadata->title + Tr(L" was deleted outside Notepad::.") + L"\n\n" +
                 Tr(L"Choose Yes to keep its contents as an unsaved document, or No to keep "
                    L"the original path blocked from overwrite.")).c_str(),
                Tr(L"File deleted on disk").c_str(), MB_YESNO | MB_ICONWARNING);
            if (answer == IDYES) {
                workspace_.Rename(document.id, metadata->title, {});
                workspace_.SetDirty(document.id, true);
                document.stamp.reset();
                document.file_state = current;
                document.external_changed = false;
                document.follow_tail = false;
                SynchronizeTabs(false);
                SyncPresentation(Tr(L"Kept deleted file as an unsaved document"));
                static_cast<void>(SaveSessionSnapshot());
            } else {
                document.external_changed = true;
                status_.SetText(metadata->title + Tr(L" is missing on disk; use Save As to preserve it"));
            }
            continue;
        }
        if (!metadata->dirty && current.exists) {
            const bool following = document.follow_tail;
            if (!ReloadDocumentFromDisk(document, following)) document.external_changed = true;
            else if (document.follow_tail) status_.SetText(Tr(L"Follow mode loaded appended data"));
        } else if (!IsTestMode() && current.exists) {
            document.external_changed = true;
            const auto answer = ::MessageBoxW(GetHwnd(),
                (metadata->title + Tr(L" changed on disk while it has unsaved edits.") + L"\n\n" +
                 Tr(L"Yes: compare with disk") + L"\n" + Tr(L"No: reload and discard local edits") +
                 L"\n" + Tr(L"Cancel: keep local edits")).c_str(),
                Tr(L"External edit conflict").c_str(), MB_YESNOCANCEL | MB_ICONWARNING);
            if (answer == IDYES) {
                const auto local = document.editor->GetText();
                const auto disk = LoadEditorFile(metadata->path,
                    document.detected_encoding == notepad_colon::EncodingKind::ansi ? document.ansi_code_page : 0,
                    document.legacy_encoding_override, document.editor_config.encoding, document.mapped_offset);
                if (local && disk && !disk->buffer && !document.large_buffer)
                    ShowComparison(Tr(L"Disk — ") + metadata->title, *local, disk->contents.text);
                else status_.SetText(Tr(L"A complete comparison is unavailable for this file; local edits were retained. Use Save As"));
            } else if (answer == IDNO) {
                if (!ReloadDocumentFromDisk(document)) document.external_changed = true;
            } else status_.SetText(metadata->title + Tr(L" conflict retained; use Compare or Save As"));
        } else {
            document.external_changed = true;
            status_.SetText(metadata->title + Tr(L" changed on disk; Save As or reload before overwriting"));
        }
    }
}

}  // namespace notepad_colon::app
