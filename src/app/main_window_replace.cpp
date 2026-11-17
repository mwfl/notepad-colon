#include "main_window.h"

// Replace in Files. Files on disk are rewritten by a cancellable background
// worker that preserves each file's encoding; documents that are open in the
// editor are changed in their buffers instead, so the edit stays undoable and
// unsaved work is never overwritten behind the user's back.
namespace notepad_colon::app {

bool MainWindow::PromptReplaceInFiles(std::wstring& query, std::wstring& replacement, bool& backups) {
    mwfl::Label query_label, replacement_label, include_label, exclude_label, hint;
    mwfl::TextBox query_box, replacement_box, include_box, exclude_box;
    mwfl::CheckBox backup_box;
    mwfl::Button replace_button, cancel;
    bool accepted = false;
    mwfl::Dialog* pointer = nullptr;
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Replace in Files"),
        .initial_client_size = {580.0_dip, 380.0_dip}, .resizable = false,
        .callbacks = {.initialize = [&](HWND window) { DarkenDialog(window);
            mwfl::ControlHost ui{window};
            ui.Add(query_label, {760}, Tr(L"Find")); ui.Add(query_box, {761}, query);
            ui.Add(replacement_label, {762}, Tr(L"Replace with"));
            ui.Add(replacement_box, {763}, replacement);
            ui.Add(include_label, {764}, Tr(L"Include files (semicolon-separated globs)"));
            ui.Add(include_box, {765}, search_include_globs_);
            ui.Add(exclude_label, {766}, Tr(L"Exclude files (semicolon-separated globs)"));
            ui.Add(exclude_box, {767}, search_exclude_globs_);
            ui.Add(backup_box, {768}, Tr(L"Keep a .bak copy of every changed file"));
            backup_box.SetChecked(backups);
            ui.Add(hint, {769},
                Tr(L"Review and select files before replacing. Recovery copies are always kept."));
            ui.Add(replace_button, {IDOK}, Tr(L"Preview")); ui.Add(cancel, {IDCANCEL}, Tr(L"Cancel"));
            mwfl::SetAccessibleName(query_box.GetHwnd(), Tr(L"Text to find in files").c_str());
            mwfl::SetAccessibleName(replacement_box.GetHwnd(), Tr(L"Replacement text").c_str());
            auto rows = mwfl::Column().Margin(10.0_dip).Gap(5.0_dip)
                .Add(query_label, mwfl::Fixed(20.0_dip)).Add(query_box, mwfl::Fixed(30.0_dip))
                .Add(replacement_label, mwfl::Fixed(20.0_dip)).Add(replacement_box, mwfl::Fixed(30.0_dip))
                .Add(include_label, mwfl::Fixed(20.0_dip)).Add(include_box, mwfl::Fixed(30.0_dip))
                .Add(exclude_label, mwfl::Fixed(20.0_dip)).Add(exclude_box, mwfl::Fixed(30.0_dip))
                .Add(backup_box, mwfl::Fixed(28.0_dip))
                .Add(hint, mwfl::Fixed(22.0_dip))
                .Add(mwfl::Row().Gap(6.0_dip).Add(mwfl::Column(), mwfl::Stretch())
                    .Add(replace_button, mwfl::Fixed(90.0_dip))
                    .Add(cancel, mwfl::Fixed(90.0_dip)), mwfl::Fixed(30.0_dip));
            query_box.Focus(); return pointer->SetLayout(std::move(rows));
        }, .command = [&](HWND, WORD id, WORD) {
            if (id != IDOK) return false;
            query = query_box.GetText();
            replacement = replacement_box.GetText();
            search_include_globs_ = include_box.GetText();
            search_exclude_globs_ = exclude_box.GetText();
            backups = backup_box.IsChecked();
            if (query.empty()) return true;
            accepted = true; pointer->Accept(); return true;
        }}});
    pointer = &dialog; static_cast<void>(dialog.ShowModal()); return accepted;
}

namespace {
struct ReplacementPreviewEvents {
    HWND list = nullptr;
    std::function<void()> changed;
};
LRESULT CALLBACK ReplacementPreviewSubclass(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR, DWORD_PTR data) {
    const auto* events = reinterpret_cast<ReplacementPreviewEvents*>(data);
    if (message == WM_NOTIFY && events && lparam) {
        const auto* header = reinterpret_cast<const NMHDR*>(lparam);
        if (header->hwndFrom == events->list && header->code == LVN_ITEMCHANGED) events->changed();
    }
    return ::DefSubclassProc(window, message, wparam, lparam);
}
std::wstring ReplacementTimestamp(const std::filesystem::path& directory) {
    const auto name = directory.filename().wstring();
    const auto ticks = std::wcstoull(name.c_str(), nullptr, 10);
    const FILETIME time{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    FILETIME local{}; SYSTEMTIME calendar{};
    if (!ticks || !::FileTimeToLocalFileTime(&time, &local) || !::FileTimeToSystemTime(&local, &calendar)) return name;
    wchar_t label[64]{};
    swprintf_s(label, L"%04u-%02u-%02u %02u:%02u:%02u", static_cast<unsigned>(calendar.wYear),
        static_cast<unsigned>(calendar.wMonth), static_cast<unsigned>(calendar.wDay),
        static_cast<unsigned>(calendar.wHour), static_cast<unsigned>(calendar.wMinute), static_cast<unsigned>(calendar.wSecond));
    return label;
}
std::wstring ReplacementStatus(const notepad_colon::ReviewedReplacement& entry) {
    using Progress = notepad_colon::ReplaceProgress;
    if (!entry.error.empty()) return entry.error;
    switch (entry.progress) {
    case Progress::ready: return entry.document ? Tr(L"Open document (unsaved)") : Tr(L"Ready");
    case Progress::writing: return Tr(L"Interrupted; review before restoring");
    case Progress::applied: return Tr(L"Applied");
    case Progress::restoring: return Tr(L"Restore interrupted; review again");
    case Progress::restored: return Tr(L"Restored");
    case Progress::conflict: return Tr(L"Changed since preview");
    case Progress::failed: return Tr(L"Not changed");
    }
    return {};
}
}

std::filesystem::path MainWindow::ReplacementHistoryPath() const {
    return IsTestMode() ? std::filesystem::path{session_path_.wstring() + L".replace-history"}
                        : session_path_.parent_path() / L"ReplaceHistory";
}

bool MainWindow::ReplacementOwnsPath(const std::filesystem::path& path) const {
    if (!pending_replace_.running || !pending_replace_.applying) return false;
    return std::ranges::any_of(pending_replace_.plan.files, [&](const auto& entry) {
        return entry.selected && !entry.document && (SamePath(entry.path, path) ||
            (pending_replace_.plan.backups && SamePath(entry.path.wstring() + L".bak", path)));
    });
}

bool MainWindow::ReviewReplacement(notepad_colon::ReplacePlan& plan, bool history) {
    mwfl::ListView files;
    mwfl::ScintillaEditor before, after;
    mwfl::Label before_label, after_label, summary, detail;
    mwfl::Button all, none, previous, next, apply, cancel, recover;
    bool accepted = false, refreshing = false, snapshots_valid = false;
    int current = -1;
    std::size_t match = 0;
    std::wstring left_text, right_text;
    mwfl::Dialog* pointer = nullptr;
    const auto go = [&] {
        if (current < 0 || static_cast<std::size_t>(current) >= plan.files.size()) return;
        const auto& spans = plan.files[current].spans;
        if (spans.empty() || match >= spans.size()) return;
        const auto& span = spans[match];
        const auto highlight = [&](mwfl::ScintillaEditor& editor, const std::wstring& text,
                                   std::size_t start, std::size_t length) {
            if (start > text.size() || length > text.size() - start) return;
            const auto prefix = mwfl::ToUtf8(std::wstring_view{text}.substr(0, start));
            const auto changed = mwfl::ToUtf8(std::wstring_view{text}.substr(start, length));
            if (!prefix || !changed) return;
            editor.Send(SCI_SETSEL, prefix->size(), prefix->size() + changed->size());
            editor.Send(SCI_SCROLLCARET);
        };
        highlight(before, left_text, span.before_start, span.before_length);
        highlight(after, right_text, span.after_start, span.after_length);
        detail.SetText(plan.files[current].path.wstring() + L" | " +
            std::to_wstring(match + 1) + L" / " + std::to_wstring(plan.files[current].replacements) +
            (plan.files[current].replacements > spans.size() ? Tr(L" (navigation is limited; full text is shown)") : L""));
    };
    const auto refresh = [&] {
        if (refreshing) return;
        refreshing = true;
        std::size_t selected = 0, replacements = 0;
        for (std::size_t index = 0; index < plan.files.size(); ++index) {
            auto& entry = plan.files[index];
            entry.selected = ListView_GetCheckState(files.GetHwnd(), static_cast<int>(index)) != FALSE;
            if (!history && entry.progress != notepad_colon::ReplaceProgress::ready && entry.selected) {
                entry.selected = false;
                ListView_SetCheckState(files.GetHwnd(), static_cast<int>(index), FALSE);
            }
            if (entry.selected) { ++selected; replacements += entry.replacements; }
        }
        summary.SetText(std::to_wstring(selected) + Tr(L" selected file(s), ") +
            std::to_wstring(replacements) + Tr(L" replacement(s)") +
            (plan.truncated ? Tr(L" | Preview limit reached; only listed files can change") : L"") +
            L" | " + std::to_wstring(plan.files_skipped) + Tr(L" file(s) skipped"));
        const auto row = ListView_GetNextItem(files.GetHwnd(), -1, LVNI_SELECTED);
        if (row != current && row >= 0) {
            current = row; match = 0;
            auto left = notepad_colon::ReadReplacementSnapshot(plan, row, false);
            auto right = notepad_colon::ReadReplacementSnapshot(plan, row, true);
            snapshots_valid = left && right;
            left_text = left.value_or(L""); right_text = right.value_or(L"");
            before.SetReadOnly(false); after.SetReadOnly(false);
            notepad_colon::SetEditorTextBaseline(before, left_text);
            notepad_colon::SetEditorTextBaseline(after, right_text);
            before.SetReadOnly(true); after.SetReadOnly(true);
            detail.SetText(snapshots_valid ? plan.files[row].path.wstring() : Tr(L"Snapshot integrity check failed; this file cannot be recovered"));
            if (snapshots_valid) {
                if (plan.files[row].spans.empty() && plan.files[row].replacements)
                    detail.SetText(plan.files[row].path.wstring() + Tr(L" (navigation is limited; full text is shown)"));
                else go();
            }
        }
        ::EnableWindow(apply.GetHwnd(), selected != 0 && (!history || plan.started));
        ::EnableWindow(recover.GetHwnd(), current >= 0 && snapshots_valid);
        refreshing = false;
    };
    ReplacementPreviewEvents events;
    events.changed = refresh;
    mwfl::Dialog dialog({.owner = self_test_ ? nullptr : GetHwnd(),
        .title = history ? Tr(L"Review Replacement History") : Tr(L"Review Replacement"),
        .initial_client_size = {1180.0_dip, 620.0_dip}, .resizable = true,
        .callbacks = {.initialize = [&](HWND window) { DarkenDialog(window);
            mwfl::ControlHost ui{window};
            if (!files.Create(window, {770}, {}) || !before.Create(window, {771}, {}, runtime_) ||
                !after.Create(window, {772}, {}, runtime_)) return false;
            files.SetExtendedListStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            files.AddColumn(Tr(L"File"), 250); files.AddColumn(Tr(L"Status"), 230);
            files.AddColumn(Tr(L"Matches"), 80);
            for (std::size_t index = 0; index < plan.files.size(); ++index) {
                const mwfl::ListItemId id{index + 1};
                const auto& entry = plan.files[index];
                files.AddItem(id, entry.path.filename().wstring());
                files.SetItemText(id, 1, ReplacementStatus(entry));
                files.SetItemText(id, 2, std::to_wstring(entry.replacements));
                ListView_SetCheckState(files.GetHwnd(), static_cast<int>(index), entry.selected);
            }
            for (auto* editor : {&before, &after}) {
                editor->ConfigureCodeEditing();
                notepad_colon::ApplyPreferences(*editor, preferences_, IsDark());
                editor->SetReadOnly(true);
            }
            ui.Add(before_label, {773}, Tr(L"Before replacement"));
            ui.Add(after_label, {774}, Tr(L"After replacement"));
            ui.Add(summary, {775}, L""); ui.Add(detail, {776}, L"");
            ui.Add(all, {777}, Tr(L"Select all")); ui.Add(none, {778}, Tr(L"Select none"));
            ui.Add(previous, {779}, Tr(L"Previous match")); ui.Add(next, {780}, Tr(L"Next match"));
            ui.Add(recover, {781}, Tr(L"Recover before as new tab"));
            ui.Add(apply, {IDOK}, history ? Tr(L"Restore selected files") : Tr(L"Replace selected files"));
            ui.Add(cancel, {IDCANCEL}, Tr(L"Close"));
            mwfl::SetAccessibleName(files.GetHwnd(), Tr(L"Files to include in the replacement").c_str());
            mwfl::SetAccessibleName(before.GetHwnd(), Tr(L"Before replacement").c_str());
            mwfl::SetAccessibleName(after.GetHwnd(), Tr(L"After replacement").c_str());
            events.list = files.GetHwnd();
            ::SetWindowSubclass(window, ReplacementPreviewSubclass, 1, reinterpret_cast<DWORD_PTR>(&events));
            const auto laid_out = pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(6.0_dip)
                .Add(summary, mwfl::Fixed(28.0_dip))
                .Add(files, mwfl::Fixed(155.0_dip))
                .Add(detail, mwfl::Fixed(26.0_dip))
                .Add(mwfl::Row().Gap(6.0_dip).Add(before_label, mwfl::Stretch())
                    .Add(after_label, mwfl::Stretch()), mwfl::Fixed(22.0_dip))
                .Add(mwfl::Row().Gap(6.0_dip).Add(before, mwfl::Stretch())
                    .Add(after, mwfl::Stretch()), mwfl::Stretch())
                .Add(mwfl::Row().Gap(6.0_dip).Add(all, ButtonWidth(Tr(L"Select all")))
                    .Add(none, ButtonWidth(Tr(L"Select none")))
                    .Add(previous, ButtonWidth(Tr(L"Previous match"))).Add(next, ButtonWidth(Tr(L"Next match")))
                    .Add(recover, ButtonWidth(Tr(L"Recover before as new tab"))), mwfl::Fixed(30.0_dip))
                .Add(mwfl::Row().Gap(6.0_dip).Add(mwfl::Column(), mwfl::Stretch())
                    .Add(apply, ButtonWidth(history ? Tr(L"Restore selected files") : Tr(L"Replace selected files")))
                    .Add(cancel, mwfl::Fixed(90.0_dip)), mwfl::Fixed(30.0_dip)));
            if (!plan.files.empty()) files.SetSelected({1});
            refresh(); files.Focus();
            return laid_out;
        }, .command = [&](HWND, WORD id, WORD) {
            if (id == 777 || id == 778) {
                refreshing = true;
                for (std::size_t index = 0; index < plan.files.size(); ++index)
                    ListView_SetCheckState(files.GetHwnd(), static_cast<int>(index), id == 777);
                refreshing = false; refresh(); return true;
            }
            if (id == 779 || id == 780) {
                if (current >= 0 && !plan.files[current].spans.empty()) {
                    const auto count = plan.files[current].spans.size();
                    match = (match + count + (id == 780 ? 1 : count - 1)) % count;
                    go();
                }
                return true;
            }
            if (id == 781 && current >= 0) {
                if (const auto text = notepad_colon::ReadReplacementSnapshot(plan, current, false))
                    NewDocument(*text, Tr(L"Recovered — ") + plan.files[current].path.filename().wstring());
                return true;
            }
            if (id != IDOK) return false;
            refresh(); accepted = true; pointer->Accept(); return true;
        }}});
    pointer = &dialog;
    static_cast<void>(dialog.ShowModal());
    return accepted;
}

void MainWindow::StartReplaceInFiles() {
    if (pending_replace_.running) { status_.SetText(Tr(L"Wait for the current replacement to finish")); return; }
    if (workspace_catalog_.Roots().empty()) { OpenFolderInteractive(); return; }
    auto query = search_.GetText(); auto replacement = replacement_.GetText();
    bool backups = preferences_.create_backup_before_save;
    if (!PromptReplaceInFiles(query, replacement, backups)) return;
    search_.SetText(query); replacement_.SetText(replacement); RememberSearch(query);
    BeginReplaceInFiles(std::move(query), std::move(replacement), backups);
}

void MainWindow::BeginReplaceInFiles(std::wstring query, std::wstring replacement, bool backups) {
    if (pending_replace_.running) return;
    pending_replace_ = {};
    pending_replace_.query = std::move(query); pending_replace_.replacement = std::move(replacement);
    pending_replace_.options.search = {
        .include_globs = SplitGlobs(search_include_globs_), .exclude_globs = SplitGlobs(search_exclude_globs_),
        .match_case = search_match_case_, .whole_word = search_whole_word_,
        .regular_expression = search_regex_, .multiline = true, .use_gitignore = true};
    pending_replace_.options.create_backups = backups;
    for (auto& document : documents_) {
        const auto* metadata = workspace_.Find(document.id);
        if (!metadata || metadata->path.empty()) continue;
        pending_replace_.options.excluded_paths.push_back(metadata->path);
        if (document.read_only || document.large_buffer) continue;
        bool eligible = false;
        for (const auto& root : workspace_catalog_.Roots()) {
            if (notepad_colon::IsWithinWorkspaceRoots(metadata->path, {root}) &&
                notepad_colon::MatchesWorkspaceGlobs(metadata->path.lexically_relative(root),
                    pending_replace_.options.search.include_globs, pending_replace_.options.search.exclude_globs)) {
                eligible = true; break;
            }
        }
        if (!eligible) continue;
        if (auto text = document.editor->GetText())
            pending_replace_.buffers.push_back({metadata->path, document.id.value, document.revision, std::move(*text)});
    }
    RunReplacePhase(false);
}

void MainWindow::RunReplacePhase(bool apply) {
    for (auto& worker : search_workers_) worker.thread.request_stop();
    ReapCompletedWorkers(search_workers_);
    search_operations_.Begin();
    const auto generation = replace_operations_.Begin();
    pending_replace_.applying = apply; pending_replace_.running = true;
    pending_replace_.cancel_requested = false;
    if (auto* command = commands_.Find(kCancelSearch)) { command->SetEnabled(true); menu_.UpdateCommand(*command); }
    status_.SetText(apply ? Tr(L"Applying reviewed replacement...") : Tr(L"Preparing replacement preview..."));
    const HWND window = GetHwnd();
    const auto roots = workspace_catalog_.Roots();
    const auto history = ReplacementHistoryPath();
    auto done = std::make_shared<std::atomic_bool>(false);
    search_workers_.push_back({done, std::jthread([this, window, roots, history, generation, apply, done,
        options = pending_replace_.options, buffers = std::move(pending_replace_.buffers),
        query = pending_replace_.query, replacement = pending_replace_.replacement,
        plan = pending_replace_.plan, restore = pending_replace_.restoring](std::stop_token stop) mutable {
        try {
            if (!apply) plan = notepad_colon::PreviewWorkspaceReplacement(roots, query, replacement, options, buffers, history, stop);
            else if (restore) notepad_colon::RestoreReplacePlan(plan, stop);
            else notepad_colon::ApplyReplacePlan(plan, stop);
        } catch (...) { plan.error = Tr(L"The replacement operation failed; review its history before retrying"); }
        if (replace_operations_.Publish(generation, std::move(plan)))
            ::PostMessageW(window, kReplaceCompleteMessage, static_cast<WPARAM>(generation), 0);
        done->store(true);
    })});
}

bool MainWindow::ValidateReplacementBuffers(notepad_colon::ReplacePlan& plan) {
    bool valid = true;
    for (std::size_t index = 0; index < plan.files.size(); ++index) {
        auto& entry = plan.files[index];
        if (!entry.selected) continue;
        if (!entry.document) {
            if (std::ranges::any_of(workspace_.GetDocuments(), [&](const auto& metadata) {
                return !metadata.path.empty() && (SamePath(metadata.path, entry.path) ||
                    (plan.backups && SamePath(metadata.path, entry.path.wstring() + L".bak")));
            })) {
                entry.progress = notepad_colon::ReplaceProgress::conflict;
                entry.error = Tr(L"The target or its adjacent backup is now open in the editor. Preview again");
                valid = false;
            }
            continue;
        }
        const auto found = std::ranges::find_if(documents_, [&](const auto& document) { return document.id.value == entry.document; });
        const auto before = notepad_colon::ReadReplacementSnapshot(plan, index, false);
        const auto* metadata = found == documents_.end() ? nullptr : workspace_.Find(found->id);
        if (found == documents_.end() || !metadata || !SamePath(metadata->path, entry.path) ||
            found->read_only || found->large_buffer || found->revision != entry.revision || !before ||
            found->editor->GetText() != before) {
            entry.progress = notepad_colon::ReplaceProgress::conflict;
            entry.error = Tr(L"The open document changed after preview; it was not replaced"); valid = false;
        }
    }
    return valid;
}

void MainWindow::ApplyReplacementBuffers(notepad_colon::ReplacePlan& plan) {
    static_cast<void>(ValidateReplacementBuffers(plan));
    for (std::size_t index = 0; index < plan.files.size(); ++index) {
        auto& entry = plan.files[index];
        if (!entry.selected || !entry.document || entry.progress != notepad_colon::ReplaceProgress::ready) continue;
        auto found = std::ranges::find_if(documents_, [&](const auto& document) { return document.id.value == entry.document; });
        const auto after = notepad_colon::ReadReplacementSnapshot(plan, index, true);
        if (found == documents_.end() || !after) continue;
        entry.progress = notepad_colon::ReplaceProgress::writing;
        if (!notepad_colon::SaveReplacePlan(plan)) {
            plan.error = Tr(L"The replacement journal could not be saved; no further files were changed"); break;
        }
        if (notepad_colon::ReplaceDocumentText(*found->editor, *after, found->editor->GetSelection())) {
            entry.progress = notepad_colon::ReplaceProgress::applied;
            ++pending_replace_.open_documents_changed; pending_replace_.open_replacements += entry.replacements;
        } else { entry.progress = notepad_colon::ReplaceProgress::failed; entry.error = Tr(L"The open document could not be changed"); }
        if (!notepad_colon::SaveReplacePlan(plan)) {
            plan.error = Tr(L"The replacement journal could not be saved; no further files were changed"); break;
        }
    }
    static_cast<void>(SaveSessionSnapshot());
}

void MainWindow::CompleteReplace() {
    auto result = replace_operations_.TakeCurrent();
    if (!result) return;
    if (pending_replace_.cancel_requested) {
        result->cancelled = true;
        if (!result->directory.empty()) static_cast<void>(notepad_colon::SaveReplacePlan(*result));
    }
    pending_replace_.running = false;
    if (auto* command = commands_.Find(kCancelSearch)) { command->SetEnabled(false); menu_.UpdateCommand(*command); }
    if (!pending_replace_.applying) {
        pending_replace_.plan = std::move(*result);
        auto& plan = pending_replace_.plan;
        if (!plan.error.empty()) { status_.SetText(plan.error); return; }
        if (plan.cancelled || plan.files.empty()) {
            if (!plan.directory.empty()) static_cast<void>(notepad_colon::DeleteReplacePlan(ReplacementHistoryPath(), plan.directory));
            status_.SetText(plan.cancelled ? Tr(L"Replace in Files cancelled; nothing was changed") : Tr(L"Replace in Files: no matches")); return;
        }
        const bool visual_test = self_test_ &&
            ::GetEnvironmentVariableW(L"NOTEPAD_COLON_REPLACE_PREVIEW_TEST", nullptr, 0) != 0;
        if (visual_test) { ::ShowWindow(GetHwnd(), SW_SHOW); SetUiLanguage(notepad_colon::UiLanguage::chinese_simplified); }
        const bool accepted = (IsTestMode() && !visual_test) || ReviewReplacement(plan);
        if (visual_test) { SetUiLanguage(notepad_colon::UiLanguage::english); ::SetEnvironmentVariableW(L"NOTEPAD_COLON_REPLACE_PREVIEW_TEST", nullptr); }
        if (!accepted) {
            static_cast<void>(notepad_colon::SaveReplacePlan(plan));
            status_.SetText(Tr(L"Replace in Files cancelled; nothing was changed")); return;
        }
        if (!ValidateReplacementBuffers(plan)) {
            static_cast<void>(notepad_colon::SaveReplacePlan(plan));
            status_.SetText(Tr(L"The preview is out of date; nothing was changed. Preview again")); return;
        }
        if (!std::ranges::any_of(plan.files, [](const auto& entry) { return entry.selected; })) return;
        RunReplacePhase(true); return;
    }
    pending_replace_.plan = std::move(*result);
    auto& plan = pending_replace_.plan;
    if (!pending_replace_.restoring && plan.started && !plan.preflight_failed && plan.error.empty() && !plan.cancelled)
        ApplyReplacementBuffers(plan);
    notepad_colon::ReplaceResult outcome;
    outcome.cancelled = plan.cancelled;
    for (const auto& entry : plan.files) {
        if (!entry.selected || (pending_replace_.restoring && entry.document)) continue;
        const bool changed = pending_replace_.restoring ? entry.progress == notepad_colon::ReplaceProgress::restored
                                                       : entry.progress == notepad_colon::ReplaceProgress::applied;
        outcome.files.push_back({entry.path, entry.replacements, changed, ReplacementStatus(entry)});
        if (changed && !entry.document) { ++outcome.files_changed; outcome.replacements += entry.replacements; }
        else if (!changed) ++outcome.files_failed;
    }
    ShowReplaceOutcome(outcome);
    if (plan.preflight_failed) status_.SetText(Tr(L"The preview is out of date; nothing was changed. Preview again"));
    else if (!plan.error.empty()) status_.SetText(plan.error);
    else if (pending_replace_.restoring) status_.SetText(Tr(L"Restore finished; review per-file results in Replacement History"));
}

void MainWindow::ShowReplacementHistory() {
    if (pending_replace_.running) { status_.SetText(Tr(L"Wait for the current replacement to finish")); return; }
    auto plans = notepad_colon::LoadReplacementHistory(ReplacementHistoryPath());
    if (plans.empty()) { status_.SetText(Tr(L"No replacement history")); return; }
    mwfl::ListBox list; mwfl::Button review, remove, close; mwfl::Label hint;
    std::optional<std::size_t> chosen;
    mwfl::Dialog* pointer = nullptr;
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Replacement History"),
        .initial_client_size = {760.0_dip, 400.0_dip}, .resizable = true,
        .callbacks = {.initialize = [&](HWND window) { DarkenDialog(window);
            mwfl::ControlHost ui{window}; ui.Add(list, {790}, mwfl::RectDip{}, mwfl::ListBoxOptions{});
            for (const auto& plan : plans) {
                std::size_t changed = 0;
                for (const auto& entry : plan.files)
                    if (entry.progress == notepad_colon::ReplaceProgress::applied ||
                        entry.progress == notepad_colon::ReplaceProgress::writing ||
                        entry.progress == notepad_colon::ReplaceProgress::restoring) ++changed;
                list.AddItem(ReplacementTimestamp(plan.directory) + L" | " + plan.query.substr(0, 60) + L" → " +
                    plan.replacement.substr(0, 60) + L" | " + std::to_wstring(changed) + Tr(L" changed file(s)"));
            }
            list.SetSelection(0);
            ui.Add(hint, {791}, Tr(L"Restore checks for newer changes. Open-document originals can be recovered into new tabs."));
            ui.Add(review, {IDOK}, Tr(L"Review...")); ui.Add(remove, {792}, Tr(L"Delete record...")); ui.Add(close, {IDCANCEL}, Tr(L"Close"));
            return pointer->SetLayout(mwfl::Column().Margin(10.0_dip).Gap(6.0_dip)
                .Add(list, mwfl::Stretch()).Add(hint, mwfl::Fixed(35.0_dip))
                .Add(mwfl::Row().Gap(6.0_dip).Add(review, ButtonWidth(Tr(L"Review...")))
                    .Add(remove, ButtonWidth(Tr(L"Delete record..."))).Add(mwfl::Column(), mwfl::Stretch())
                    .Add(close, mwfl::Fixed(90.0_dip)), mwfl::Fixed(30.0_dip)));
        }, .command = [&](HWND, WORD id, WORD) {
            if (id != IDOK && id != 792) return false;
            const auto row = list.GetSelection();
            if (row < 0 || static_cast<std::size_t>(row) >= plans.size()) return true;
            if (id == IDOK) { chosen = static_cast<std::size_t>(row); pointer->Accept(); return true; }
            if (::MessageBoxW(pointer->GetHwnd(), Tr(L"Delete this operation and its recovery copies? This cannot be undone.").c_str(),
                Tr(L"Replacement History").c_str(), MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK &&
                notepad_colon::DeleteReplacePlan(ReplacementHistoryPath(), plans[row].directory)) {
                plans.erase(plans.begin() + row); list.RemoveItem(row);
                if (!plans.empty()) list.SetSelection(0);
            }
            return true;
        }}});
    pointer = &dialog; static_cast<void>(dialog.ShowModal());
    if (!chosen) return;
    auto plan = std::move(plans[*chosen]);
    if (!ReviewReplacement(plan, true)) return;
    // A file already open in the editor must be closed first. This keeps an
    // old buffer from being saved over a just-restored disk version.
    for (auto& entry : plan.files) {
        if (!entry.selected || entry.document) continue;
        if (std::ranges::any_of(workspace_.GetDocuments(), [&](const auto& metadata) {
                return !metadata.path.empty() && SamePath(metadata.path, entry.path);
            })) {
            entry.selected = false; entry.error = Tr(L"Close this file in the editor before restoring it");
        }
    }
    // Buffers are recovered into new unnamed documents, never written to disk.
    for (std::size_t index = 0; index < plan.files.size(); ++index) {
        auto& entry = plan.files[index];
        if (!entry.selected || !entry.document) continue;
        if (const auto text = notepad_colon::ReadReplacementSnapshot(plan, index, false))
            NewDocument(*text, Tr(L"Recovered — ") + entry.path.filename().wstring());
    }
    pending_replace_ = {}; pending_replace_.restoring = true;
    pending_replace_.plan = std::move(plan);
    RunReplacePhase(true);
}

void MainWindow::ShowReplaceOutcome(const notepad_colon::ReplaceResult& result) {
    // Reuse the search-results list: one row per changed or failed file.
    search_results_ = {};
    ListView_DeleteAllItems(results_.GetHwnd());
    for (std::size_t index = 0; index < result.files.size(); ++index) {
        const auto& outcome = result.files[index];
        const mwfl::ListItemId id{index + 1};
        auto display_path = outcome.path;
        for (const auto& root : workspace_catalog_.Roots())
            if (notepad_colon::IsWithinWorkspaceRoots(outcome.path, {root})) {
                display_path = root.filename() / outcome.path.lexically_relative(root); break;
            }
        const auto summary = outcome.written
            ? std::to_wstring(outcome.replacements) + Tr(L" replacement(s)")
            : Tr(L"Not changed: ") + outcome.error;
        results_.AddItem(id, display_path.wstring());
        results_.SetItemText(id, 1, L"");
        results_.SetItemText(id, 2, L"");
        results_.SetItemText(id, 3, summary);
        search_results_.matches.push_back({outcome.path, 1, 1, summary});
    }
    results_visible_ = true;
    ApplyCompactLayout();
    auto summary = std::wstring(result.cancelled ? Tr(L"Replace in Files stopped | ") : Tr(L"Replace in Files | ")) +
        std::to_wstring(result.replacements + pending_replace_.open_replacements) +
        Tr(L" replacement(s) in ") +
        std::to_wstring(result.files_changed + pending_replace_.open_documents_changed) + Tr(L" file(s)");
    if (pending_replace_.open_documents_changed != 0)
        summary += L" (" + std::to_wstring(pending_replace_.open_documents_changed) +
                   Tr(L" open document(s) changed in the editor, unsaved)");
    if (result.files_failed != 0)
        summary += L" | " + std::to_wstring(result.files_failed) + Tr(L" file(s) could not be changed");
    status_.SetText(summary);
}

}  // namespace notepad_colon::app
