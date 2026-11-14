#include "main_window.h"

// Split view: a second Scintilla view bound to the same document, so edits,
// undo history and syntax state are shared while scroll position, selection,
// zoom and wrapping stay independent.
namespace notepad_colon::app {

namespace {
constexpr int kSplitGap = 4;
}

bool MainWindow::ConfigureViewLanguage(mwfl::ScintillaEditor& view, const EditorDocument& document,
                                       bool dark) {
    const auto* registered = language_registry_.Find(document.language_id);
    if (registered && !registered->builtin)
        return notepad_colon::ConfigureRegisteredLanguage(view, lexilla_, *registered, dark);
    return notepad_colon::ConfigureLanguage(
        view, lexilla_, document.language, dark,
        document.mapped_file ? notepad_colon::SyntaxPerformanceMode::lightweight
                             : notepad_colon::SyntaxPerformanceMode::full);
}

bool MainWindow::OpenSplitView(EditorDocument& document) {
    if (document.split_editor) return true;
    auto view = std::make_unique<mwfl::ScintillaEditor>();
    if (!view->Create(GetHwnd(), mwfl::ControlId{static_cast<WORD>(3000 + document.id.value)},
                      mwfl::RectDip{}, runtime_) ||
        !view->ConfigureCodeEditing())
        return false;
    // Rebinding releases the view's private empty document and adds a
    // reference to the shared one; Scintilla frees it with the last view.
    view->Send(SCI_SETDOCPOINTER, 0, document.editor->Send(SCI_GETDOCPOINTER));
    notepad_colon::ConfigureAdvancedEditing(*view);
    notepad_colon::ApplyPreferences(*view, preferences_, IsDark());
    view->Send(SCI_SETTABWIDTH, document.editor->Send(SCI_GETTABWIDTH));
    view->Send(SCI_SETINDENT, document.editor->Send(SCI_GETINDENT));
    view->Send(SCI_SETUSETABS, document.editor->Send(SCI_GETUSETABS));
    view->Send(SCI_SETWRAPMODE, document.editor->Send(SCI_GETWRAPMODE));
    view->Send(SCI_SETZOOM, document.editor->Send(SCI_GETZOOM));
    if (!ConfigureViewLanguage(*view, document, IsDark())) return false;
    static_cast<void>(view->UpdateLineNumberMargin());
    if (const auto* metadata = workspace_.Find(document.id))
        mwfl::SetAccessibleName(view->GetHwnd(), (metadata->title + Tr(L" (second view)").c_str()).c_str());
    view->Send(SCI_GOTOPOS, document.editor->Send(SCI_GETCURRENTPOS));
    document.split_editor = std::move(view);
    if (document.syntax_tree || document.wasm_syntax) {
        notepad_colon::ConfigureTreeSitterStyles(*document.split_editor, IsDark());
        StyleVisibleSyntax(document, *document.split_editor);
    }
    return true;
}

void MainWindow::CloseSplitView(EditorDocument& document) {
    if (!document.split_editor) return;
    const bool had_focus = ::GetFocus() == document.split_editor->GetHwnd();
    const HWND hwnd = document.split_editor->GetHwnd();
    document.split_editor.reset();
    if (::IsWindow(hwnd)) ::DestroyWindow(hwnd);
    if (had_focus && ::IsWindow(document.editor->GetHwnd())) ::SetFocus(document.editor->GetHwnd());
}

void MainWindow::ToggleSplitView() {
    auto* document = ActiveDocument();
    if (!document) return;
    if (document->split_editor) {
        CloseSplitView(*document);
        ArrangeViews();
        status_.SetText(Tr(L"Split view closed"));
        return;
    }
    if (!OpenSplitView(*document)) {
        status_.SetText(Tr(L"Split view could not be created"));
        return;
    }
    ArrangeViews();
    status_.SetText(Tr(L"Split view: both views edit the same document"));
}

void MainWindow::RotateSplitView() {
    auto* document = ActiveDocument();
    if (!document || !document->split_editor) {
        status_.SetText(Tr(L"Open a split view first"));
        return;
    }
    document->split_side_by_side = !document->split_side_by_side;
    ArrangeViews();
    status_.SetText(document->split_side_by_side ? Tr(L"Split view: side by side")
                                                 : Tr(L"Split view: stacked"));
}

void MainWindow::FocusOtherView() {
    auto* document = ActiveDocument();
    if (!document) return;
    if (!document->split_editor) {
        ::SetFocus(document->editor->GetHwnd());
        return;
    }
    const bool in_split = ::GetFocus() == document->split_editor->GetHwnd();
    ::SetFocus(in_split ? document->editor->GetHwnd() : document->split_editor->GetHwnd());
}

// The tab adapter gives the whole page to the primary editor; when a document
// has a second view the page is divided between the two.
void MainWindow::ArrangeViews() noexcept {
    adapter_.ArrangePages();
    const auto active = workspace_.GetActiveId();
    for (auto& document : documents_) {
        if (!document.split_editor) continue;
        const HWND split = document.split_editor->GetHwnd();
        if (!active || document.id != *active) {
            ::ShowWindow(split, SW_HIDE);
            continue;
        }
        const HWND primary = document.editor->GetHwnd();
        RECT page{};
        if (!::GetWindowRect(primary, &page)) continue;
        ::MapWindowPoints(nullptr, GetHwnd(), reinterpret_cast<POINT*>(&page), 2);
        RECT first = page, second = page;
        if (document.split_side_by_side) {
            const LONG middle = page.left + (page.right - page.left - kSplitGap) / 2;
            first.right = middle;
            second.left = middle + kSplitGap;
        } else {
            const LONG middle = page.top + (page.bottom - page.top - kSplitGap) / 2;
            first.bottom = middle;
            second.top = middle + kSplitGap;
        }
        ::SetWindowPos(primary, nullptr, first.left, first.top, first.right - first.left,
                       first.bottom - first.top, SWP_NOZORDER | SWP_NOACTIVATE);
        ::SetWindowPos(split, primary, second.left, second.top, second.right - second.left,
                       second.bottom - second.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

}  // namespace notepad_colon::app
