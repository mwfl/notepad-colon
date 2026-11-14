#include "main_window.h"

namespace notepad_colon::app {

void MainWindow::ShowGoToLineColumn() {
    const auto value = PromptWorkspaceName(Tr(L"Go to Line / Column"), L"1:1", false);
    auto* editor = ActiveEditor();
    if (!value || !editor) return;
    wchar_t* end = nullptr;
    const auto line = std::wcstoull(value->c_str(), &end, 10);
    std::uint64_t column = 1;
    if (end && (*end == L':' || *end == L',')) column = std::wcstoull(end + 1, nullptr, 10);
    if (line == 0 || column == 0) { status_.SetText(Tr(L"Line and column must be positive")); return; }
    const auto line_index = static_cast<LRESULT>((std::min<std::uint64_t>)(
        line - 1, static_cast<std::uint64_t>((std::max<LRESULT>)(0, editor->Send(SCI_GETLINECOUNT) - 1))));
    const auto position = editor->Send(SCI_FINDCOLUMN, line_index,
        static_cast<LPARAM>((std::min<std::uint64_t>)(column - 1, INT_MAX)));
    editor->SetSelection({position, position}); editor->Send(SCI_SCROLLCARET); editor->Focus();
    status_.SetText(Tr(L"Moved to line ") + std::to_wstring(line_index + 1));
}

void MainWindow::ShowQuickOpen() {
    std::vector<std::filesystem::path> paths;
    auto scans = workspace_scans_;
    if (workspace_lazy_) {
        scans.clear();
        for (const auto& root : workspace_catalog_.Roots())
            scans.emplace_back(root, notepad_colon::ScanWorkspace(root, 20000));
    }
    for (const auto& [root, scan] : scans)
        for (const auto& entry : scan.entries)
            if (!entry.directory) paths.push_back(root / entry.relative_path);
    if (paths.empty()) { status_.SetText(Tr(L"Open a workspace before Quick Open")); return; }
    mwfl::TextBox query; mwfl::ListBox list; mwfl::Button open, cancel;
    std::vector<std::size_t> visible; std::optional<std::size_t> chosen;
    mwfl::Dialog* pointer = nullptr;
    const auto refresh = [&] {
        list.ClearItems(); visible.clear();
        auto filter = query.GetText();
        std::ranges::transform(filter, filter.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        for (std::size_t index = 0; index < paths.size() && visible.size() < 1000; ++index) {
            auto label = paths[index].wstring(); auto folded = label;
            std::ranges::transform(folded, folded.begin(), [](wchar_t ch) {
                return static_cast<wchar_t>(std::towlower(ch));
            });
            std::size_t cursor = 0;
            for (const auto ch : filter) {
                cursor = folded.find(ch, cursor);
                if (cursor == std::wstring::npos) break;
                ++cursor;
            }
            if (!filter.empty() && cursor == std::wstring::npos) continue;
            visible.push_back(index); list.AddItem(label);
        }
        if (!visible.empty()) list.SetSelection(0);
    };
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Quick Open File"),
        .initial_client_size = {620.0_dip, 430.0_dip}, .resizable = true,
        .callbacks = {.initialize = [&](HWND window) {
            mwfl::ControlHost ui{window}; ui.Add(query, {743}, L""); ui.Add(list, {744}, {});
            ui.Add(open, {IDOK}, Tr(L"Open")); ui.Add(cancel, {IDCANCEL}, Tr(L"Cancel"));
            const auto layout = pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(6.0_dip)
                .Add(query, mwfl::Fixed(30.0_dip)).Add(list, mwfl::Stretch())
                .Add(mwfl::Row().Gap(6.0_dip).Add(mwfl::Column(), mwfl::Stretch())
                    .Add(open, mwfl::Fixed(80.0_dip)).Add(cancel, mwfl::Fixed(80.0_dip)),
                    mwfl::Fixed(30.0_dip)));
            refresh(); query.Focus(); return layout;
        }, .command = [&](HWND, WORD id, WORD notification) {
            if (id == 743 && notification == EN_CHANGE) { refresh(); return true; }
            if (id == IDOK || (id == 744 && notification == LBN_DBLCLK)) {
                const auto selected = list.GetSelectedIndex();
                if (selected && static_cast<std::size_t>(*selected) < visible.size()) {
                    chosen = visible[static_cast<std::size_t>(*selected)]; pointer->Accept(); return true;
                }
            }
            return false;
        }}});
    pointer = &dialog; static_cast<void>(dialog.ShowModal());
    if (chosen && !OpenPath(paths[*chosen])) status_.SetText(Tr(L"Quick Open failed"));
}

void MainWindow::ToggleSearchOption(mwfl::ControlId id, bool& value) {
    value = !value;
    if (auto* command = commands_.Find(id)) {
        command->SetChecked(value);
        menu_.UpdateCommand(*command);
    }
}

void MainWindow::ToggleSearchSelection() {
    search_selection_ = !search_selection_;
    search_scope_.reset();
    if (search_selection_) {
        if (auto* editor = ActiveEditor()) {
            const auto selection = editor->GetSelection();
            if (selection.end > selection.start) search_scope_ = selection;
            else search_selection_ = false;
        } else search_selection_ = false;
    }
    if (auto* command = commands_.Find(kSearchSelection)) {
        command->SetChecked(search_selection_);
        menu_.UpdateCommand(*command);
    }
    status_.SetText(search_selection_ ? Tr(L"Search is limited to the captured selection")
                                      : Tr(L"Search uses the whole document"));
}

mwfl::ScintillaSearchFlags MainWindow::SearchFlags() const noexcept {
    auto flags = mwfl::ScintillaSearchFlags::none;
    if (search_match_case_) flags = flags | mwfl::ScintillaSearchFlags::match_case;
    if (search_whole_word_) flags = flags | mwfl::ScintillaSearchFlags::whole_word;
    if (search_regex_) flags = flags | mwfl::ScintillaSearchFlags::regular_expression;
    return flags;
}

void MainWindow::IncrementalSearch() {
    auto* document = ActiveDocument();
    if (!document) return;
    const auto query = search_.GetText();
    if (query.empty()) { notepad_colon::ClearSearchMarks(*document->editor); return; }
    const auto scope_start = search_scope_ ? search_scope_->start : 0;
    const auto scope_end = search_scope_ ? search_scope_->end : document->editor->GetLength();
    const auto selection = document->editor->GetSelection();
    auto match = document->editor->Find(
        query, (std::max)(selection.start, scope_start), scope_end, SearchFlags());
    if (!match) match = document->editor->Find(query, scope_start, scope_end, SearchFlags());
    if (match) { document->editor->SetSelection(*match); document->editor->Send(SCI_SCROLLCARET); }
}

void MainWindow::RememberSearch(std::wstring_view query) {
    if (query.empty()) return;
    std::erase(search_history_, query);
    search_history_.insert(search_history_.begin(), std::wstring{query});
    if (search_history_.size() > 20) search_history_.resize(20);
}

void MainWindow::MarkAllSearchMatches() {
    auto* document = ActiveDocument();
    if (!document) return;
    const auto query = search_.GetText();
    if (query.empty()) return;
    RememberSearch(query);
    const auto scope = search_scope_.value_or(mwfl::ScintillaTextRange{
        0, document->editor->GetLength()});
    const auto count = notepad_colon::MarkAllMatches(
        *document->editor, query, SearchFlags(), scope);
    status_.SetText(Tr(L"Marked ") + std::to_wstring(count) + Tr(L" occurrence(s)") +
        (count == 10000 ? Tr(L" (limit reached)") : L"") +
        (document->large_buffer ? Tr(L" in the visible large-file window") : L""));
}

std::optional<mwfl::ScintillaTextRange> MainWindow::FindNext() {
    auto* document = ActiveDocument();
    auto* editor = document ? document->editor.get() : nullptr;
    const auto query = search_.GetText();
    if (!editor || query.empty()) return std::nullopt;
    RememberSearch(query);
    const auto selection = editor->GetSelection();
    const auto scope_start = search_scope_ ? search_scope_->start : 0;
    const auto scope_end = search_scope_ ? search_scope_->end : editor->GetLength();
    const auto start = (std::max)(selection.end, scope_start);
    auto match = editor->Find(query, start, scope_end, SearchFlags());
    if (!match && start != scope_start)
        match = editor->Find(query, scope_start, scope_end, SearchFlags());
    if (!match && document->large_buffer && !search_regex_ && !search_selection_) {
        const auto bytes = mwfl::ToUtf8(query);
        if (bytes && !bytes->empty()) {
            const auto needle = std::span<const std::uint8_t>{
                reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()};
            const auto logical_start = document->mapped_decoded_offset +
                static_cast<std::uint64_t>((std::max<mwfl::ScintillaPosition>)(0, selection.end));
            auto found = document->large_buffer->Find(
                needle, logical_start, UINT64_MAX, search_match_case_);
            if (!found && logical_start != 0)
                found = document->large_buffer->Find(
                    needle, 0, logical_start, search_match_case_);
            if (found) {
                const auto lead = document->mapped_window_size / 4;
                const auto window_offset = *found > lead ? *found - lead : 0;
                if (LoadLargeFileWindow(*document, window_offset,
                                        std::pair{*found, bytes->size()}))
                    match = editor->GetSelection();
            }
        }
    }
    if (match) editor->SetSelection(*match);
    status_.SetText(match ? Tr(L"Match selected") :
        (document->large_buffer && search_regex_ ?
            Tr(L"No match in this window; full-file regular expressions are disabled for large files")
            : Tr(L"No matches")));
    return match;
}

void MainWindow::ReplaceNext() {
    auto* editor = ActiveEditor();
    const auto match = FindNext();
    if (!editor || !match || !editor->ReplaceTarget(replacement_.GetText())) return;
    status_.SetText(Tr(L"Replaced"));
}

void MainWindow::ReplaceAll() {
    auto* editor = ActiveEditor();
    const auto query = search_.GetText();
    if (!editor || query.empty()) return;
    const auto replacement = replacement_.GetText();
    const auto scope_start = search_scope_ ? search_scope_->start : 0;
    auto scope_end = search_scope_ ? search_scope_->end : editor->GetLength();
    mwfl::ScintillaPosition cursor = scope_start;
    std::size_t count = 0;
    while (cursor <= scope_end) {
        const auto match = editor->Find(query, cursor, scope_end, SearchFlags());
        if (!match) break;
        editor->SetSelection(*match);
        if (!editor->ReplaceTarget(replacement)) break;
        const auto replacement_utf8 = mwfl::ToUtf8(replacement);
        const auto replacement_size = static_cast<mwfl::ScintillaPosition>(
            replacement_utf8 ? replacement_utf8->size() : 0);
        const auto removed = match->end - match->start;
        scope_end += replacement_size - removed;
        cursor = match->start + replacement_size;
        if (removed == 0 && replacement_size == 0) ++cursor;
        ++count;
    }
    status_.SetText(Tr(L"Replaced ") + std::to_wstring(count) + Tr(L" occurrence(s)") +
        (ActiveDocument() && ActiveDocument()->large_buffer ? Tr(L" in the loaded large-file window") : L""));
}

bool MainWindow::PromptFolderSearch(std::wstring& query, bool& open_documents_only) {
    mwfl::Label query_label, include_label, exclude_label;
    mwfl::TextBox query_box, include_box, exclude_box;
    mwfl::CheckBox open_only;
    mwfl::Button search_button, cancel;
    bool accepted = false;
    mwfl::Dialog* pointer = nullptr;
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Find in Files"),
        .initial_client_size = {560.0_dip, 270.0_dip}, .resizable = false,
        .callbacks = {.initialize = [&](HWND window) {
            mwfl::ControlHost ui{window};
            ui.Add(query_label, {750}, Tr(L"Find")); ui.Add(query_box, {751}, query);
            ui.Add(include_label, {752}, Tr(L"Include files (semicolon-separated globs)"));
            ui.Add(include_box, {753}, search_include_globs_);
            ui.Add(exclude_label, {754}, Tr(L"Exclude files (semicolon-separated globs)"));
            ui.Add(exclude_box, {755}, search_exclude_globs_);
            ui.Add(open_only, {756}, Tr(L"Search open documents only"));
            open_only.SetChecked(open_documents_only);
            ui.Add(search_button, {IDOK}, Tr(L"Search")); ui.Add(cancel, {IDCANCEL}, Tr(L"Cancel"));
            auto rows = mwfl::Column().Margin(10.0_dip).Gap(5.0_dip)
                .Add(query_label, mwfl::Fixed(20.0_dip)).Add(query_box, mwfl::Fixed(30.0_dip))
                .Add(include_label, mwfl::Fixed(20.0_dip)).Add(include_box, mwfl::Fixed(30.0_dip))
                .Add(exclude_label, mwfl::Fixed(20.0_dip)).Add(exclude_box, mwfl::Fixed(30.0_dip))
                .Add(open_only, mwfl::Fixed(28.0_dip))
                .Add(mwfl::Row().Gap(6.0_dip).Add(mwfl::Column(), mwfl::Stretch())
                    .Add(search_button, mwfl::Fixed(90.0_dip))
                    .Add(cancel, mwfl::Fixed(90.0_dip)), mwfl::Fixed(30.0_dip));
            query_box.Focus(); return pointer->SetLayout(std::move(rows));
        }, .command = [&](HWND, WORD id, WORD) {
            if (id != IDOK) return false;
            query = query_box.GetText();
            search_include_globs_ = include_box.GetText();
            search_exclude_globs_ = exclude_box.GetText();
            open_documents_only = open_only.IsChecked();
            if (query.empty()) return true;
            accepted = true; pointer->Accept(); return true;
        }}});
    pointer = &dialog; static_cast<void>(dialog.ShowModal()); return accepted;
}

void MainWindow::StartFolderSearch() {
    if (workspace_catalog_.Roots().empty()) {
        OpenFolderInteractive();
        return;
    }
    auto query = search_.GetText();
    bool open_documents_only = false;
    if (!PromptFolderSearch(query, open_documents_only)) return;
    search_.SetText(query);
    RememberSearch(query);
    CancelFolderSearch();
    ReapCompletedWorkers(search_workers_);
    const auto generation = search_operations_.Begin();
    if (auto* command = commands_.Find(kCancelSearch)) command->SetEnabled(true);
    menu_.UpdateCommand(*commands_.Find(kCancelSearch));
    status_.SetText(Tr(L"Searching workspace..."));
    const HWND window = GetHwnd();
    const auto roots = workspace_catalog_.Roots();
    const notepad_colon::SearchOptions requested_options{
        .include_globs = SplitGlobs(search_include_globs_),
        .exclude_globs = SplitGlobs(search_exclude_globs_),
        .match_case = search_match_case_, .whole_word = search_whole_word_,
        .regular_expression = search_regex_, .multiline = true,
        .use_gitignore = true};
    if (open_documents_only) {
        std::vector<std::pair<std::filesystem::path, std::wstring>> open_documents;
        for (const auto& document : documents_) {
            const auto* metadata = workspace_.Find(document.id);
            if (!metadata || metadata->path.empty() || document.editor->GetLength() > 8 * 1024 * 1024)
                continue;
            if (const auto text = document.editor->GetText())
                open_documents.emplace_back(metadata->path, *text);
        }
        auto done = std::make_shared<std::atomic_bool>(false);
        search_workers_.push_back({done, std::jthread(
            [this, window, query, generation, requested_options, done,
             open_documents = std::move(open_documents)](std::stop_token stop) mutable {
                notepad_colon::SearchResult result;
                for (const auto& [path, text] : open_documents) {
                    auto options = requested_options;
                    options.maximum_results = 5000 - result.matches.size();
                    if (options.maximum_results == 0) { result.truncated = true; break; }
                    auto part = notepad_colon::SearchText(path, text, query, options, stop);
                    result.files_searched += part.files_searched;
                    result.matches.insert(result.matches.end(),
                        std::make_move_iterator(part.matches.begin()),
                        std::make_move_iterator(part.matches.end()));
                    if (!part.error.empty()) { result.error = std::move(part.error); break; }
                    if (stop.stop_requested()) { result.cancelled = true; break; }
                }
                if (!search_operations_.Publish(generation, std::move(result))) {
                    done->store(true); return;
                }
                ::PostMessageW(window, kSearchCompleteMessage,
                               static_cast<WPARAM>(generation), 0);
                done->store(true);
            })});
        return;
    }
    auto done = std::make_shared<std::atomic_bool>(false);
    search_workers_.push_back({done, std::jthread([this, window, roots, query, generation,
                                                  requested_options, done](std::stop_token stop) {
        notepad_colon::SearchResult result;
        for (const auto& root : roots) {
            if (stop.stop_requested()) { result.cancelled = true; break; }
            auto options = requested_options;
            options.maximum_results = 5000 - result.matches.size();
            if (options.maximum_results == 0) { result.truncated = true; break; }
            auto part = notepad_colon::SearchWorkspace(root, query, options, stop);
            if (!part.error.empty()) { result.error = std::move(part.error); break; }
            result.matches.insert(result.matches.end(),
                                  std::make_move_iterator(part.matches.begin()),
                                  std::make_move_iterator(part.matches.end()));
            result.files_searched += part.files_searched;
            result.files_skipped += part.files_skipped;
            result.truncated = result.truncated || part.truncated;
            result.cancelled = result.cancelled || part.cancelled;
        }
        if (!search_operations_.Publish(generation, std::move(result))) {
            done->store(true); return;
        }
        ::PostMessageW(window, kSearchCompleteMessage,
                       static_cast<WPARAM>(generation), 0);
        done->store(true);
    })});
}

void MainWindow::CancelFolderSearch() {
    for (auto& worker : search_workers_) worker.thread.request_stop();
    search_operations_.Begin();
    replace_operations_.Begin();
    filter_operations_.Begin();
    if (auto* command = commands_.Find(kCancelSearch)) {
        command->SetEnabled(false);
        menu_.UpdateCommand(*command);
    }
    status_.SetText(Tr(L"Search cancelled."));
}

void MainWindow::CompleteSearch() {
    auto result = search_operations_.TakeCurrent();
    if (!result) return;
    search_results_ = std::move(*result);
    ListView_DeleteAllItems(results_.GetHwnd());
    for (std::size_t index = 0; index < search_results_.matches.size(); ++index) {
        const auto& match = search_results_.matches[index];
        const mwfl::ListItemId id{index + 1};
        auto display_path = match.path;
        for (const auto& root : workspace_catalog_.Roots())
            if (notepad_colon::IsWithinWorkspaceRoots(match.path, {root})) {
                display_path = root.filename() / match.path.lexically_relative(root); break;
            }
        results_.AddItem(id, display_path.wstring());
        results_.SetItemText(id, 1, std::to_wstring(match.line));
        results_.SetItemText(id, 2, std::to_wstring(match.column));
        results_.SetItemText(id, 3, match.preview);
    }
    if (auto* command = commands_.Find(kCancelSearch)) command->SetEnabled(false);
    menu_.UpdateCommand(*commands_.Find(kCancelSearch));
    results_visible_ = true;
    ApplyCompactLayout();
    status_.SetText(!search_results_.error.empty() ? search_results_.error :
        (search_results_.cancelled ? Tr(L"Search cancelled | ") : Tr(L"Search complete | ")) +
        std::to_wstring(search_results_.matches.size()) + Tr(L" matches in ") +
        std::to_wstring(search_results_.files_searched) + Tr(L" files") +
        (search_results_.truncated ? Tr(L" (truncated)") : L""));
}

}  // namespace notepad_colon::app
