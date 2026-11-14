#include "main_window.h"

// Reviewing changes another program made to an open file.
//
// A clean document is reloaded automatically when its file changes on disk.
// That is the right default, but it makes a coding agent's (or any other
// tool's) edit invisible: the text simply becomes different. The document
// therefore keeps the text it had before the first unreviewed reload, marks
// the lines that changed, and lets the user accept or reject each change.

namespace notepad_colon::app {
namespace {
// Remembering the previous text doubles the document's memory, and aligning
// it is quadratic in the changed region; very large documents are skipped.
constexpr std::size_t kMaximumReviewCharacters = 8u * 1024 * 1024;

void DefineReviewMarker(mwfl::ScintillaEditor& editor, bool dark) {
    editor.Send(SCI_MARKERDEFINE, kReviewMarker, SC_MARK_FULLRECT);
    editor.Send(SCI_MARKERSETBACK, kReviewMarker, dark ? RGB(90, 160, 255) : RGB(0, 110, 220));
    const auto mask = editor.Send(SCI_GETMARGINMASKN, 2);
    editor.Send(SCI_SETMARGINMASKN, 2, mask | (LRESULT{1} << kReviewMarker));
}
}  // namespace

std::size_t MainWindow::PendingReviewCount() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(documents_,
        [](const EditorDocument& document) { return document.review_base.has_value(); }));
}

void MainWindow::ClearReview(EditorDocument& document) {
    document.review_base.reset();
    document.editor->Send(SCI_MARKERDELETEALL, kReviewMarker);
}

std::size_t MainWindow::NoteExternalReload(EditorDocument& document, std::wstring before) {
    if (document.large_buffer || document.follow_tail) return 0;
    const auto after = document.editor->GetText();
    if (!after || before.size() > kMaximumReviewCharacters ||
        after->size() > kMaximumReviewCharacters) {
        ClearReview(document);
        return 0;
    }
    // Several reloads before a review are judged together against the oldest text.
    if (!document.review_base) document.review_base = std::move(before);
    const auto comparison = notepad_colon::CompareText(*document.review_base, *after);
    if (comparison.identical) {
        ClearReview(document);
        return 0;
    }
    DefineReviewMarker(*document.editor, IsDark());
    if (document.split_editor) DefineReviewMarker(*document.split_editor, IsDark());
    document.editor->Send(SCI_MARKERDELETEALL, kReviewMarker);
    const auto line_count = document.editor->Send(SCI_GETLINECOUNT);
    for (const auto& line : comparison.lines) {
        if (line.kind == notepad_colon::DifferenceKind::equal) continue;
        // A removed line has no line of its own any more: mark where it was.
        const auto target = (std::min)(static_cast<LRESULT>(line.right_line) - 1, line_count - 1);
        if (target >= 0) document.editor->Send(SCI_MARKERADD, target, kReviewMarker);
    }
    return notepad_colon::GroupChangeHunks(comparison).size();
}

bool MainWindow::FinishReview(EditorDocument& document, const std::vector<bool>& rejected) {
    if (!document.review_base) return false;
    const auto after = document.editor->GetText();
    if (!after) return false;
    const auto comparison = notepad_colon::CompareText(*document.review_base, *after);
    const auto hunks = notepad_colon::GroupChangeHunks(comparison);
    const auto rejected_count = static_cast<std::size_t>(std::ranges::count(rejected, true));
    if (rejected_count != 0) {
        const auto merged = notepad_colon::MergeReviewedText(
            *document.review_base, *after, comparison, rejected);
        if (merged != *after &&
            !notepad_colon::ReplaceDocumentText(*document.editor, merged, document.editor->GetSelection()))
            return false;
    }
    ClearReview(document);
    SyncPresentation(rejected_count == 0
        ? Tr(L"Review finished: all changes kept")
        : Tr(L"Review finished: ") + std::to_wstring(hunks.size() - (std::min)(hunks.size(), rejected_count)) +
          Tr(L" kept, ") + std::to_wstring(rejected_count) + Tr(L" rejected; save to write the result"));
    return true;
}

void MainWindow::ReviewExternalChanges() {
    auto* document = ActiveDocument();
    if (!document || !document->review_base) {
        const auto pending = std::ranges::find_if(documents_,
            [](const EditorDocument& candidate) { return candidate.review_base.has_value(); });
        if (pending == documents_.end()) {
            status_.SetText(Tr(L"No external changes are waiting for review"));
            return;
        }
        workspace_.Activate(pending->id);
        SynchronizeTabs(true);
        document = &*pending;
    }
    if (IsTestMode()) return;  // The dialog is modal; tests drive FinishReview directly.
    ShowChangeReview(*document);
}

void MainWindow::ShowChangeReview(EditorDocument& document) {
    const auto* metadata = workspace_.Find(document.id);
    const auto after_text = document.editor->GetText();
    if (!metadata || !after_text || !document.review_base) return;
    const std::wstring before_text = *document.review_base;
    const auto comparison = notepad_colon::CompareText(before_text, *after_text);
    const auto hunks = notepad_colon::GroupChangeHunks(comparison);
    if (hunks.empty()) { ClearReview(document); SyncPresentation(Tr(L"Review finished: all changes kept")); return; }
    std::vector<bool> rejected(hunks.size(), false);
    std::size_t current = 0;
    bool apply = false;

    mwfl::ScintillaEditor left, right;
    mwfl::Label left_label, right_label, summary;
    mwfl::Button previous, next, toggle, reject_all, keep_all, accept, later;
    CompareScrollSync sync{&left, &right};
    mwfl::Dialog* pointer = nullptr;
    constexpr int kChanged = 10, kRejected = 11, kCurrent = 12;

    const auto refresh = [&] {
        for (auto* editor : {&left, &right})
            for (const int marker : {kChanged, kRejected, kCurrent}) editor->Send(SCI_MARKERDELETEALL, marker);
        for (std::size_t index = 0; index < hunks.size(); ++index) {
            const auto& hunk = hunks[index];
            const int marker = rejected[index] ? kRejected : kChanged;
            for (std::size_t offset = 0; offset < hunk.count; ++offset) {
                const auto& line = comparison.lines[hunk.first + offset];
                if (line.kind != notepad_colon::DifferenceKind::inserted)
                    left.Send(SCI_MARKERADD, line.left_line - 1, marker);
                if (line.kind != notepad_colon::DifferenceKind::deleted)
                    right.Send(SCI_MARKERADD, line.right_line - 1, marker);
            }
        }
        const auto& hunk = hunks[current];
        const auto left_lines = left.Send(SCI_GETLINECOUNT), right_lines = right.Send(SCI_GETLINECOUNT);
        const auto left_line = (std::min)(static_cast<LRESULT>(hunk.left_line) - 1, left_lines - 1);
        const auto right_line = (std::min)(static_cast<LRESULT>(hunk.right_line) - 1, right_lines - 1);
        left.Send(SCI_MARKERADD, left_line, kCurrent);
        right.Send(SCI_MARKERADD, right_line, kCurrent);
        sync.updating = true;
        for (auto [editor, line] : {std::pair{&left, left_line}, std::pair{&right, right_line}}) {
            editor->Send(SCI_GOTOLINE, line);
            const auto visible = editor->Send(SCI_LINESONSCREEN);
            editor->Send(SCI_SETFIRSTVISIBLELINE,
                (std::max)(LRESULT{0}, editor->Send(SCI_VISIBLEFROMDOCLINE, line) - visible / 3));
        }
        sync.updating = false;
        const auto rejected_count = static_cast<std::size_t>(std::ranges::count(rejected, true));
        summary.SetText(Tr(L"Change ") + std::to_wstring(current + 1) + Tr(L" of ") +
            std::to_wstring(hunks.size()) + L"  •  -" + std::to_wstring(hunk.removed) + L" +" +
            std::to_wstring(hunk.added) + L"  •  " + std::to_wstring(rejected_count) + Tr(L" rejected"));
        toggle.SetText(rejected[current] ? Tr(L"Keep this change") : Tr(L"Reject this change"));
    };

    mwfl::Dialog dialog({
        .owner = GetHwnd(), .title = Tr(L"Review External Changes — ") + metadata->title,
        .initial_client_size = {1040.0_dip, 640.0_dip}, .resizable = true,
        .callbacks = {
            .initialize = [&](HWND window) {
                mwfl::ControlHost ui{window};
                ui.Add(left_label, {621}, Tr(L"Before"));
                ui.Add(right_label, {622}, Tr(L"After (in the editor now)"));
                ui.Add(summary, {623}, L"");
                ui.Add(previous, {624}, Tr(L"Previous")); ui.Add(next, {625}, Tr(L"Next"));
                ui.Add(toggle, {626}, Tr(L"Reject this change"));
                ui.Add(reject_all, {627}, Tr(L"Reject all")); ui.Add(keep_all, {628}, Tr(L"Keep all"));
                ui.Add(accept, {IDOK}, Tr(L"Finish review")); ui.Add(later, {IDCANCEL}, Tr(L"Later"));
                if (!left.Create(window, {629}, {}, runtime_) ||
                    !right.Create(window, {630}, {}, runtime_)) return false;
                const bool dark = IsDark();
                for (auto* editor : {&left, &right}) {
                    editor->ConfigureCodeEditing();
                    notepad_colon::ApplyPreferences(*editor, preferences_, dark);
                    static_cast<void>(notepad_colon::ConfigureLanguage(*editor, lexilla_, document.language, dark));
                    // Folding would hide parts of a change; the fold margin stays closed.
                    editor->Send(SCI_SETPROPERTY, reinterpret_cast<WPARAM>("fold"), reinterpret_cast<LPARAM>("0"));
                    editor->Send(SCI_SETMARGINWIDTHN, 1, 0);
                    editor->Send(SCI_MARKERDEFINE, kChanged, SC_MARK_BACKGROUND);
                    editor->Send(SCI_MARKERDEFINE, kRejected, SC_MARK_BACKGROUND);
                    editor->Send(SCI_MARKERDEFINE, kCurrent, SC_MARK_SHORTARROW);
                    editor->Send(SCI_MARKERSETBACK, kCurrent, dark ? RGB(120, 180, 255) : RGB(0, 100, 210));
                    editor->Send(SCI_MARKERSETBACK, kRejected, dark ? RGB(70, 70, 70) : RGB(225, 225, 225));
                    editor->Send(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
                    editor->Send(SCI_SETMARGINMASKN, 2, 1 << kCurrent);
                    editor->Send(SCI_SETMARGINWIDTHN, 2, 16);
                    ::SetWindowSubclass(editor->GetHwnd(), CompareEditorSubclass, 1,
                                        reinterpret_cast<DWORD_PTR>(&sync));
                }
                // Removed text reads red on the old side, added text green on the new side.
                left.Send(SCI_MARKERSETBACK, kChanged, dark ? RGB(92, 44, 44) : RGB(255, 222, 222));
                right.Send(SCI_MARKERSETBACK, kChanged, dark ? RGB(36, 80, 48) : RGB(214, 245, 220));
                left.SetText(before_text); right.SetText(*after_text);
                left.SetReadOnly(true); right.SetReadOnly(true);
                mwfl::SetAccessibleName(left.GetHwnd(), Tr(L"Text before the external change").c_str());
                mwfl::SetAccessibleName(right.GetHwnd(), Tr(L"Text after the external change").c_str());
                mwfl::SetDialogDefaultButton(window, IDOK);
                const auto layout = pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(5.0_dip)
                    .Add(mwfl::Row().Gap(6.0_dip).Add(left_label, mwfl::Stretch())
                        .Add(right_label, mwfl::Stretch()), mwfl::Fixed(22.0_dip))
                    .Add(mwfl::Row().Gap(5.0_dip).Add(left, mwfl::Stretch())
                        .Add(right, mwfl::Stretch()), mwfl::Stretch())
                    .Add(summary, mwfl::Fixed(22.0_dip))
                    .Add(mwfl::Row().Gap(6.0_dip)
                        .Add(previous, ButtonWidth(Tr(L"Previous"))).Add(next, ButtonWidth(Tr(L"Next")))
                        .Add(toggle, ButtonWidth(Tr(L"Reject this change") + L"  "))
                        .Add(reject_all, ButtonWidth(Tr(L"Reject all")))
                        .Add(keep_all, ButtonWidth(Tr(L"Keep all")))
                        .Add(mwfl::Column(), mwfl::Stretch())
                        .Add(accept, ButtonWidth(Tr(L"Finish review")))
                        .Add(later, ButtonWidth(Tr(L"Later"))), mwfl::Fixed(30.0_dip)));
                refresh();
                return layout;
            },
            .command = [&](HWND, WORD id, WORD) {
                if (id == 624) { current = current ? current - 1 : hunks.size() - 1; refresh(); return true; }
                if (id == 625) { current = (current + 1) % hunks.size(); refresh(); return true; }
                if (id == 626) {
                    rejected[current] = !rejected[current];
                    // Deciding a change moves on to the next undecided one.
                    if (rejected[current] && current + 1 < hunks.size()) ++current;
                    refresh(); return true;
                }
                if (id == 627 || id == 628) { rejected.assign(hunks.size(), id == 627); refresh(); return true; }
                if (id == IDOK) { apply = true; pointer->Accept(); return true; }
                return false;
            },
            .destroyed = [&] {
                if (left.GetHwnd()) ::RemoveWindowSubclass(left.GetHwnd(), CompareEditorSubclass, 1);
                if (right.GetHwnd()) ::RemoveWindowSubclass(right.GetHwnd(), CompareEditorSubclass, 1);
            }}});
    pointer = &dialog;
    static_cast<void>(dialog.ShowModal());
    if (apply) static_cast<void>(FinishReview(document, rejected));
    else status_.SetText(Tr(L"Review postponed; the changed lines stay marked"));
}

}  // namespace notepad_colon::app
