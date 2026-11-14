#include "main_window.h"

namespace notepad_colon::app {

bool MainWindow::OpenPath(const std::filesystem::path& path) {
    for (const auto& metadata : workspace_.GetDocuments()) {
        if (!metadata.path.empty() && SamePath(metadata.path, path)) {
            workspace_.Activate(metadata.id);
            SynchronizeTabs(true);
            SyncPresentation(Tr(L"Already open"));
            return true;
        }
    }
    std::error_code size_error;
    const auto size = std::filesystem::file_size(path, size_error);
    if (size_error) return false;
    const auto open_mode = notepad_colon::ClassifyFileSize(size);
    if (open_mode == notepad_colon::FileOpenMode::unsupported) {
        status_.SetText(Tr(L"File exceeds the 4 GiB mapped-view limit"));
        return false;
    }
    const bool protected_mode = open_mode == notepad_colon::FileOpenMode::protected_read_only;
    const auto stamp_before_read = protected_mode ? std::nullopt : QueryFileStamp(path);
    if (!protected_mode && !stamp_before_read) return false;
    std::unique_ptr<notepad_colon::MappedFile> mapped;
    std::unique_ptr<notepad_colon::LargeFileBuffer> large_buffer;
    std::optional<std::vector<std::uint8_t>> bytes;
    if (protected_mode) {
        mapped = std::make_unique<notepad_colon::MappedFile>();
        if (!mapped->Open(path)) return false;
        large_buffer = std::make_unique<notepad_colon::LargeFileBuffer>();
        if (!large_buffer->Open(path)) return false;
        bytes = mapped->Read(0, 8u * 1024 * 1024);
    } else bytes = ReadFileBytes(path);
    if (!bytes) return false;
    const auto editor_config = notepad_colon::ResolveEditorConfig(path);
    auto analysis = notepad_colon::AnalyzeEncoding(*bytes);
    if (editor_config.encoding) analysis.encoding = *editor_config.encoding;
    if (analysis.encoding == notepad_colon::EncodingKind::binary) {
        status_.SetText(Tr(L"Binary content detected; use a hex editor")); return false;
    }
    const bool large_editable = protected_mode &&
        (analysis.encoding == notepad_colon::EncodingKind::utf8 ||
         analysis.encoding == notepad_colon::EncodingKind::utf8_bom);
    std::wstring loaded_text;
    mwfl::TextEncoding loaded_encoding = ToMwflEncoding(analysis.encoding);
    std::optional<mwfl::FileStamp> loaded_stamp;
    if (!protected_mode) {
        loaded_stamp = QueryFileStamp(path);
        if (!loaded_stamp || loaded_stamp != stamp_before_read) {
            status_.SetText(Tr(L"File changed while it was being opened; try again"));
            return false;
        }
        const auto decoded = notepad_colon::DecodeBytes(*bytes, analysis.encoding, analysis.code_page);
        if (!decoded) return false;
        loaded_text = *decoded;
    } else {
        std::optional<std::wstring> decoded;
        for (std::size_t trim = 0; trim <= 3 && trim <= bytes->size(); ++trim) {
            decoded = notepad_colon::DecodeBytes(
                std::span<const std::uint8_t>{*bytes}.first(bytes->size() - trim),
                analysis.encoding, analysis.code_page);
            if (decoded) break;
        }
        if (!decoded) return false;
        loaded_text = *decoded;
    }
    const mwfl::DocumentId id{next_id_++};
    auto editor = std::make_unique<mwfl::ScintillaEditor>();
    if (!editor->Create(GetHwnd(), mwfl::ControlId{static_cast<WORD>(200 + id.value)},
                        mwfl::RectDip{}, runtime_) ||
        !editor->ConfigureCodeEditing() || !editor->SetText(loaded_text)) return false;
    notepad_colon::ConfigureAdvancedEditing(*editor);
    notepad_colon::ApplyPreferences(*editor, preferences_, IsDark());
    if (editor_config.indent_size) {
        editor->Send(SCI_SETTABWIDTH, *editor_config.indent_size);
        editor->Send(SCI_SETINDENT, *editor_config.indent_size);
    }
    if (editor_config.use_tabs) editor->Send(SCI_SETUSETABS, *editor_config.use_tabs);
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
                          notepad_colon::DetectLineEnding(loaded_text), loaded_stamp,
                          protected_mode && !large_editable, language,
                          notepad_colon::CaptureFileState(path)});
    documents_.back().detected_encoding = analysis.encoding;
    documents_.back().ansi_code_page = analysis.code_page;
    documents_.back().editor_config = editor_config;
    if (editor_config.line_ending) documents_.back().line_ending = *editor_config.line_ending;
    if (editor_config.encoding) {
        documents_.back().detected_encoding = *editor_config.encoding;
        documents_.back().encoding = ToMwflEncoding(*editor_config.encoding);
    }
    documents_.back().encoding_analysis = analysis;
    documents_.back().mapped_file = std::move(mapped);
    documents_.back().large_buffer = std::move(large_buffer);
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
    if (selected.accepted && !OpenPath(selected.path)) status_.SetText(Tr(L"Open failed"));
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
    if (document.large_buffer) return SaveLargeDocument(document, path, *metadata);
    auto text = document.editor->GetText();
    if (!text) return false;
    const auto trim_trailing = document.editor_config.trim_trailing_whitespace
        .value_or(preferences_.trim_trailing_whitespace_on_save);
    const auto final_newline = document.editor_config.insert_final_newline
        .value_or(preferences_.ensure_final_newline);
    auto prepared = trim_trailing
        ? notepad_colon::TrimTrailingWhitespace(*text) : *text;
    if (document.editor_config.line_ending)
        prepared = notepad_colon::NormalizeLineEndings(
            prepared, *document.editor_config.line_ending);
    if (final_newline) {
        const auto newline = document.line_ending == notepad_colon::LineEnding::crlf ? L"\r\n" :
                             document.line_ending == notepad_colon::LineEnding::cr ? L"\r" : L"\n";
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
    const auto expected = path == metadata->path ? document.stamp : std::nullopt;
    if (document.detected_encoding == notepad_colon::EncodingKind::ansi) {
        std::optional<notepad_colon::EncodedWriteExpectation> ansi_expected;
        if (path == metadata->path) ansi_expected = notepad_colon::EncodedWriteExpectation{
            document.file_state.size, document.file_state.last_write, document.file_state.exists};
        if (!notepad_colon::WriteEncodedFileAtomic(path, *text, document.detected_encoding,
                                                    document.ansi_code_page, ansi_expected)) {
            status_.SetText(Tr(L"ANSI save cancelled: text is not representable or the disk file changed"));
            return false;
        }
        document.stamp.reset();
    } else {
        const auto saved = mwfl::WriteTextFileAtomic(path, *text, document.encoding, expected);
        if (!saved.Succeeded() || !saved.stamp) return false;
        document.stamp = saved.stamp;
    }
    document.file_state = notepad_colon::CaptureFileState(path);
    document.external_changed = false;
    document.line_ending = notepad_colon::DetectLineEnding(*text);
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
    if (path == metadata.path && notepad_colon::CaptureFileState(path) != document.file_state) {
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
    if (document.mapped_file) document.mapped_file->Close();
    if (!document.large_buffer->SaveAs(path)) {
        if (document.mapped_file) static_cast<void>(document.mapped_file->Open(metadata.path));
        status_.SetText(Tr(L"Large-file streaming save failed; the original file was retained"));
        return false;
    }
    if (document.mapped_file && !document.mapped_file->Open(path)) {
        status_.SetText(Tr(L"Saved, but the mapped view could not be reopened"));
        return false;
    }
    document.file_state = notepad_colon::CaptureFileState(path);
    document.stamp = QueryFileStamp(path);
    document.external_changed = false;
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
    if (!window) return false;
    document.loading_large_window = true;
    document.editor->SetReadOnly(false);
    const bool replaced = document.editor->SetText(window->text);
    document.editor->SetReadOnly(document.read_only);
    document.loading_large_window = false;
    if (!replaced) return false;
    document.mapped_offset = offset;
    document.mapped_decoded_offset = window->decoded_offset;
    if (!document.large_buffer->IsModified()) document.editor->SetSavePoint();
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
    if (!window) { status_.SetText(Tr(L"Window boundary contains invalid text")); return; }
    document->loading_large_window = true;
    document->editor->SetReadOnly(false);
    const bool replaced = document->editor->SetText(window->text);
    document->editor->SetReadOnly(document->read_only);
    document->loading_large_window = false;
    if (!replaced) return;
    document->mapped_offset = offset;
    document->mapped_decoded_offset = window->decoded_offset;
    if (!document->large_buffer->IsModified()) document->editor->SetSavePoint();
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
        Tr(L"\nInvalid byte sequences: ") + std::to_wstring(analysis.invalid_byte_offsets.size()) +
        Tr(L"\nLine endings: ") + std::to_wstring(analysis.eol.crlf) + Tr(L" CRLF, ") +
        std::to_wstring(analysis.eol.lf) + Tr(L" LF, ") + std::to_wstring(analysis.eol.cr) + Tr(L" CR") +
        Tr(L"\nMixed line endings: ") + (analysis.eol.Mixed() ? std::wstring(Tr(L"Yes")) : std::wstring(Tr(L"No"))) +
        Tr(L"\nBidi / zero-width controls: ") + std::to_wstring(analysis.unicode_risks.size());
    ::MessageBoxW(GetHwnd(), message.c_str(), Tr(L"Document Encoding Information").c_str(), MB_OK |
        ((!analysis.invalid_byte_offsets.empty() || analysis.eol.Mixed() || !analysis.unicode_risks.empty())
            ? MB_ICONWARNING : MB_ICONINFORMATION));
}

void MainWindow::ReopenActiveWithCodePage(unsigned int code_page) {
    auto* document = ActiveDocument();
    const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
    if (!document || !metadata || metadata->path.empty()) return;
    if (metadata->dirty && ::MessageBoxW(GetHwnd(),
        Tr(L"Reopening discards unsaved edits. Continue?").c_str(), Tr(L"Reopen with Encoding").c_str(),
        MB_YESNO | MB_ICONWARNING) != IDYES) return;
    const auto bytes = ReadFileBytes(metadata->path);
    if (!bytes) return;
    const auto decoded = notepad_colon::DecodeBytes(*bytes, notepad_colon::EncodingKind::ansi, code_page);
    if (!decoded) { status_.SetText(Tr(L"The selected code page cannot decode this file")); return; }
    const bool was_read_only = document->read_only;
    if (was_read_only) document->editor->SetReadOnly(false);
    if (!document->editor->SetText(*decoded)) return;
    if (was_read_only) document->editor->SetReadOnly(true);
    document->detected_encoding = notepad_colon::EncodingKind::ansi;
    document->ansi_code_page = code_page;
    document->encoding_analysis = notepad_colon::AnalyzeEncoding(*bytes, code_page);
    document->line_ending = notepad_colon::DetectLineEnding(*decoded);
    document->file_state = notepad_colon::CaptureFileState(metadata->path);
    document->stamp.reset(); document->editor->SetSavePoint();
    workspace_.SetDirty(document->id, false);
    SyncPresentation(Tr(L"Reopened as ") + notepad_colon::CodePageName(code_page));
}

void MainWindow::SetAnsiEncoding(unsigned int code_page) {
    auto* document = ActiveDocument();
    if (!document) return;
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
    workspace_.SetDirty(document->id, true);
    SyncPresentation(Tr(L"Will save as ") + notepad_colon::CodePageName(code_page));
}

void MainWindow::SetEncoding(mwfl::TextEncoding encoding) {
    auto* document = ActiveDocument();
    if (!document || (document->encoding == encoding &&
        document->detected_encoding != notepad_colon::EncodingKind::ansi)) return;
    document->encoding = encoding;
    switch (encoding) {
    case mwfl::TextEncoding::utf8: document->detected_encoding = notepad_colon::EncodingKind::utf8; break;
    case mwfl::TextEncoding::utf8_bom: document->detected_encoding = notepad_colon::EncodingKind::utf8_bom; break;
    case mwfl::TextEncoding::utf16_le: document->detected_encoding = notepad_colon::EncodingKind::utf16_le; break;
    case mwfl::TextEncoding::utf16_be: document->detected_encoding = notepad_colon::EncodingKind::utf16_be; break;
    }
    workspace_.SetDirty(document->id, true);
    SyncPresentation(Tr(L"Encoding changed"));
}

void MainWindow::SetLineEnding(notepad_colon::LineEnding ending) {
    auto* document = ActiveDocument();
    if (!document || document->line_ending == ending) return;
    const auto text = document->editor->GetText();
    if (!text) return;
    const auto normalized = notepad_colon::NormalizeLineEndings(*text, ending);
    const auto selection = document->editor->GetSelection();
    if (!document->editor->SetText(normalized)) return;
    document->editor->SetSelection(selection);
    document->line_ending = ending;
    workspace_.SetDirty(document->id, true);
    SyncPresentation(Tr(L"Line endings converted"));
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
        const auto current = notepad_colon::CaptureFileState(metadata->path);
        if (current == document.file_state || document.external_changed) continue;
        if (document.follow_tail && document.large_buffer && current.exists &&
            !metadata->dirty && current.size >= document.file_state.size) {
            if (document.mapped_file) document.mapped_file->Close();
            document.large_buffer->Close();
            const bool reopened = document.large_buffer->Open(metadata->path) &&
                (!document.mapped_file || document.mapped_file->Open(metadata->path));
            const auto offset = current.size > document.mapped_window_size
                ? current.size - document.mapped_window_size : 0;
            if (reopened && LoadLargeFileWindow(document, offset)) {
                document.file_state = current;
                document.editor->Send(SCI_GOTOPOS, document.editor->GetLength());
                status_.SetText(Tr(L"Follow mode loaded appended data"));
                continue;
            }
            document.follow_tail = false;
            status_.SetText(Tr(L"Follow mode stopped because the file could not be reopened"));
        }
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
                SynchronizeTabs(false);
                SyncPresentation(Tr(L"Kept deleted file as an unsaved document"));
            } else {
                document.external_changed = true;
                status_.SetText(metadata->title + Tr(L" is missing on disk; use Save As to preserve it"));
            }
            continue;
        }
        if (!metadata->dirty && current.exists) {
            const auto loaded = mwfl::ReadTextFile(metadata->path);
            auto before = document.editor->GetText();
            if (loaded.Succeeded() && document.editor->SetText(loaded.value->text)) {
                document.encoding = loaded.value->encoding;
                document.line_ending = notepad_colon::DetectLineEnding(loaded.value->text);
                document.stamp = loaded.value->stamp;
                document.file_state = current;
                document.editor->SetSavePoint();
                workspace_.SetDirty(document.id, false);
                const auto changes = before ? NoteExternalReload(document, std::move(*before)) : 0;
                SyncPresentation(changes == 0 ? Tr(L"Reloaded external change")
                    : metadata->title + Tr(L" changed on disk: ") + std::to_wstring(changes) +
                      Tr(L" change(s) to review (F8)"));
            }
        } else if (!IsTestMode() && current.exists) {
            const auto answer = ::MessageBoxW(GetHwnd(),
                (metadata->title + Tr(L" changed on disk while it has unsaved edits.") + L"\n\n" +
                 Tr(L"Yes: compare with disk") + L"\n" + Tr(L"No: reload and discard local edits") +
                 L"\n" + Tr(L"Cancel: keep local edits")).c_str(),
                Tr(L"External edit conflict").c_str(), MB_YESNOCANCEL | MB_ICONWARNING);
            if (answer == IDYES) {
                const auto local = document.editor->GetText();
                const auto disk = mwfl::ReadTextFile(metadata->path);
                if (local && disk.Succeeded())
                    ShowComparison(Tr(L"Disk — ") + metadata->title, *local, disk.value->text);
                document.external_changed = true;
            } else if (answer == IDNO) {
                const auto disk = mwfl::ReadTextFile(metadata->path);
                if (disk.Succeeded() && document.editor->SetText(disk.value->text)) {
                    document.encoding = disk.value->encoding;
                    document.line_ending = notepad_colon::DetectLineEnding(disk.value->text);
                    document.stamp = disk.value->stamp;
                    document.file_state = current;
                    document.external_changed = false;
                    document.editor->SetSavePoint();
                    workspace_.SetDirty(document.id, false);
                    SyncPresentation(Tr(L"Reloaded disk version"));
                }
            } else {
                document.external_changed = true;
                status_.SetText(metadata->title + Tr(L" conflict retained; use Compare or Save As"));
            }
        } else {
            document.external_changed = true;
            status_.SetText(metadata->title + Tr(L" changed on disk; Save As or reload before overwriting"));
        }
    }
}

}  // namespace notepad_colon::app
