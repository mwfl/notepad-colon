#include "main_window.h"

namespace notepad_colon::app {

void MainWindow::LoadPreferences() {
    const std::array schema{
        mwfl::SettingDefinition{L"FontName", mwfl::SettingType::string, 256, true},
        mwfl::SettingDefinition{L"FontSize", mwfl::SettingType::dword, 4, true},
        mwfl::SettingDefinition{L"TabWidth", mwfl::SettingType::dword, 4, true},
        mwfl::SettingDefinition{L"Theme", mwfl::SettingType::dword, 4, true},
        mwfl::SettingDefinition{L"AutoSave", mwfl::SettingType::dword, 4, false},
        mwfl::SettingDefinition{L"AutoSaveSeconds", mwfl::SettingType::dword, 4, false},
        mwfl::SettingDefinition{L"TrimTrailingOnSave", mwfl::SettingType::dword, 4, false},
        mwfl::SettingDefinition{L"CreateBackup", mwfl::SettingType::dword, 4, false},
        mwfl::SettingDefinition{L"EnsureFinalNewline", mwfl::SettingType::dword, 4, false},
        mwfl::SettingDefinition{L"ChineseUi", mwfl::SettingType::dword, 4, false},
        mwfl::SettingDefinition{L"HighlightRules", mwfl::SettingType::string, 2048, false}};
    const auto loaded = settings_.Load(schema);
    if (!loaded) return;
    notepad_colon::Preferences candidate;
    if (const auto* value = mwfl::FindSetting(loaded.values, L"FontName"))
        candidate.font_name = std::get<std::wstring>(value->data);
    if (const auto* value = mwfl::FindSetting(loaded.values, L"FontSize"))
        candidate.font_size = std::get<std::uint32_t>(value->data);
    if (const auto* value = mwfl::FindSetting(loaded.values, L"TabWidth"))
        candidate.tab_width = std::get<std::uint32_t>(value->data);
    if (const auto* value = mwfl::FindSetting(loaded.values, L"Theme"))
        candidate.theme = static_cast<notepad_colon::ThemePreference>(
            std::get<std::uint32_t>(value->data));
    if (const auto* value = mwfl::FindSetting(loaded.values, L"AutoSave"))
        candidate.auto_save = std::get<std::uint32_t>(value->data) != 0;
    if (const auto* value = mwfl::FindSetting(loaded.values, L"AutoSaveSeconds"))
        candidate.auto_save_seconds = std::get<std::uint32_t>(value->data);
    if (const auto* value = mwfl::FindSetting(loaded.values, L"TrimTrailingOnSave"))
        candidate.trim_trailing_whitespace_on_save = std::get<std::uint32_t>(value->data) != 0;
    if (const auto* value = mwfl::FindSetting(loaded.values, L"CreateBackup"))
        candidate.create_backup_before_save = std::get<std::uint32_t>(value->data) != 0;
    if (const auto* value = mwfl::FindSetting(loaded.values, L"EnsureFinalNewline"))
        candidate.ensure_final_newline = std::get<std::uint32_t>(value->data) != 0;
    if (const auto* value = mwfl::FindSetting(loaded.values, L"HighlightRules")) {
        const auto utf8 = mwfl::ToUtf8(std::get<std::wstring>(value->data));
        if (auto rules = utf8 ? notepad_colon::ParseHighlightRules(*utf8) : std::nullopt)
            highlight_rules_ = std::move(*rules);
    }
    if (const auto* value = mwfl::FindSetting(loaded.values, L"ChineseUi"))
        chinese_ui_ = std::get<std::uint32_t>(value->data) != 0;
    preferences_ = notepad_colon::SanitizePreferences(std::move(candidate));
}

bool MainWindow::SavePreferences() {
    const std::array values{
        mwfl::SettingValue{L"FontName", preferences_.font_name},
        mwfl::SettingValue{L"FontSize", preferences_.font_size},
        mwfl::SettingValue{L"TabWidth", preferences_.tab_width},
        mwfl::SettingValue{L"Theme", static_cast<std::uint32_t>(preferences_.theme)},
        mwfl::SettingValue{L"AutoSave", static_cast<std::uint32_t>(preferences_.auto_save)},
        mwfl::SettingValue{L"AutoSaveSeconds", preferences_.auto_save_seconds},
        mwfl::SettingValue{L"TrimTrailingOnSave", static_cast<std::uint32_t>(preferences_.trim_trailing_whitespace_on_save)},
        mwfl::SettingValue{L"CreateBackup", static_cast<std::uint32_t>(preferences_.create_backup_before_save)},
        mwfl::SettingValue{L"EnsureFinalNewline", static_cast<std::uint32_t>(preferences_.ensure_final_newline)},
        mwfl::SettingValue{L"ChineseUi", static_cast<std::uint32_t>(chinese_ui_)},
        mwfl::SettingValue{L"HighlightRules", mwfl::FromUtf8(
            notepad_colon::FormatHighlightRules(highlight_rules_)).value_or(std::wstring{})}};
    return static_cast<bool>(settings_.Save(values));
}

void MainWindow::ShowPreferences() {
    mwfl::Label font_label, size_label, tab_label, theme_label, theme_value;
    mwfl::TextBox font, size, tab;
    mwfl::Button system, light, dark, accept, cancel;
    mwfl::CheckBox auto_save, trim_trailing, create_backup, final_newline;
    auto candidate = preferences_;
    mwfl::Dialog* pointer = nullptr;
    mwfl::Dialog dialog({
        .owner = GetHwnd(), .title = Tr(L"Notepad:: Preferences"),
        .initial_client_size = {500.0_dip, 390.0_dip}, .resizable = false,
        .callbacks = {
            .initialize = [&](HWND window) {
                mwfl::ControlHost ui{window};
                ui.Add(font_label, {401}, Tr(L"Font family")); ui.Add(font, {402}, candidate.font_name);
                ui.Add(size_label, {403}, Tr(L"Font size (8-40)")); ui.Add(size, {404}, std::to_wstring(candidate.font_size));
                ui.Add(tab_label, {405}, Tr(L"Tab width (1-16)")); ui.Add(tab, {406}, std::to_wstring(candidate.tab_width));
                ui.Add(theme_label, {407}, Tr(L"Theme")); ui.Add(theme_value, {408}, L"");
                ui.Add(system, {409}, Tr(L"System")); ui.Add(light, {410}, Tr(L"Light")); ui.Add(dark, {411}, Tr(L"Dark"));
                ui.Add(auto_save, {412}, Tr(L"Auto-save named documents every 30 seconds"));
                ui.Add(trim_trailing, {413}, Tr(L"Trim trailing whitespace when saving"));
                ui.Add(create_backup, {414}, Tr(L"Create .bak file before overwriting"));
                ui.Add(final_newline, {415}, Tr(L"Ensure final newline when saving"));
                auto_save.SetChecked(candidate.auto_save);
                trim_trailing.SetChecked(candidate.trim_trailing_whitespace_on_save);
                create_backup.SetChecked(candidate.create_backup_before_save);
                final_newline.SetChecked(candidate.ensure_final_newline);
                ui.Add(accept, {IDOK}, Tr(L"Save")); ui.Add(cancel, {IDCANCEL}, Tr(L"Cancel"));
                const auto update_theme = [&] { theme_value.SetText(
                    candidate.theme == notepad_colon::ThemePreference::system ? Tr(L"System") :
                    candidate.theme == notepad_colon::ThemePreference::light ? Tr(L"Light") : Tr(L"Dark")); };
                update_theme();
                mwfl::SetAccessibleName(font.GetHwnd(), Tr(L"Editor font family").c_str());
                mwfl::SetAccessibleName(size.GetHwnd(), Tr(L"Editor font size").c_str());
                mwfl::SetAccessibleName(tab.GetHwnd(), Tr(L"Editor tab width").c_str());
                mwfl::SetDialogDefaultButton(window, IDOK);
                return pointer->SetLayout(mwfl::Column().Margin(16.0_dip).Gap(8.0_dip)
                    .Add(mwfl::Row().Gap(8.0_dip).Add(font_label, mwfl::Fixed(150.0_dip)).Add(font, mwfl::Stretch()), mwfl::Fixed(34.0_dip))
                    .Add(mwfl::Row().Gap(8.0_dip).Add(size_label, mwfl::Fixed(150.0_dip)).Add(size, mwfl::Stretch()), mwfl::Fixed(34.0_dip))
                    .Add(mwfl::Row().Gap(8.0_dip).Add(tab_label, mwfl::Fixed(150.0_dip)).Add(tab, mwfl::Stretch()), mwfl::Fixed(34.0_dip))
                    .Add(mwfl::Row().Gap(8.0_dip).Add(theme_label, mwfl::Fixed(80.0_dip)).Add(theme_value, mwfl::Fixed(70.0_dip)).Add(system, mwfl::Auto()).Add(light, mwfl::Auto()).Add(dark, mwfl::Auto()), mwfl::Fixed(36.0_dip))
                    .Add(auto_save, mwfl::Fixed(28.0_dip))
                    .Add(trim_trailing, mwfl::Fixed(28.0_dip))
                    .Add(create_backup, mwfl::Fixed(28.0_dip))
                    .Add(final_newline, mwfl::Fixed(28.0_dip))
                    .Add(mwfl::Row().Gap(8.0_dip).Add(mwfl::Column(), mwfl::Stretch()).Add(accept, mwfl::Fixed(100.0_dip)).Add(cancel, mwfl::Fixed(100.0_dip)), mwfl::Fixed(36.0_dip)));
            },
            .command = [&](HWND, WORD id, WORD) {
                if (id == 409 || id == 410 || id == 411) {
                    candidate.theme = id == 409 ? notepad_colon::ThemePreference::system :
                                      id == 410 ? notepad_colon::ThemePreference::light :
                                                  notepad_colon::ThemePreference::dark;
                    theme_value.SetText(id == 409 ? Tr(L"System") : id == 410 ? Tr(L"Light") : Tr(L"Dark"));
                }
                if (id == IDOK) {
                    candidate.font_name = font.GetText();
                    wchar_t* end{};
                    candidate.font_size = static_cast<std::uint32_t>(std::wcstoul(size.GetText().c_str(), &end, 10));
                    candidate.tab_width = static_cast<std::uint32_t>(std::wcstoul(tab.GetText().c_str(), &end, 10));
                    candidate.auto_save = auto_save.IsChecked();
                    candidate.trim_trailing_whitespace_on_save = trim_trailing.IsChecked();
                    candidate.create_backup_before_save = create_backup.IsChecked();
                    candidate.ensure_final_newline = final_newline.IsChecked();
                    if (!notepad_colon::ValidatePreferences(candidate)) {
                        ::MessageBoxW(pointer->GetHwnd(), Tr(L"Enter a valid font, size (8-40), and tab width (1-16).").c_str(), Tr(L"Preferences").c_str(), MB_OK | MB_ICONWARNING);
                        return true;
                    }
                }
                return false;
            }} });
    pointer = &dialog;
    if (dialog.ShowModal()) {
        preferences_ = std::move(candidate);
        ApplyAppearance();
        if (!IsTestMode()) static_cast<void>(SavePreferences());
        if (!IsTestMode()) static_cast<void>(SaveConfigurationFile(configuration_path_));
        SyncPresentation(Tr(L"Preferences applied"));
    }
}

void MainWindow::SaveRecoverySnapshotsIfDue() {
    if (!recovery_store_ || restoring_session_) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - last_recovery_snapshot_ < std::chrono::seconds{30}) return;
    last_recovery_snapshot_ = now;
    for (const auto& metadata : workspace_.GetDocuments()) {
        if (!metadata.dirty) continue;
        const auto* document = FindDocument(metadata.id);
        const auto text = document ? document->editor->GetText() : std::nullopt;
        if (!text) continue;
        const auto key = metadata.path.empty()
            ? L"untitled-" + std::to_wstring(metadata.id.value)
            : metadata.path.wstring();
        static_cast<void>(recovery_store_->Save(key, metadata.title, metadata.path, *text));
    }
}

void MainWindow::ShowRecoveryManager() {
    if (!recovery_store_) return;
    auto snapshots = recovery_store_->List();
    mwfl::ListBox list;
    mwfl::Label details;
    mwfl::Button restore, remove, close;
    std::optional<std::size_t> chosen;
    mwfl::Dialog* pointer = nullptr;
    mwfl::Dialog dialog({
        .owner = GetHwnd(), .title = Tr(L"Recovery Manager"),
        .initial_client_size = {620.0_dip, 340.0_dip}, .resizable = true,
        .callbacks = {
            .initialize = [&](HWND window) {
                mwfl::ControlHost ui{window};
                ui.Add(list, {501}, {});
                ui.Add(details, {502}, Tr(L"Local snapshots, newest first"));
                ui.Add(restore, {503}, Tr(L"Restore as new document"));
                ui.Add(remove, {504}, Tr(L"Delete snapshot"));
                ui.Add(close, {IDCANCEL}, Tr(L"Close"));
                for (const auto& item : snapshots) {
                    const auto source = item.original_path.empty() ? Tr(L"Unsaved") : item.original_path.wstring();
                    list.AddItem(item.title + L"  —  " + source + L"  (" +
                                 std::to_wstring(item.size) + Tr(L" bytes)"));
                }
                if (!snapshots.empty()) list.SetSelection(0);
                mwfl::SetAccessibleName(list.GetHwnd(), Tr(L"Recovery snapshots").c_str());
                return pointer->SetLayout(mwfl::Column().Margin(10.0_dip).Gap(6.0_dip)
                    .Add(list, mwfl::Stretch()).Add(details, mwfl::Fixed(24.0_dip))
                    .Add(mwfl::Row().Gap(6.0_dip).Add(restore, mwfl::Auto())
                        .Add(remove, mwfl::Auto()).Add(mwfl::Column(), mwfl::Stretch())
                        .Add(close, mwfl::Fixed(86.0_dip)), mwfl::Fixed(30.0_dip)));
            },
            .command = [&](HWND, WORD id, WORD) {
                if (id == 503) {
                    const auto selected = list.GetSelectedIndex();
                    if (selected && static_cast<std::size_t>(*selected) < snapshots.size()) {
                        chosen = static_cast<std::size_t>(*selected);
                        pointer->Accept();
                        return true;
                    }
                } else if (id == 504) {
                    const auto selected = list.GetSelectedIndex();
                    if (selected && static_cast<std::size_t>(*selected) < snapshots.size() &&
                        recovery_store_->Remove(snapshots[static_cast<std::size_t>(*selected)])) {
                        snapshots.erase(snapshots.begin() + *selected);
                        list.RemoveItem(*selected);
                        if (!snapshots.empty()) list.SetSelection((std::min)(*selected,
                            static_cast<int>(snapshots.size() - 1)));
                    }
                    return true;
                }
                return false;
            }}});
    pointer = &dialog;
    if (!dialog.ShowModal() || !chosen || *chosen >= snapshots.size()) return;
    const auto text = recovery_store_->Load(snapshots[*chosen]);
    if (text) NewDocument(*text, Tr(L"Recovered — ") + snapshots[*chosen].title);
}

void MainWindow::ResolveSessionPath() {
    if (IsTestMode()) {
        session_path_ = std::filesystem::temp_directory_path() /
            (L"notepad-colon-gui-" + std::to_wstring(::GetCurrentProcessId()) + L".state");
        std::error_code ignored;
        std::filesystem::remove(session_path_, ignored);
        recovery_store_ = std::make_unique<notepad_colon::RecoveryStore>(
            std::filesystem::temp_directory_path() /
            (L"notepad-colon-gui-recovery-" + std::to_wstring(::GetCurrentProcessId())), 10);
        macros_path_ = std::filesystem::temp_directory_path() /
            (L"notepad-colon-gui-macros-" + std::to_wstring(::GetCurrentProcessId()) + L".state");
        configuration_path_ = std::filesystem::temp_directory_path() /
            (L"notepad-colon-gui-config-" + std::to_wstring(::GetCurrentProcessId()) + L".state");
        workspace_catalog_path_ = std::filesystem::temp_directory_path() /
            (L"notepad-colon-gui-workspaces-" + std::to_wstring(::GetCurrentProcessId()) + L".state");
        return;
    }
    wchar_t local_app_data[32768]{};
    const DWORD length = ::GetEnvironmentVariableW(
        L"LOCALAPPDATA", local_app_data, static_cast<DWORD>(std::size(local_app_data)));
    if (length > 0 && length < std::size(local_app_data))
        session_path_ = std::filesystem::path{local_app_data} / L"mwfl" /
            L"Notepad Colon" / L"session.state";
    if (!session_path_.empty())
        recovery_store_ = std::make_unique<notepad_colon::RecoveryStore>(
            session_path_.parent_path() / L"Recovery", 50);
    if (!session_path_.empty()) {
        workspace_catalog_path_ = session_path_.parent_path() / L"workspaces.state";
        static_cast<void>(notepad_colon::LoadWorkspaceCatalog(
            workspace_catalog_path_, workspace_catalog_));
        macros_path_ = session_path_.parent_path() / L"macros.state";
        static_cast<void>(notepad_colon::LoadMacros(macros_path_, saved_macros_));
        configuration_path_ = session_path_.parent_path() / L"settings.npcconfig";
        std::ifstream input(configuration_path_, std::ios::binary);
        const std::string encoded{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        notepad_colon::Configuration configuration;
        if (!encoded.empty() && notepad_colon::DeserializeConfiguration(encoded, configuration)) {
            preferences_ = configuration.preferences;
            search_history_ = configuration.search_history;
            ApplyAppearance();
            static_cast<void>(ApplyShortcuts(configuration.shortcuts, false));
        }
    }
}

std::optional<notepad_colon::Session> MainWindow::CaptureSession() {
    notepad_colon::Session session;
    session.workspace_paths = workspace_catalog_.Roots();
    if (!session.workspace_paths.empty()) session.workspace_path = session.workspace_paths.front();
    if (const auto active = workspace_.GetActiveId()) {
        const auto index = workspace_.FindIndex(*active);
        if (index) session.active_index = *index;
    }
    for (const auto& metadata : workspace_.GetDocuments()) {
        const auto* document = FindDocument(metadata.id);
        if (!document) return std::nullopt;
        const auto text = document->editor->GetText();
        if (!text) return std::nullopt;
        notepad_colon::SessionEntry entry;
        entry.path = metadata.path;
        entry.recovery_text = metadata.dirty || metadata.path.empty() ? *text : L"";
        entry.encoding = document->detected_encoding == notepad_colon::EncodingKind::ansi
            ? notepad_colon::Encoding::ansi : ToSessionEncoding(document->encoding);
        entry.line_ending = document->line_ending;
        const auto selection = document->editor->GetSelection();
        entry.view.anchor = selection.start;
        entry.view.caret = selection.end;
        entry.dirty = metadata.dirty;
        session.documents.push_back(std::move(entry));
    }
    return session;
}

bool MainWindow::SaveSessionSnapshot() {
    if (session_path_.empty() || restoring_session_) return true;
    const auto session = CaptureSession();
    if (!session) return false;
    session_writer_.Queue(session_path_, std::move(*session));
    session_snapshot_pending_ = false;
    return true;
}

void MainWindow::QueueSessionSnapshotIfDue() {
    if (!session_snapshot_pending_ || restoring_session_ ||
        std::chrono::steady_clock::now() < session_snapshot_due_) return;
    static_cast<void>(SaveSessionSnapshot());
}

void MainWindow::SaveNamedSession() {
    const auto selected = mwfl::ShowSaveFileDialog({.owner = GetHwnd(), .title = Tr(L"Save Named Session"),
        .filters = {{Tr(L"Notepad:: session"), L"*.npcsession"}},
        .default_extension = L"npcsession"});
    if (!selected.accepted) return;
    const auto session = CaptureSession();
    status_.SetText(session && notepad_colon::SaveSessionAtomic(selected.path, *session)
        ? Tr(L"Named session saved") : Tr(L"Named session could not be saved"));
}

void MainWindow::OpenNamedSession() {
    const auto selected = mwfl::ShowOpenFileDialog({.owner = GetHwnd(), .title = Tr(L"Open Named Session"),
        .filters = {{Tr(L"Notepad:: session"), L"*.npcsession"}}});
    if (!selected.accepted) return;
    notepad_colon::Session session;
    if (!notepad_colon::LoadSession(selected.path, session)) {
        ::MessageBoxW(GetHwnd(), Tr(L"This session file is invalid or unreadable.").c_str(), Tr(L"Open Session").c_str(),
                      MB_OK | MB_ICONERROR); return;
    }
    for (const auto& root : session.workspace_paths)
        if (std::filesystem::is_directory(root)) static_cast<void>(workspace_catalog_.AddRoot(root));
    if (!workspace_catalog_.Roots().empty()) StartWorkspaceScan(workspace_catalog_.Roots().back());
    for (const auto& entry : session.documents) {
        if (!entry.path.empty() && OpenPath(entry.path)) {
            if (entry.dirty && !entry.recovery_text.empty()) {
                auto* document = ActiveDocument();
                if (document && document->editor->SetText(entry.recovery_text)) {
                    workspace_.SetDirty(document->id, true);
                    document->editor->SetSelection({entry.view.anchor, entry.view.caret});
                }
            }
        } else if (!entry.recovery_text.empty()) NewDocument(entry.recovery_text, Tr(L"Recovered session note"));
    }
    status_.SetText(Tr(L"Named session opened"));
}

bool MainWindow::RestoreSession() {
    if (session_path_.empty()) return false;
    notepad_colon::Session session;
    if (!notepad_colon::LoadSession(session_path_, session) || session.documents.empty()) return false;
    const auto roots = !session.workspace_paths.empty()
        ? session.workspace_paths : std::vector<std::filesystem::path>{session.workspace_path};
    for (const auto& root : roots)
        if (!root.empty() && std::filesystem::is_directory(root)) static_cast<void>(workspace_catalog_.AddRoot(root));
    if (!workspace_catalog_.Roots().empty()) StartWorkspaceScan(workspace_catalog_.Roots().back());
    restoring_session_ = true;
    for (const auto& entry : session.documents) {
        if (!entry.path.empty() && OpenPath(entry.path)) {
            auto* document = ActiveDocument();
            if (document && entry.dirty && !entry.recovery_text.empty()) {
                document->editor->SetText(entry.recovery_text);
                document->encoding = FromSessionEncoding(entry.encoding);
                document->detected_encoding = entry.encoding == notepad_colon::Encoding::ansi
                    ? notepad_colon::EncodingKind::ansi
                    : static_cast<notepad_colon::EncodingKind>(entry.encoding);
                document->line_ending = entry.line_ending;
                workspace_.SetDirty(document->id, true);
                document->editor->SetSelection({entry.view.anchor, entry.view.caret});
            }
        } else if (entry.path.empty() || !entry.recovery_text.empty()) {
            NewDocument(entry.recovery_text,
                        entry.path.empty() ? std::wstring{} : entry.path.filename().wstring());
            auto* document = ActiveDocument();
            if (document) {
                if (!entry.path.empty())
                    workspace_.Rename(document->id, entry.path.filename().wstring(), entry.path);
                document->encoding = FromSessionEncoding(entry.encoding);
                document->detected_encoding = entry.encoding == notepad_colon::Encoding::ansi
                    ? notepad_colon::EncodingKind::ansi
                    : static_cast<notepad_colon::EncodingKind>(entry.encoding);
                document->line_ending = entry.line_ending;
                if (entry.dirty) workspace_.SetDirty(document->id, true);
                document->editor->SetSelection({entry.view.anchor, entry.view.caret});
            }
        }
    }
    if (session.active_index < workspace_.GetDocuments().size())
        workspace_.Activate(workspace_.GetDocuments()[session.active_index].id);
    restoring_session_ = false;
    if (documents_.empty()) return false;
    SynchronizeTabs(true);
    SyncPresentation(Tr(L"Session restored"));
    return true;
}

}  // namespace notepad_colon::app
