#include "main_window.h"

namespace notepad_colon::app {

void MainWindow::BuildUI() {
    static_cast<void>(mwfl::SetProcessAppUserModelId(L"mwfl.notepad-colon"));
    if (!runtime_.LoadAdjacent())
        throw std::runtime_error("Scintilla.dll is not available beside Notepad::");
    if (!lexilla_.LoadAdjacent())
        throw std::runtime_error("Lexilla.dll is not available beside Notepad::");
    if (!IsTestMode()) {
        const auto loaded = mwfl::LoadRecentFilesFromRegistry(HKEY_CURRENT_USER, kSettingsKey, 10);
        if (loaded.Succeeded()) recent_ = *loaded.value;
        LoadPreferences();
    }
    ApplyAppearance();
    BuildCommands();
    ApplyUiLanguage();
    default_shortcuts_ = CaptureShortcuts();
    mwfl::EnableFileDrop(GetHwnd());

    mwfl::ControlHost ui{*this};
    ui.Add(toolbar_);
    ui.Add(search_, kSearch, L"");
    ui.Add(replacement_, kReplacement, L"");
    ui.Add(workspace_filter_, kWorkspaceFilter, L"");
    ui.Add(tree_, kTree, mwfl::RectDip{});
    ui.Add(tabs_, mwfl::TabControlOptions{});
    ui.Add(results_, kResults, mwfl::RectDip{}, mwfl::ListViewOptions{});
    ui.Add(status_);
    const auto toolbar_style = static_cast<DWORD>(
        ::GetWindowLongPtrW(toolbar_.GetHwnd(), GWL_STYLE));
    ::SetWindowLongPtrW(toolbar_.GetHwnd(), GWL_STYLE,
                        toolbar_style & ~static_cast<DWORD>(TBSTYLE_LIST));
    const int dpi = static_cast<int>(::GetDpiForWindow(GetHwnd()));
    // Icons are drawn from the system icon font at the display scale; without
    // that font the 24-pixel bitmap strip is used as it is.
    toolbar_icon_size_ = ::MulDiv(24, dpi, 96);
    mwfl::Must(toolbar_images_.Create(toolbar_icon_size_, toolbar_icon_size_, ILC_COLOR32, 7, 1),
               "create toolbar image list");
    if (!LoadToolbarImages(IsDark()) && toolbar_icon_size_ != 24) {
        toolbar_icon_size_ = 24;
        mwfl::Must(toolbar_images_.Create(24, 24, ILC_COLOR32, 7, 1), "create toolbar image list");
    }
    mwfl::Must(LoadToolbarImages(IsDark()), "load toolbar images");
    mwfl::Must(toolbar_images_.SetBackgroundColor(CLR_NONE),
               "make toolbar images transparent");
    mwfl::Must(toolbar_.SetImageList(toolbar_images_), "attach toolbar images");
    const std::array toolbar_commands{
        std::pair{kNew, 0}, std::pair{kOpen, 1}, std::pair{kSave, 2},
        std::pair{kOpenFolder, 6}, std::pair{kUndo, 3}, std::pair{kRedo, 4},
        std::pair{kToggleFindBar, 5}};
    for (const auto& [id, image] : toolbar_commands)
        commands_.Find(id)->SetImageIndex(image);
    const auto add_button = [&](mwfl::ControlId id) {
        mwfl::Must(toolbar_.AddCommand(*commands_.Find(id)), "add toolbar command");
        TBBUTTONINFOW information{sizeof(information)};
        information.dwMask = TBIF_STYLE;
        information.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
        mwfl::Must(::SendMessageW(toolbar_.GetHwnd(), TB_SETBUTTONINFOW, id.value,
                                 reinterpret_cast<LPARAM>(&information)) != FALSE,
                   "configure toolbar button");
    };
    const auto add_separator = [&] {
        TBBUTTON separator{};
        separator.fsStyle = BTNS_SEP;
        separator.iBitmap = 8;
        mwfl::Must(::SendMessageW(toolbar_.GetHwnd(), TB_ADDBUTTONSW, 1,
                                 reinterpret_cast<LPARAM>(&separator)) != FALSE,
                   "add toolbar separator");
    };
    add_button(kNew); add_button(kOpen); add_button(kSave); add_button(kOpenFolder);
    add_separator(); add_button(kUndo); add_button(kRedo);
    add_separator(); add_button(kToggleFindBar);
    ::SendMessageW(toolbar_.GetHwnd(), TB_SETMAXTEXTROWS, 0, 0);
    ::SendMessageW(toolbar_.GetHwnd(), TB_SETINDENT, 6, 0);
    ::SendMessageW(toolbar_.GetHwnd(), TB_SETBUTTONSIZE, 0,
                   MAKELPARAM(toolbar_icon_size_ + ::MulDiv(16, dpi, 96),
                              toolbar_icon_size_ + ::MulDiv(12, dpi, 96)));
    ::SendMessageW(toolbar_.GetHwnd(), TB_SETPADDING, 0,
                   MAKELPARAM(::MulDiv(8, dpi, 96), ::MulDiv(6, dpi, 96)));
    toolbar_.AutoSize();
    mwfl::Must(mwfl::SetAccessibleName(toolbar_.GetHwnd(), Tr(L"Primary commands").c_str()),
               "name toolbar");
    mwfl::Must(mwfl::AddColumns(results_, {{Tr(L"File"), 330}, {Tr(L"Line"), 70},
                                            {Tr(L"Column"), 70}, {Tr(L"Preview"), 520}}),
               "add search-result columns");
    results_.SetExtendedListStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    mwfl::Must(adapter_.Attach(tabs_) == mwfl::DocumentTabStatus::success,
               "attach document tab adapter");
    ::SendMessageW(tabs_.GetHwnd(), TCM_SETPADDING, 0, MAKELPARAM(14, 5));
    BuildMenu();
    InstallDarkMenuBar(GetHwnd());
    InstallDarkTabStrip(tabs_.GetHwnd());
    InstallDarkStatusBar(status_.GetHwnd());
    mwfl::Must(accelerators_.Create(commands_), "create accelerators");
    SetAccelerators(accelerators_.GetHandle());

    mwfl::Must(mwfl::SetAccessibleName(tabs_.GetHwnd(), Tr(L"Open documents").c_str()), "name tabs");
    mwfl::Must(mwfl::SetAccessibleName(search_.GetHwnd(), Tr(L"Find text").c_str()), "name search");
    mwfl::Must(mwfl::SetAccessibleName(replacement_.GetHwnd(), Tr(L"Replacement text").c_str()),
               "name replacement");
    mwfl::Must(mwfl::SetAccessibleName(status_.GetHwnd(), Tr(L"Document status").c_str()), "name status");
    mwfl::Must(mwfl::SetAccessibleName(tree_.GetHwnd(), Tr(L"Workspace files").c_str()), "name workspace tree");
    mwfl::Must(mwfl::SetAccessibleName(workspace_filter_.GetHwnd(), Tr(L"Filter workspace files").c_str()),
               "name workspace filter");
    mwfl::Must(mwfl::SetAccessibleName(results_.GetHwnd(), Tr(L"Folder search results").c_str()), "name search results");
    ApplyNativeControlColors(GetAppearanceState());

    ApplyCompactLayout();

    ResolveSessionPath();
    std::optional<mwfl::DocumentId> placeholder;
    if (!RestoreSession()) {
        NewDocument();
        placeholder = workspace_.GetActiveId();
        if (!workspace_catalog_.Roots().empty()) StartWorkspaceScan(workspace_catalog_.Roots().back());
    }
    mwfl::Must(instance_.RegisterWindow(GetHwnd()), "register single-instance window");
    for (const auto& path : startup_paths_) static_cast<void>(OpenPathOrFolder(path));
    // Files named on the command line replace the empty document a fresh start
    // would otherwise leave as an extra tab.
    if (placeholder && workspace_.GetCount() > 1) {
        const auto* metadata = workspace_.Find(*placeholder);
        const auto* document = FindDocument(*placeholder);
        const auto opened = workspace_.GetActiveId();
        if (metadata && document && opened && *opened != *placeholder && metadata->path.empty() &&
            !metadata->dirty && document->editor->GetText().value_or(L"x").empty()) {
            workspace_.Activate(*placeholder);
            if (CloseActive(true)) workspace_.Activate(*opened);
            SynchronizeTabs(true);
        }
    }
    mwfl::Must(monitor_timer_.Start(*this, kMonitorTimer, std::chrono::milliseconds{2000}),
               "start external-file monitor");
    if ((IsSelfTest() || large_file_self_test_) &&
        !::PostMessageW(GetHwnd(), kSelfTestMessage, 0, 0))
        throw std::runtime_error("could not schedule GUI self-test");
    if (session_restore_warning_) status_.SetText(*session_restore_warning_);
}

mwfl::EventResult MainWindow::OnCommand(const mwfl::CommandEvent& event) {
    if (event.id == kSearch && event.notification == EN_CHANGE) {
        ::SetTimer(GetHwnd(), kIncrementalSearchTimer.value, 120, nullptr);
        return mwfl::EventResult::Handled();
    }
    if (event.id == kWorkspaceFilter) {
        ::SetTimer(GetHwnd(), kWorkspaceFilterTimer.value, 150, nullptr);
        return mwfl::EventResult::Handled();
    }
    // Menus, toolbar buttons and accelerators report notification code 0 or 1.
    // Editor controls also send WM_COMMAND (focus/change) under their own
    // control ids, which overlap the command id range; never run a command
    // for those.
    if (event.notification > 1) return mwfl::EventResult::Propagate();
    auto* command = commands_.Find(event.id);
    if (!command || !command->IsEnabled() || !command->IsVisible())
        return mwfl::EventResult::Propagate();
    if (!playing_macro_ && macro_recorder_.IsRecording() && IsMacroSafeCommand(event.id))
        macro_recorder_.RecordCommand(static_cast<std::uint16_t>(event.id.value));
    RememberCommand(event.id);
    command->Invoke();
    return mwfl::EventResult::Handled();
}

mwfl::EventResult MainWindow::OnNotify(const mwfl::NotifyEvent& event) {
    if (event.Is(tabs_, TCN_SELCHANGE)) {
        adapter_.ActivateNativeSelection(workspace_);
        ArrangeViews();
        SyncPresentation(Tr(L"Document selected"));
        return mwfl::EventResult::Handled();
    }
    if (event.IsFrom(tree_)) {
        const auto notification = tree_.DecodeNotification(event.header);
        if (notification && notification->kind == mwfl::TreeViewNotificationKind::selection_changed) {
            const auto found = tree_paths_.find(notification->item.value);
            if (found != tree_paths_.end() && std::filesystem::is_regular_file(found->second))
                static_cast<void>(OpenPath(found->second));
            return mwfl::EventResult::Handled();
        }
        if (notification && notification->kind == mwfl::TreeViewNotificationKind::item_expanded &&
            notification->action == TVE_EXPAND && workspace_lazy_) {
            LoadWorkspaceChildren(notification->item);
            return mwfl::EventResult::Handled();
        }
    }
    if (event.IsFrom(results_)) {
        if (const auto notification = results_.DecodeNotification(event.header);
            notification && notification->kind == mwfl::ListViewNotificationKind::activated) {
            const auto index = notification->item.value > 0 ? notification->item.value - 1 : 0;
            if (index < search_results_.matches.size() && OpenPath(search_results_.matches[index].path))
                if (auto* editor = ActiveEditor())
                    notepad_colon::GoToLine(*editor, search_results_.matches[index].line);
            return mwfl::EventResult::Handled();
        }
    }
    auto* document = FindByHwnd(event.header.hwndFrom);
    if (!document) return mwfl::EventResult::Propagate();
    // Markers and indicators applied by DecorateVisibleLines are not edits.
    if (decorating_) return mwfl::EventResult::Propagate();
    // Double-clicking a row of a line-filter result opens the line it came from.
    if (event.header.code == SCN_DOUBLECLICK && document->filter_link &&
        document->editor->GetHwnd() == event.header.hwndFrom) {
        GoToFilterSource();
        return mwfl::EventResult::Handled();
    }
    if (document->split_editor && document->split_editor->GetHwnd() == event.header.hwndFrom) {
        // The clone shares the document: content changes are handled once, from
        // the primary view. Only view-local feedback is handled here.
        auto& view = *document->split_editor;
        const auto notification = view.DecodeNotification(event.header);
        if (!notification) return mwfl::EventResult::Propagate();
        if (notification->kind == mwfl::ScintillaNotificationKind::modified &&
            notification->lines_added != 0) {
            static_cast<void>(view.UpdateLineNumberMargin());
        } else if (notification->kind == mwfl::ScintillaNotificationKind::character_added) {
            if (!playing_macro_ && macro_recorder_.IsRecording() && notification->character > 0)
                macro_recorder_.RecordText(std::wstring(1, static_cast<wchar_t>(notification->character)));
            notepad_colon::HandleCharacterAdded(view, notification->character);
        } else if (notification->kind == mwfl::ScintillaNotificationKind::update_ui) {
            notepad_colon::UpdateBraceHighlight(view);
            StyleVisibleSyntax(*document, view);
            DecorateVisibleLines(*document, view, 1);
        }
        return mwfl::EventResult::Propagate();
    }
    const auto notification = document->editor->DecodeNotification(event.header);
    if (!notification) return mwfl::EventResult::Propagate();
    if (notification->kind == mwfl::ScintillaNotificationKind::save_point_left && !document->loading_large_window) {
        workspace_.SetDirty(document->id, true);
    } else if (notification->kind == mwfl::ScintillaNotificationKind::save_point_reached && !document->loading_large_window) {
        const auto* metadata = workspace_.Find(document->id);
        workspace_.SetDirty(document->id, (document->large_buffer && document->large_buffer->IsModified()) ||
            (metadata && metadata->path.empty() && document->editor->GetLength() != 0));
    } else if (notification->kind == mwfl::ScintillaNotificationKind::modified &&
               notification->lines_added != 0) {
        static_cast<void>(document->editor->UpdateLineNumberMargin());
    } else if (notification->kind == mwfl::ScintillaNotificationKind::character_added) {
        if (!playing_macro_ && macro_recorder_.IsRecording() && notification->character > 0)
            macro_recorder_.RecordText(std::wstring(1, static_cast<wchar_t>(notification->character)));
        notepad_colon::HandleCharacterAdded(*document->editor, notification->character);
    } else if (notification->kind == mwfl::ScintillaNotificationKind::update_ui) {
        notepad_colon::UpdateBraceHighlight(*document->editor);
        StyleVisibleSyntax(*document);
        DecorateVisibleLines(*document, *document->editor, 0);
    }
    if (notification->kind == mwfl::ScintillaNotificationKind::modified &&
        !document->loading_large_window &&
        (reinterpret_cast<const SCNotification*>(&event.header)->modificationType &
         (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) != 0)
        ++document->revision;
    if (notification->kind == mwfl::ScintillaNotificationKind::modified)
        UpdateLargeBuffer(*document, *reinterpret_cast<const SCNotification*>(&event.header));
    if (notification->kind == mwfl::ScintillaNotificationKind::modified)
        UpdateSyntaxTree(*document, *reinterpret_cast<const SCNotification*>(&event.header));
    workspace_.SetUndoState(document->id, document->editor->CanUndo(), document->editor->CanRedo());
    SyncPresentation(Tr(L"Editing"));
    if (notification->kind == mwfl::ScintillaNotificationKind::modified && !document->loading_large_window) {
        session_snapshot_due_ = std::chrono::steady_clock::now() + kSessionDebounce;
        session_snapshot_pending_ = true;
    }
    return mwfl::EventResult::Propagate();
}

mwfl::EventResult MainWindow::OnResize(const mwfl::ResizeEvent&) {
    ArrangeViews();
    return mwfl::EventResult::Propagate();
}

mwfl::EventResult MainWindow::OnAppearanceChanged(const mwfl::AppearanceState& state) {
    RefreshNativeMenuTheme();
    ApplyEditorAppearance(state.IsDark());
    ApplyNativeControlColors(state);
    return mwfl::EventResult::Propagate();
}

mwfl::EventResult MainWindow::OnMessage(const mwfl::WindowMessage& event) {
    if (event.id == kSelfTestMessage) {
        RunSelfTest();
        return mwfl::EventResult::Handled();
    }
    if (event.id == WM_DROPFILES) {
        const auto paths = mwfl::ReadDroppedFiles(reinterpret_cast<HDROP>(event.wparam));
        std::size_t opened = 0;
        for (const auto& path : paths) opened += OpenPathOrFolder(path) ? 1u : 0u;
        status_.SetText(Tr(L"Opened ") + std::to_wstring(opened) + Tr(L" dropped file(s)"));
        return mwfl::EventResult::Handled();
    }
    if (event.id == kSearchCompleteMessage) {
        CompleteSearch();
        return mwfl::EventResult::Handled();
    }
    if (event.id == kReplaceCompleteMessage) {
        CompleteReplace();
        return mwfl::EventResult::Handled();
    }
    if (event.id == kFilterCompleteMessage) {
        CompleteLineFilter();
        return mwfl::EventResult::Handled();
    }
    if (event.id == kExtractCompleteMessage) {
        CompleteColumnExtract();
        return mwfl::EventResult::Handled();
    }
    if (event.id == kWorkspaceCompleteMessage) {
        CompleteWorkspaceScan();
        return mwfl::EventResult::Handled();
    }
    if (auto activation = instance_.DecodeActivation(event.id, event.lparam)) {
        if (::IsIconic(GetHwnd())) ::ShowWindow(GetHwnd(), SW_RESTORE);
        ::SetForegroundWindow(GetHwnd());
        std::size_t start = 0;
        bool opened = false;
        while (start <= activation->size()) {
            const auto end = activation->find(L'\n', start);
            const auto item = activation->substr(start, end - start);
            if (!item.empty()) opened = OpenPathOrFolder(item) || opened;
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
        if (activation_test_server_) {
            activation_test_result_ = opened ? 0 : 4;
            ::SetTimer(GetHwnd(), kActivationTestTimer.value, 500, nullptr);
        }
        return mwfl::EventResult::Handled(TRUE);
    }
    return mwfl::EventResult::Propagate();
}

mwfl::EventResult MainWindow::OnTimer(mwfl::TimerId id) {
    if (id == kActivationTestTimer) {
        ::KillTimer(GetHwnd(), kActivationTestTimer.value);
        RemoveTestState();
        ::PostQuitMessage(activation_test_result_);
        return mwfl::EventResult::Handled();
    }
    if (id == kWorkspaceFilterTimer) {
        ::KillTimer(GetHwnd(), kWorkspaceFilterTimer.value);
        if (workspace_lazy_) RenderLazyWorkspaceRoots();
        else RenderWorkspaceTree();
        return mwfl::EventResult::Handled();
    }
    if (id == kIncrementalSearchTimer) {
        ::KillTimer(GetHwnd(), kIncrementalSearchTimer.value);
        IncrementalSearch();
        return mwfl::EventResult::Handled();
    }
    if (id != kMonitorTimer) return mwfl::EventResult::Propagate();
    CheckExternalChanges(false);
    AutoSaveIfDue();
    SaveRecoverySnapshotsIfDue();
    QueueSessionSnapshotIfDue();
    return mwfl::EventResult::Handled();
}

mwfl::EventResult MainWindow::OnClose() {
    if (!IsTestMode()) {
        for (const auto& document : workspace_.GetDocuments()) {
            if (!document.dirty) continue;
            if (::MessageBoxW(GetHwnd(),
                              Tr(L"Close Notepad::? Unsaved documents will be restored next time.").c_str(),
                              Tr(L"Notepad::").c_str(), MB_ICONINFORMATION | MB_OKCANCEL) != IDOK)
                return mwfl::EventResult::Handled();
            break;
        }
        if (session_checkpoint_blocked_) {
            // A startup archive may have failed only because another process
            // temporarily held a manifest. Retry before giving up on closing.
            if (const auto preserved = notepad_colon::PreserveUnrestoredSession(session_path_))
                session_checkpoint_blocked_ = false;
        }
        const bool checkpoint_saved = SaveSessionSnapshot() && session_writer_.Flush();
        const bool all_text_saved = session_checkpoint_blocked_ && std::ranges::all_of(documents_, [&](const auto& document) {
            const auto* metadata = workspace_.Find(document.id);
            if (!metadata || metadata->dirty || document.external_changed) return false;
            if (metadata->path.empty()) return document.editor->GetLength() == 0;
            return document.file_state.exists && document.file_state == notepad_colon::CaptureFileState(metadata->path);
        });
        if (!checkpoint_saved && !all_text_saved) {
            ::MessageBoxW(GetHwnd(),
                Tr(L"Your session could not be saved. The editor will stay open; save your documents or free disk space and try again.").c_str(),
                Tr(L"Session recovery unavailable").c_str(), MB_OK | MB_ICONERROR);
            return mwfl::EventResult::Handled();
        }
        static_cast<void>(mwfl::SaveRecentFilesToRegistry(
            HKEY_CURRENT_USER, kSettingsKey, recent_));
        SaveWorkspaceCatalog();
        static_cast<void>(SaveConfigurationFile(configuration_path_));
    }
    monitor_timer_.Stop();
    StopWorkers();
    adapter_.Detach();
    instance_.UnregisterWindow();
    return mwfl::EventResult::Propagate();
}

std::wstring DocumentEncodingName(const EditorDocument& document) {
    if (document.detected_encoding == notepad_colon::EncodingKind::ansi)
        return notepad_colon::CodePageName(document.ansi_code_page);
    return std::wstring{EncodingName(document.encoding)};
}

EditorDocument* MainWindow::FindDocument(mwfl::DocumentId id) noexcept {
    const auto found = std::ranges::find(documents_, id, &EditorDocument::id);
    return found == documents_.end() ? nullptr : &*found;
}

EditorDocument* MainWindow::FindByHwnd(HWND hwnd) noexcept {
    const auto found = std::ranges::find_if(documents_,
        [hwnd](const EditorDocument& document) {
            return document.editor->GetHwnd() == hwnd ||
                   (document.split_editor && document.split_editor->GetHwnd() == hwnd);
        });
    return found == documents_.end() ? nullptr : &*found;
}

EditorDocument* MainWindow::ActiveDocument() noexcept {
    const auto id = workspace_.GetActiveId();
    return id ? FindDocument(*id) : nullptr;
}

mwfl::ScintillaEditor* MainWindow::ActiveEditor() noexcept {
    const auto* document = ActiveDocument();
    if (!document) return nullptr;
    // Commands act on whichever view of the document has the keyboard focus.
    if (document->split_editor && ::GetFocus() == document->split_editor->GetHwnd())
        return document->split_editor.get();
    return document->editor.get();
}

void MainWindow::ApplyCompactLayout() {
    ::ShowWindow(search_.GetHwnd(), find_bar_visible_ ? SW_SHOW : SW_HIDE);
    ::ShowWindow(replacement_.GetHwnd(), find_bar_visible_ ? SW_SHOW : SW_HIDE);
    ::ShowWindow(tree_.GetHwnd(), workspace_visible_ ? SW_SHOW : SW_HIDE);
    ::ShowWindow(workspace_filter_.GetHwnd(), workspace_visible_ ? SW_SHOW : SW_HIDE);
    ::ShowWindow(results_.GetHwnd(), results_visible_ ? SW_SHOW : SW_HIDE);
    SetLayout(mwfl::Column()
        .Add(toolbar_, mwfl::Auto())
        .Add(mwfl::Row().Gap(4.0_dip).Margin(3.0_dip)
            .Add(search_, mwfl::Stretch())
            .Add(replacement_, mwfl::Stretch()),
            mwfl::Fixed(find_bar_visible_ ? 36.0_dip : 0.0_dip))
        .Add(mwfl::Row().Gap(workspace_visible_ ? 3.0_dip : 0.0_dip)
            .Add(mwfl::Column().Gap(3.0_dip)
                .Add(workspace_filter_, mwfl::Fixed(workspace_visible_ ? 30.0_dip : 0.0_dip))
                .Add(tree_, mwfl::Stretch()),
                mwfl::Fixed(workspace_visible_ ? 240.0_dip : 0.0_dip))
            .Add(mwfl::Column().Gap(results_visible_ ? 3.0_dip : 0.0_dip)
                .Add(tabs_, mwfl::Stretch())
                .Add(results_, mwfl::Fixed(results_visible_ ? 160.0_dip : 0.0_dip)),
                mwfl::Stretch()), mwfl::Stretch())
        .Add(status_, mwfl::Fixed(24.0_dip)));
    ArrangeViews();
    ::InvalidateRect(GetHwnd(), nullptr, TRUE);
}

void MainWindow::NewDocument(std::wstring text, std::wstring title) {
    const mwfl::DocumentId id{next_id_++};
    if (title.empty()) title = id.value == 1 ? Tr(L"Untitled") : Tr(L"Untitled ") + std::to_wstring(id.value);
    auto editor = std::make_unique<mwfl::ScintillaEditor>();
    mwfl::Must(editor->Create(GetHwnd(), mwfl::ControlId{static_cast<WORD>(200 + id.value)},
                              mwfl::RectDip{}, runtime_), "create document editor");
    mwfl::Must(editor->ConfigureCodeEditing(), "configure document editor");
    notepad_colon::ConfigureAdvancedEditing(*editor);
    notepad_colon::ApplyPreferences(*editor, preferences_, IsDark());
    mwfl::Must(notepad_colon::ConfigureLanguage(
        *editor, lexilla_, notepad_colon::Language::plain_text, IsDark()), "configure plain-text lexer");
    mwfl::Must(notepad_colon::SetEditorTextBaseline(*editor, text), "set document text");
    editor->SetSavePoint();
    mwfl::Must(static_cast<bool>(workspace_.Add({id, title, {}})), "add document metadata");
    mwfl::Must(adapter_.BindPage(id, editor->GetHwnd()) == mwfl::DocumentTabStatus::success,
               "bind document page");
    mwfl::Must(mwfl::SetAccessibleName(editor->GetHwnd(), title.c_str()), "name document editor");
    documents_.push_back({id, std::move(editor)});
    workspace_.SetDirty(id, !text.empty());
    InitializeSyntaxTree(documents_.back());
    workspace_.Activate(id);
    SynchronizeTabs(true);
    SyncPresentation(Tr(L"New document"));
    static_cast<void>(SaveSessionSnapshot());
}

mwfl::ColorMode MainWindow::ToColorMode(notepad_colon::ThemePreference theme) noexcept {
    switch (theme) {
    case notepad_colon::ThemePreference::light: return mwfl::ColorMode::light;
    case notepad_colon::ThemePreference::dark: return mwfl::ColorMode::dark;
    case notepad_colon::ThemePreference::system: return mwfl::ColorMode::system;
    }
    return mwfl::ColorMode::system;
}

void MainWindow::ApplyAppearance() {
    SetAppearance({ToColorMode(preferences_.theme), mwfl::Backdrop::mica});
    const auto& state = GetAppearanceState();
    ApplyEditorAppearance(state.IsDark());
    ApplyNativeControlColors(state);
}

void MainWindow::ApplyEditorAppearance(bool dark) {
    for (auto& document : documents_) {
        notepad_colon::ApplyPreferences(*document.editor, preferences_, dark);
        static_cast<void>(ConfigureViewLanguage(*document.editor, document, dark));
        if (document.split_editor) {
            notepad_colon::ApplyPreferences(*document.split_editor, preferences_, dark);
            static_cast<void>(ConfigureViewLanguage(*document.split_editor, document, dark));
        }
        if (document.syntax_tree || document.wasm_syntax) {
            notepad_colon::ConfigureTreeSitterStyles(*document.editor, dark);
            StyleVisibleSyntax(document);
            if (document.split_editor) {
                notepad_colon::ConfigureTreeSitterStyles(*document.split_editor, dark);
                StyleVisibleSyntax(document, *document.split_editor);
            }
        }
    }
}

bool MainWindow::LoadToolbarImages(bool dark) noexcept {
    const COLORREF ink = dark ? RGB(228, 228, 228) : RGB(42, 55, 70);
    // In image-list order: new, open, save, undo, redo, find, open folder.
    constexpr std::wstring_view glyphs = L"";
    if (FillToolbarGlyphs(toolbar_images_.GetHandle(), glyphs, toolbar_icon_size_, ink)) return true;
    // The bitmap strip only fits an image list of its own size.
    if (toolbar_icon_size_ != 24) return false;
    const auto strip = reinterpret_cast<HBITMAP>(::LoadImageW(
        ::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDB_TOOLBAR), IMAGE_BITMAP,
        0, 0, LR_CREATEDIBSECTION));
    if (!strip) return false;
    const bool loaded = FillToolbarImages(toolbar_images_.GetHandle(), strip, ink);
    ::DeleteObject(strip);
    return loaded;
}

void MainWindow::ApplyNativeControlColors(const mwfl::AppearanceState& state) noexcept {
    FlattenControlBorders(GetHwnd(), state.IsDark());
    if (toolbar_.GetHwnd() && toolbar_images_.GetHandle()) {
        static_cast<void>(LoadToolbarImages(state.IsDark()));
        ::InvalidateRect(toolbar_.GetHwnd(), nullptr, TRUE);
    }
    const COLORREF background = state.palette.control_background;
    const COLORREF text = state.palette.text;
    if (status_.GetHwnd())
        ::SendMessageW(status_.GetHwnd(), SB_SETBKCOLOR, 0, background);
    if (tree_.GetHwnd()) {
        TreeView_SetBkColor(tree_.GetHwnd(), background);
        TreeView_SetTextColor(tree_.GetHwnd(), text);
    }
    if (results_.GetHwnd()) {
        ListView_SetBkColor(results_.GetHwnd(), background);
        ListView_SetTextBkColor(results_.GetHwnd(), background);
        ListView_SetTextColor(results_.GetHwnd(), text);
    }
    ::RedrawWindow(GetHwnd(), nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void MainWindow::StopWorkers() {
    for (auto& worker : search_workers_) worker.thread.request_stop();
    for (auto& worker : workspace_workers_) worker.thread.request_stop();
    search_workers_.clear();
    workspace_workers_.clear();
}

void MainWindow::SynchronizeTabs(bool focus) {
    const auto result = adapter_.Synchronize(workspace_, focus);
    if (!result) throw std::runtime_error("synchronize document tabs failed");
    ArrangeViews();
}

void MainWindow::SyncPresentation(std::wstring_view action) {
    const auto* metadata = workspace_.GetActiveId() ? workspace_.Find(*workspace_.GetActiveId()) : nullptr;
    const auto* document = ActiveDocument();
    const auto title = (metadata ? metadata->title : Tr(L"Notepad::")) +
        std::wstring(metadata && metadata->dirty ? Tr(L" * — Notepad::") : Tr(L" — Notepad::"));
    if (title != presentation_title_) {
        SetTitle(title);
        presentation_title_ = title;
    }
    if (metadata && document) {
        const auto selection = document->editor->GetSelection();
        status_.SetText(std::wstring(action) + Tr(L" | Pos ") + std::to_wstring(selection.end) +
            L" | " + DocumentEncodingName(*document) + L" | " +
            std::wstring(LineEndingName(document->line_ending)) + L" | " +
            Tr(notepad_colon::LanguageName(document->language)) + L" | " +
            DelimitedFieldStatus(*document) +
            std::to_wstring(workspace_.GetCount()) + Tr(L" document(s)") +
            (PendingReviewCount() == 0 ? std::wstring{}
                : L" | " + std::to_wstring(PendingReviewCount()) + Tr(L" to review")));
    } else status_.SetText(action);

    const bool has_editor = document != nullptr;
    bool menu_changed = false;
    const auto set_enabled = [&](mwfl::ControlId id, bool enabled, bool on_toolbar) {
        auto* command = commands_.Find(id);
        if (!command || command->IsEnabled() == enabled) return;
        command->SetEnabled(enabled);
        static_cast<void>(menu_.UpdateCommand(*command));
        if (on_toolbar) static_cast<void>(toolbar_.UpdateCommand(*command));
        menu_changed = true;
    };
    set_enabled(kSave, has_editor && !document->read_only, true);
    set_enabled(kClose, has_editor, false);
    set_enabled(kUndo, has_editor && document->editor->CanUndo(), true);
    set_enabled(kRedo, has_editor && document->editor->CanRedo(), true);
    const bool can_convert = has_editor && !document->large_buffer && !document->read_only;
    for (const auto id : {kUtf8, kUtf8Bom, kUtf16Le, kUtf16Be, kSaveSystemAnsi, kCrlf, kLf})
        set_enabled(id, can_convert, false);
    for (WORD index = 0; index < notepad_colon::SupportedLegacyCodePages().size(); ++index)
        set_enabled({static_cast<WORD>(kConvertCodePageBase.value + index)}, can_convert, false);
    if (menu_changed) ::DrawMenuBar(GetHwnd());
}

}  // namespace notepad_colon::app
