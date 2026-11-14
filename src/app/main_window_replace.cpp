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
        .callbacks = {.initialize = [&](HWND window) {
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
                Tr(L"Matches are counted first; nothing is written until you confirm."));
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

void MainWindow::StartReplaceInFiles() {
    if (workspace_catalog_.Roots().empty()) {
        OpenFolderInteractive();
        return;
    }
    auto query = search_.GetText();
    auto replacement = replacement_.GetText();
    bool backups = preferences_.create_backup_before_save;
    if (!PromptReplaceInFiles(query, replacement, backups)) return;
    search_.SetText(query);
    replacement_.SetText(replacement);
    RememberSearch(query);
    BeginReplaceInFiles(std::move(query), std::move(replacement), backups);
}

void MainWindow::BeginReplaceInFiles(std::wstring query, std::wstring replacement, bool backups) {
    pending_replace_ = {};
    pending_replace_.query = std::move(query);
    pending_replace_.replacement = std::move(replacement);
    pending_replace_.options.search = {
        .include_globs = SplitGlobs(search_include_globs_),
        .exclude_globs = SplitGlobs(search_exclude_globs_),
        .match_case = search_match_case_, .whole_word = search_whole_word_,
        .regular_expression = search_regex_, .multiline = true, .use_gitignore = true};
    pending_replace_.options.create_backups = backups;
    for (const auto& metadata : workspace_.GetDocuments())
        if (!metadata.path.empty()) pending_replace_.options.excluded_paths.push_back(metadata.path);
    RunReplacePhase(false);
}

void MainWindow::RunReplacePhase(bool apply) {
    for (auto& worker : search_workers_) worker.thread.request_stop();
    ReapCompletedWorkers(search_workers_);
    search_operations_.Begin();
    const auto generation = replace_operations_.Begin();
    pending_replace_.applying = apply;
    if (auto* command = commands_.Find(kCancelSearch)) {
        command->SetEnabled(true);
        menu_.UpdateCommand(*command);
    }
    status_.SetText(apply ? Tr(L"Replacing in files...") : Tr(L"Counting matches in files..."));
    const HWND window = GetHwnd();
    const auto roots = workspace_catalog_.Roots();
    auto options = pending_replace_.options;
    options.dry_run = !apply;
    auto done = std::make_shared<std::atomic_bool>(false);
    search_workers_.push_back({done, std::jthread(
        [this, window, roots, generation, options, done, query = pending_replace_.query,
         replacement = pending_replace_.replacement](std::stop_token stop) {
            notepad_colon::ReplaceResult total;
            for (const auto& root : roots) {
                if (stop.stop_requested()) { total.cancelled = true; break; }
                auto part = notepad_colon::ReplaceInWorkspace(root, query, replacement, options, stop);
                if (!part.error.empty()) { total.error = std::move(part.error); break; }
                total.files.insert(total.files.end(), std::make_move_iterator(part.files.begin()),
                                   std::make_move_iterator(part.files.end()));
                total.replacements += part.replacements;
                total.files_changed += part.files_changed;
                total.files_failed += part.files_failed;
                total.files_searched += part.files_searched;
                total.files_skipped += part.files_skipped;
                total.cancelled = total.cancelled || part.cancelled;
                total.truncated = total.truncated || part.truncated;
            }
            if (replace_operations_.Publish(generation, std::move(total)))
                ::PostMessageW(window, kReplaceCompleteMessage, static_cast<WPARAM>(generation), 0);
            done->store(true);
        })});
}

// Counts (apply = false) or performs (apply = true) the replacement in the
// buffers of open, named, editable documents. Returns {documents, matches}.
std::pair<std::size_t, std::size_t> MainWindow::ReplaceInOpenDocuments(bool apply) {
    std::size_t documents = 0, matches = 0;
    for (auto& document : documents_) {
        const auto* metadata = workspace_.Find(document.id);
        if (!metadata || metadata->path.empty() || document.read_only || document.large_buffer ||
            !notepad_colon::IsWithinWorkspaceRoots(metadata->path, workspace_catalog_.Roots()))
            continue;
        const auto text = document.editor->GetText();
        if (!text) continue;
        // The include/exclude globs apply to open documents exactly as they
        // do to files on disk.
        const auto& options = pending_replace_.options.search;
        bool eligible = false;
        for (const auto& root : workspace_catalog_.Roots()) {
            if (!notepad_colon::IsWithinWorkspaceRoots(metadata->path, {root})) continue;
            eligible = notepad_colon::MatchesWorkspaceGlobs(
                metadata->path.lexically_relative(root), options.include_globs, options.exclude_globs);
            break;
        }
        if (!eligible) continue;
        const auto replaced = notepad_colon::ReplaceAllInText(
            *text, pending_replace_.query, pending_replace_.replacement, pending_replace_.options.search);
        if (replaced.count == 0) continue;
        if (apply) {
            const auto selection = document.editor->GetSelection();
            if (!notepad_colon::ReplaceDocumentText(*document.editor, replaced.text, selection)) continue;
        }
        ++documents;
        matches += replaced.count;
    }
    return {documents, matches};
}

void MainWindow::CompleteReplace() {
    auto result = replace_operations_.TakeCurrent();
    if (!result) return;
    const auto disable_cancel = [&] {
        if (auto* command = commands_.Find(kCancelSearch)) {
            command->SetEnabled(false);
            menu_.UpdateCommand(*command);
        }
    };
    if (!result->error.empty()) { disable_cancel(); status_.SetText(result->error); return; }
    if (result->cancelled && !pending_replace_.applying) {
        disable_cancel(); status_.SetText(Tr(L"Replace in Files cancelled; nothing was changed")); return;
    }
    if (!pending_replace_.applying) {
        const auto [open_documents, open_matches] = ReplaceInOpenDocuments(false);
        const auto total = result->replacements + open_matches;
        const auto files = result->files.size() + open_documents;
        if (total == 0) {
            disable_cancel();
            status_.SetText(Tr(L"Replace in Files: no matches in ") +
                std::to_wstring(result->files_searched) + Tr(L" files"));
            return;
        }
        if (!IsTestMode()) {
            auto message = Tr(L"Replace ") + std::to_wstring(total) + Tr(L" occurrence(s) in ") +
                std::to_wstring(files) + Tr(L" file(s)?");
            if (open_documents != 0)
                message += L"\n\n" + std::to_wstring(open_documents) +
                    Tr(L" of them are open in the editor: they are changed in the editor (undoable) and stay unsaved.");
            message += pending_replace_.options.create_backups
                ? L"\n\n" + Tr(L"A .bak copy is kept next to every changed file on disk.")
                : L"\n\n" + Tr(L"Files on disk are overwritten without a backup.");
            if (::MessageBoxW(GetHwnd(), message.c_str(), Tr(L"Replace in Files").c_str(),
                              MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) {
                disable_cancel();
                status_.SetText(Tr(L"Replace in Files cancelled; nothing was changed"));
                return;
            }
        }
        const auto [changed_documents, changed_matches] = ReplaceInOpenDocuments(true);
        RunReplacePhase(true);
        pending_replace_.open_documents_changed = changed_documents;
        pending_replace_.open_replacements = changed_matches;
        return;
    }
    disable_cancel();
    ShowReplaceOutcome(*result);
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
