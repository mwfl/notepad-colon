#include <mwfl/mwfl.h>
#include <mwfl/scintilla.h>
#include <notepad_colon/text.h>
#include <notepad_colon/session.h>
#include <notepad_colon/language.h>
#include "scintilla_support.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

using mwfl::operator""_dip;

namespace {
constexpr mwfl::ControlId kNew{100};
constexpr mwfl::ControlId kOpen{101};
constexpr mwfl::ControlId kSave{102};
constexpr mwfl::ControlId kSaveAs{103};
constexpr mwfl::ControlId kSaveAll{104};
constexpr mwfl::ControlId kClose{105};
constexpr mwfl::ControlId kExit{106};
constexpr mwfl::ControlId kUndo{110};
constexpr mwfl::ControlId kRedo{111};
constexpr mwfl::ControlId kCut{112};
constexpr mwfl::ControlId kCopy{113};
constexpr mwfl::ControlId kPaste{114};
constexpr mwfl::ControlId kSelectAll{115};
constexpr mwfl::ControlId kFindNext{120};
constexpr mwfl::ControlId kReplaceNext{121};
constexpr mwfl::ControlId kReplaceAll{122};
constexpr mwfl::ControlId kUtf8{123};
constexpr mwfl::ControlId kUtf8Bom{124};
constexpr mwfl::ControlId kUtf16Le{125};
constexpr mwfl::ControlId kUtf16Be{126};
constexpr mwfl::ControlId kCrlf{127};
constexpr mwfl::ControlId kLf{128};
constexpr mwfl::ControlId kRecentBase{300};
constexpr mwfl::ControlId kToggleFold{320};
constexpr mwfl::ControlId kToggleBookmark{321};
constexpr mwfl::ControlId kNextBookmark{322};
constexpr mwfl::ControlId kRectangular{323};
constexpr mwfl::ControlId kWhitespace{324};
constexpr mwfl::ControlId kMoveLineUp{325};
constexpr mwfl::ControlId kMoveLineDown{326};
constexpr mwfl::ControlId kDuplicateLine{327};
constexpr mwfl::ControlId kDeleteLine{328};
constexpr mwfl::ControlId kUppercase{329};
constexpr mwfl::ControlId kLowercase{330};
constexpr mwfl::ControlId kIndent{331};
constexpr mwfl::ControlId kOutdent{332};
constexpr mwfl::ControlId kWordWrap{333};
constexpr mwfl::ControlId kZoomIn{334};
constexpr mwfl::ControlId kZoomOut{335};
constexpr mwfl::ControlId kZoomReset{336};
constexpr std::wstring_view kSettingsKey = L"Software\\mwfl\\Notepad Colon";
constexpr mwfl::ControlId kSearch{130};
constexpr mwfl::ControlId kReplacement{131};
constexpr mwfl::ControlId kTabs{140};
constexpr UINT kSelfTestMessage = WM_APP + 0x240;

struct EditorDocument {
    mwfl::DocumentId id{};
    std::unique_ptr<mwfl::ScintillaEditor> editor;
    mwfl::TextEncoding encoding = mwfl::TextEncoding::utf8;
    notepad_colon::LineEnding line_ending = notepad_colon::LineEnding::crlf;
    std::optional<mwfl::FileStamp> stamp;
    bool read_only = false;
    notepad_colon::Language language = notepad_colon::Language::plain_text;
};

std::wstring_view EncodingName(mwfl::TextEncoding encoding) noexcept {
    switch (encoding) {
    case mwfl::TextEncoding::utf8: return L"UTF-8";
    case mwfl::TextEncoding::utf8_bom: return L"UTF-8 BOM";
    case mwfl::TextEncoding::utf16_le: return L"UTF-16 LE";
    case mwfl::TextEncoding::utf16_be: return L"UTF-16 BE";
    }
    return L"Unknown";
}

std::wstring_view LineEndingName(notepad_colon::LineEnding ending) noexcept {
    switch (ending) {
    case notepad_colon::LineEnding::crlf: return L"CRLF";
    case notepad_colon::LineEnding::lf: return L"LF";
    case notepad_colon::LineEnding::cr: return L"CR";
    }
    return L"Unknown";
}

notepad_colon::Encoding ToSessionEncoding(mwfl::TextEncoding encoding) noexcept {
    switch (encoding) {
    case mwfl::TextEncoding::utf8: return notepad_colon::Encoding::utf8;
    case mwfl::TextEncoding::utf8_bom: return notepad_colon::Encoding::utf8_bom;
    case mwfl::TextEncoding::utf16_le: return notepad_colon::Encoding::utf16_le;
    case mwfl::TextEncoding::utf16_be: return notepad_colon::Encoding::utf16_be;
    }
    return notepad_colon::Encoding::utf8;
}

mwfl::TextEncoding FromSessionEncoding(notepad_colon::Encoding encoding) noexcept {
    switch (encoding) {
    case notepad_colon::Encoding::utf8: return mwfl::TextEncoding::utf8;
    case notepad_colon::Encoding::utf8_bom: return mwfl::TextEncoding::utf8_bom;
    case notepad_colon::Encoding::utf16_le: return mwfl::TextEncoding::utf16_le;
    case notepad_colon::Encoding::utf16_be: return mwfl::TextEncoding::utf16_be;
    case notepad_colon::Encoding::ansi: return mwfl::TextEncoding::utf8;
    }
    return mwfl::TextEncoding::utf8;
}

bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) noexcept {
    const auto a = left.lexically_normal().native();
    const auto b = right.lexically_normal().native();
    return ::CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                  b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

class MainWindow final : public mwfl::WindowBase {
public:
    MainWindow() : workspace_({1}, 12) {}

    void BuildUI() override {
        if (!runtime_.LoadAdjacent())
            throw std::runtime_error("Scintilla.dll is not available beside Notepad Colon");
        if (!lexilla_.LoadAdjacent())
            throw std::runtime_error("Lexilla.dll is not available beside Notepad Colon");
        if (!IsSelfTest()) {
            const auto loaded = mwfl::LoadRecentFilesFromRegistry(HKEY_CURRENT_USER, kSettingsKey, 10);
            if (loaded.Succeeded()) recent_ = *loaded.value;
        }
        BuildCommands();
        mwfl::EnableFileDrop(GetHwnd());

        mwfl::ControlHost ui{*this};
        ui.Add(toolbar_);
        ui.Add(search_, kSearch, L"");
        ui.Add(replacement_, kReplacement, L"");
        ui.Add(tabs_, mwfl::TabControlOptions{});
        ui.Add(status_);
        for (const auto id : {kNew, kOpen, kSave, kClose, kUndo, kRedo,
                              kCut, kCopy, kPaste, kFindNext, kReplaceNext})
            mwfl::Must(toolbar_.AddCommand(*commands_.Find(id)), "add toolbar command");
        toolbar_.AutoSize();
        mwfl::Must(adapter_.Attach(tabs_) == mwfl::DocumentTabStatus::success,
                   "attach document tab adapter");
        BuildMenu();
        mwfl::Must(accelerators_.Create(commands_), "create accelerators");
        SetAccelerators(accelerators_.GetHandle());

        mwfl::Must(mwfl::SetAccessibleName(tabs_.GetHwnd(), L"Open documents"), "name tabs");
        mwfl::Must(mwfl::SetAccessibleName(search_.GetHwnd(), L"Find text"), "name search");
        mwfl::Must(mwfl::SetAccessibleName(replacement_.GetHwnd(), L"Replacement text"),
                   "name replacement");
        mwfl::Must(mwfl::SetAccessibleName(status_.GetHwnd(), L"Document status"), "name status");

        SetLayout(mwfl::Column()
            .Add(toolbar_, mwfl::Auto())
            .Add(mwfl::Row().Gap(5.0_dip).Margin(5.0_dip)
                .Add(search_, mwfl::Stretch())
                .Add(replacement_, mwfl::Stretch()), mwfl::Fixed(34.0_dip))
            .Add(tabs_, mwfl::Stretch())
            .Add(status_, mwfl::Fixed(26.0_dip)));

        ResolveSessionPath();
        if (!RestoreSession()) NewDocument();
        if (IsSelfTest() && !::PostMessageW(GetHwnd(), kSelfTestMessage, 0, 0))
            throw std::runtime_error("could not schedule GUI self-test");
    }

    mwfl::EventResult OnCommand(const mwfl::CommandEvent& event) override {
        return commands_.Dispatch(event);
    }

    mwfl::EventResult OnNotify(const mwfl::NotifyEvent& event) override {
        if (event.Is(tabs_, TCN_SELCHANGE)) {
            adapter_.ActivateNativeSelection(workspace_);
            SyncPresentation(L"Document selected");
            return mwfl::EventResult::Handled();
        }
        auto* document = FindByHwnd(event.header.hwndFrom);
        if (!document) return mwfl::EventResult::Propagate();
        const auto notification = document->editor->DecodeNotification(event.header);
        if (!notification) return mwfl::EventResult::Propagate();
        if (notification->kind == mwfl::ScintillaNotificationKind::save_point_left) {
            workspace_.SetDirty(document->id, true);
        } else if (notification->kind == mwfl::ScintillaNotificationKind::save_point_reached) {
            workspace_.SetDirty(document->id, false);
        } else if (notification->kind == mwfl::ScintillaNotificationKind::modified &&
                   notification->lines_added != 0) {
            static_cast<void>(document->editor->UpdateLineNumberMargin());
        } else if (notification->kind == mwfl::ScintillaNotificationKind::character_added) {
            notepad_colon::HandleCharacterAdded(*document->editor, notification->character);
        } else if (notification->kind == mwfl::ScintillaNotificationKind::update_ui) {
            notepad_colon::UpdateBraceHighlight(*document->editor);
        }
        workspace_.SetUndoState(document->id, document->editor->CanUndo(), document->editor->CanRedo());
        SyncPresentation(L"Editing");
        if (notification->kind == mwfl::ScintillaNotificationKind::modified)
            static_cast<void>(SaveSessionSnapshot());
        return mwfl::EventResult::Propagate();
    }

    mwfl::EventResult OnResize(const mwfl::ResizeEvent&) override {
        adapter_.ArrangePages();
        return mwfl::EventResult::Propagate();
    }

    mwfl::EventResult OnMessage(const mwfl::WindowMessage& event) override {
        if (event.id == kSelfTestMessage) {
            RunSelfTest();
            return mwfl::EventResult::Handled();
        }
        if (event.id == WM_DROPFILES) {
            const auto paths = mwfl::ReadDroppedFiles(reinterpret_cast<HDROP>(event.wparam));
            std::size_t opened = 0;
            for (const auto& path : paths) opened += OpenPath(path) ? 1u : 0u;
            status_.SetText(L"Opened " + std::to_wstring(opened) + L" dropped file(s)");
            return mwfl::EventResult::Handled();
        }
        return mwfl::EventResult::Propagate();
    }

    mwfl::EventResult OnClose() override {
        if (!IsSelfTest()) {
            for (const auto& document : workspace_.GetDocuments()) {
                if (!document.dirty) continue;
                if (::MessageBoxW(GetHwnd(),
                                  L"Close Notepad Colon? Unsaved documents will be restored next time.",
                                  L"Notepad Colon", MB_ICONINFORMATION | MB_OKCANCEL) != IDOK)
                    return mwfl::EventResult::Handled();
                break;
            }
            if (!SaveSessionSnapshot())
                return mwfl::EventResult::Handled();
            static_cast<void>(mwfl::SaveRecentFilesToRegistry(
                HKEY_CURRENT_USER, kSettingsKey, recent_));
        }
        adapter_.Detach();
        return mwfl::EventResult::Propagate();
    }

private:
    static bool IsSelfTest() noexcept {
        return std::wstring_view{::GetCommandLineW()}.find(L"--self-test") != std::wstring_view::npos;
    }

    void BuildCommands() {
        commands_
            .Add(mwfl::Command(kNew, L"&New", [this] { NewDocument(); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'N'}))
            .Add(mwfl::Command(kOpen, L"&Open...", [this] { OpenInteractive(); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'O'}))
            .Add(mwfl::Command(kSave, L"&Save", [this] { static_cast<void>(SaveActive(false)); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'S'}))
            .Add(mwfl::Command(kSaveAs, L"Save &As...", [this] { static_cast<void>(SaveActive(true)); }))
            .Add(mwfl::Command(kSaveAll, L"Save A&ll", [this] { static_cast<void>(SaveAll()); })
                     .SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, 'S'}))
            .Add(mwfl::Command(kClose, L"&Close", [this] { static_cast<void>(CloseActive()); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'W'}))
            .Add(mwfl::Command(kExit, L"E&xit", [this] { static_cast<void>(Close()); }))
            .Add(mwfl::Command(kUndo, L"&Undo", [this] { if (auto* e = ActiveEditor()) e->Undo(); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'Z'}))
            .Add(mwfl::Command(kRedo, L"&Redo", [this] { if (auto* e = ActiveEditor()) e->Redo(); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'Y'}))
            .Add(mwfl::Command(kCut, L"Cu&t", [this] { if (auto* e = ActiveEditor()) e->Cut(); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'X'}))
            .Add(mwfl::Command(kCopy, L"&Copy", [this] { if (auto* e = ActiveEditor()) e->Copy(); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'C'}))
            .Add(mwfl::Command(kPaste, L"&Paste", [this] { if (auto* e = ActiveEditor()) e->Paste(); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'V'}))
            .Add(mwfl::Command(kSelectAll, L"Select &All", [this] { if (auto* e = ActiveEditor()) e->SelectAll(); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'A'}))
            .Add(mwfl::Command(kFindNext, L"&Find Next", [this] { static_cast<void>(FindNext()); })
                     .SetShortcut({FVIRTKEY, VK_F3}))
            .Add(mwfl::Command(kReplaceNext, L"&Replace Next", [this] { ReplaceNext(); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'H'}))
            .Add(mwfl::Command(kReplaceAll, L"Replace &All", [this] { ReplaceAll(); }))
            .Add(mwfl::Command(kUtf8, L"UTF-&8", [this] { SetEncoding(mwfl::TextEncoding::utf8); }))
            .Add(mwfl::Command(kUtf8Bom, L"UTF-8 &BOM", [this] { SetEncoding(mwfl::TextEncoding::utf8_bom); }))
            .Add(mwfl::Command(kUtf16Le, L"UTF-16 &LE", [this] { SetEncoding(mwfl::TextEncoding::utf16_le); }))
            .Add(mwfl::Command(kUtf16Be, L"UTF-16 B&E", [this] { SetEncoding(mwfl::TextEncoding::utf16_be); }))
            .Add(mwfl::Command(kCrlf, L"Windows (&CRLF)", [this] { SetLineEnding(notepad_colon::LineEnding::crlf); }))
            .Add(mwfl::Command(kLf, L"Unix (&LF)", [this] { SetLineEnding(notepad_colon::LineEnding::lf); }));
        commands_
            .Add(mwfl::Command(kToggleFold, L"Toggle &Fold", [this] { if (auto* e = ActiveEditor()) notepad_colon::ToggleCurrentFold(*e); })
                     .SetShortcut({FVIRTKEY | FCONTROL, VK_OEM_4}))
            .Add(mwfl::Command(kToggleBookmark, L"Toggle &Bookmark", [this] { if (auto* e = ActiveEditor()) notepad_colon::ToggleBookmark(*e); })
                     .SetShortcut({FVIRTKEY | FCONTROL, VK_F2}))
            .Add(mwfl::Command(kNextBookmark, L"Next Bookmark", [this] { if (auto* e = ActiveEditor()) notepad_colon::GoToNextBookmark(*e); })
                     .SetShortcut({FVIRTKEY, VK_F2}))
            .Add(mwfl::Command(kRectangular, L"Rectangular Selection", [this] { if (auto* e = ActiveEditor()) notepad_colon::ToggleRectangularSelection(*e); }))
            .Add(mwfl::Command(kWhitespace, L"Show Whitespace", [this] { if (auto* e = ActiveEditor()) notepad_colon::ToggleWhitespace(*e); }))
            .Add(mwfl::Command(kMoveLineUp, L"Move Line Up", [this] { if (auto* e = ActiveEditor()) notepad_colon::MoveSelectedLines(*e, false); })
                     .SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, VK_UP}))
            .Add(mwfl::Command(kMoveLineDown, L"Move Line Down", [this] { if (auto* e = ActiveEditor()) notepad_colon::MoveSelectedLines(*e, true); })
                     .SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, VK_DOWN}))
            .Add(mwfl::Command(kDuplicateLine, L"Duplicate Line", [this] { if (auto* e = ActiveEditor()) notepad_colon::DuplicateLine(*e); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'D'}))
            .Add(mwfl::Command(kDeleteLine, L"Delete Line", [this] { if (auto* e = ActiveEditor()) notepad_colon::DeleteLine(*e); })
                     .SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, 'L'}))
            .Add(mwfl::Command(kUppercase, L"UPPERCASE", [this] { if (auto* e = ActiveEditor()) notepad_colon::ChangeCase(*e, true); }))
            .Add(mwfl::Command(kLowercase, L"lowercase", [this] { if (auto* e = ActiveEditor()) notepad_colon::ChangeCase(*e, false); }))
            .Add(mwfl::Command(kIndent, L"Indent", [this] { if (auto* e = ActiveEditor()) notepad_colon::IndentSelection(*e, true); }))
            .Add(mwfl::Command(kOutdent, L"Outdent", [this] { if (auto* e = ActiveEditor()) notepad_colon::IndentSelection(*e, false); }));
        commands_
            .Add(mwfl::Command(kWordWrap, L"Word Wrap", [this] { if (auto* e = ActiveEditor()) notepad_colon::ToggleWordWrap(*e); }))
            .Add(mwfl::Command(kZoomIn, L"Zoom In", [this] { if (auto* e = ActiveEditor()) e->SetZoom(e->GetZoom() + 1); })
                     .SetShortcut({FVIRTKEY | FCONTROL, VK_OEM_PLUS}))
            .Add(mwfl::Command(kZoomOut, L"Zoom Out", [this] { if (auto* e = ActiveEditor()) e->SetZoom(e->GetZoom() - 1); })
                     .SetShortcut({FVIRTKEY | FCONTROL, VK_OEM_MINUS}))
            .Add(mwfl::Command(kZoomReset, L"Reset Zoom", [this] { if (auto* e = ActiveEditor()) e->SetZoom(0); })
                     .SetShortcut({FVIRTKEY | FCONTROL, '0'}));
        for (std::size_t index = 0; index < recent_.GetMaximumEntries(); ++index) {
            commands_.Add(mwfl::Command(
                {static_cast<WORD>(kRecentBase.value + index)}, L"Recent file",
                [this, index] {
                    const auto paths = recent_.GetPaths();
                    if (index < paths.size()) static_cast<void>(OpenPath(paths[index]));
                }));
        }
        RefreshRecentCommands();
    }

    void BuildMenu() {
        mwfl::Menu file, edit, search, encoding, line_endings, view, code;
        mwfl::Must(menu_.Create(), "create menu bar");
        mwfl::Must(file.CreatePopup(), "create file menu");
        mwfl::Must(edit.CreatePopup(), "create edit menu");
        mwfl::Must(search.CreatePopup(), "create search menu");
        mwfl::Must(encoding.CreatePopup(), "create encoding menu");
        mwfl::Must(line_endings.CreatePopup(), "create line endings menu");
        mwfl::Must(view.CreatePopup(), "create view menu");
        mwfl::Must(code.CreatePopup(), "create code menu");
        for (const auto id : {kNew, kOpen, kSave, kSaveAs, kSaveAll, kClose})
            mwfl::Must(file.AppendCommand(*commands_.Find(id)), "append file command");
        mwfl::Must(file.AppendSeparator(), "append file separator");
        mwfl::Must(file.AppendCommand(*commands_.Find(kExit)), "append exit command");
        if (!recent_.GetPaths().empty()) {
            mwfl::Must(file.AppendSeparator(), "append recent separator");
            for (std::size_t index = 0; index < recent_.GetPaths().size(); ++index)
                mwfl::Must(file.AppendCommand(*commands_.Find(
                    {static_cast<WORD>(kRecentBase.value + index)})), "append recent file");
        }
        for (const auto id : {kUndo, kRedo, kCut, kCopy, kPaste, kSelectAll})
            mwfl::Must(edit.AppendCommand(*commands_.Find(id)), "append edit command");
        for (const auto id : {kFindNext, kReplaceNext, kReplaceAll})
            mwfl::Must(search.AppendCommand(*commands_.Find(id)), "append search command");
        for (const auto id : {kUtf8, kUtf8Bom, kUtf16Le, kUtf16Be})
            mwfl::Must(encoding.AppendCommand(*commands_.Find(id)), "append encoding command");
        for (const auto id : {kCrlf, kLf})
            mwfl::Must(line_endings.AppendCommand(*commands_.Find(id)), "append line ending command");
        for (const auto id : {kWhitespace, kWordWrap, kZoomIn, kZoomOut, kZoomReset,
                              kRectangular, kToggleFold, kToggleBookmark, kNextBookmark})
            mwfl::Must(view.AppendCommand(*commands_.Find(id)), "append view command");
        for (const auto id : {kMoveLineUp, kMoveLineDown, kDuplicateLine, kDeleteLine,
                              kUppercase, kLowercase, kIndent, kOutdent})
            mwfl::Must(code.AppendCommand(*commands_.Find(id)), "append code command");
        mwfl::Must(menu_.AppendSubmenu(std::move(file), L"&File"), "append file menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(edit), L"&Edit"), "append edit menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(search), L"&Search"), "append search menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(encoding), L"En&coding"), "append encoding menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(line_endings), L"&EOL"), "append line endings menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(view), L"&View"), "append view menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(code), L"&Code"), "append code menu");
        mwfl::Must(menu_.AttachToWindow(GetHwnd()), "attach menu");
    }

    EditorDocument* FindDocument(mwfl::DocumentId id) noexcept {
        const auto found = std::ranges::find(documents_, id, &EditorDocument::id);
        return found == documents_.end() ? nullptr : &*found;
    }

    EditorDocument* FindByHwnd(HWND hwnd) noexcept {
        const auto found = std::ranges::find_if(documents_,
            [hwnd](const EditorDocument& document) { return document.editor->GetHwnd() == hwnd; });
        return found == documents_.end() ? nullptr : &*found;
    }

    EditorDocument* ActiveDocument() noexcept {
        const auto id = workspace_.GetActiveId();
        return id ? FindDocument(*id) : nullptr;
    }

    mwfl::ScintillaEditor* ActiveEditor() noexcept {
        const auto* document = ActiveDocument();
        return document ? document->editor.get() : nullptr;
    }

    void NewDocument(std::wstring text = {}, std::wstring title = {}) {
        const mwfl::DocumentId id{next_id_++};
        if (title.empty()) title = id.value == 1 ? L"Untitled" : L"Untitled " + std::to_wstring(id.value);
        auto editor = std::make_unique<mwfl::ScintillaEditor>();
        mwfl::Must(editor->Create(GetHwnd(), mwfl::ControlId{static_cast<WORD>(200 + id.value)},
                                  mwfl::RectDip{}, runtime_), "create document editor");
        mwfl::Must(editor->ConfigureCodeEditing(), "configure document editor");
        notepad_colon::ConfigureAdvancedEditing(*editor);
        mwfl::Must(notepad_colon::ConfigureLanguage(
            *editor, lexilla_, notepad_colon::Language::plain_text), "configure plain-text lexer");
        mwfl::Must(editor->SetText(text), "set document text");
        editor->SetSavePoint();
        mwfl::Must(static_cast<bool>(workspace_.Add({id, title, {}})), "add document metadata");
        mwfl::Must(adapter_.BindPage(id, editor->GetHwnd()) == mwfl::DocumentTabStatus::success,
                   "bind document page");
        mwfl::Must(mwfl::SetAccessibleName(editor->GetHwnd(), title.c_str()), "name document editor");
        documents_.push_back({id, std::move(editor)});
        workspace_.Activate(id);
        SynchronizeTabs(true);
        SyncPresentation(L"New document");
        static_cast<void>(SaveSessionSnapshot());
    }

    bool OpenPath(const std::filesystem::path& path) {
        for (const auto& metadata : workspace_.GetDocuments()) {
            if (!metadata.path.empty() && SamePath(metadata.path, path)) {
                workspace_.Activate(metadata.id);
                SynchronizeTabs(true);
                SyncPresentation(L"Already open");
                return true;
            }
        }
        const auto loaded = mwfl::ReadTextFile(path);
        if (!loaded.Succeeded()) return false;
        const mwfl::DocumentId id{next_id_++};
        auto editor = std::make_unique<mwfl::ScintillaEditor>();
        if (!editor->Create(GetHwnd(), mwfl::ControlId{static_cast<WORD>(200 + id.value)},
                            mwfl::RectDip{}, runtime_) ||
            !editor->ConfigureCodeEditing() || !editor->SetText(loaded.value->text)) return false;
        notepad_colon::ConfigureAdvancedEditing(*editor);
        const auto language = notepad_colon::DetectLanguage(path);
        if (!notepad_colon::ConfigureLanguage(*editor, lexilla_, language)) return false;
        editor->SetSavePoint();
        const auto title = path.filename().wstring();
        if (!workspace_.Add({id, title, path})) return false;
        if (adapter_.BindPage(id, editor->GetHwnd()) != mwfl::DocumentTabStatus::success) return false;
        mwfl::SetAccessibleName(editor->GetHwnd(), title.c_str());
        documents_.push_back({id, std::move(editor), loaded.value->encoding,
                              notepad_colon::DetectLineEnding(loaded.value->text), loaded.value->stamp,
                              false, language});
        workspace_.Activate(id);
        SynchronizeTabs(true);
        SyncPresentation(L"Opened");
        RememberPath(path);
        static_cast<void>(SaveSessionSnapshot());
        return true;
    }

    void OpenInteractive() {
        const auto selected = mwfl::ShowOpenFileDialog({.owner = GetHwnd(), .title = L"Open",
            .filters = {{L"Text and source files", L"*.txt;*.md;*.cpp;*.h;*.json;*.xml;*.ini;*.yaml;*.yml;*.ps1"},
                        {L"All files", L"*.*"}}});
        if (selected.accepted && !OpenPath(selected.path)) status_.SetText(L"Open failed");
    }

    bool SaveDocument(EditorDocument& document, bool choose_path) {
        const auto* metadata = workspace_.Find(document.id);
        if (!metadata) return false;
        auto path = metadata->path;
        if (choose_path || path.empty()) {
            if (IsSelfTest()) {
                path = std::filesystem::temp_directory_path() /
                    (L"notepad-colon-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
                     std::to_wstring(document.id.value) + L".txt");
            } else {
                const auto selected = mwfl::ShowSaveFileDialog({.owner = GetHwnd(), .title = L"Save",
                    .filters = {{L"Text files", L"*.txt"}, {L"All files", L"*.*"}},
                    .default_extension = L"txt", .path_must_exist = false});
                if (!selected.accepted) return false;
                path = selected.path;
            }
        }
        const auto text = document.editor->GetText();
        if (!text) return false;
        const auto expected = path == metadata->path ? document.stamp : std::nullopt;
        const auto saved = mwfl::WriteTextFileAtomic(path, *text, document.encoding, expected);
        if (!saved.Succeeded() || !saved.stamp) return false;
        document.stamp = saved.stamp;
        document.line_ending = notepad_colon::DetectLineEnding(*text);
        document.editor->SetSavePoint();
        workspace_.Rename(document.id, path.filename().wstring(), path);
        const auto language = notepad_colon::DetectLanguage(path);
        if (language != document.language) {
            document.language = language;
            static_cast<void>(notepad_colon::ConfigureLanguage(*document.editor, lexilla_, language));
        }
        workspace_.SetDirty(document.id, false);
        RememberPath(path);
        SynchronizeTabs(false);
        SyncPresentation(L"Saved");
        static_cast<void>(SaveSessionSnapshot());
        return true;
    }

    bool SaveActive(bool choose_path) {
        auto* document = ActiveDocument();
        return document && SaveDocument(*document, choose_path);
    }

    bool SaveAll() {
        for (auto& document : documents_) {
            const auto* metadata = workspace_.Find(document.id);
            if (metadata && metadata->dirty && !SaveDocument(document, false)) return false;
        }
        return true;
    }

    bool CloseActive(bool discard = false) {
        const auto id = workspace_.GetActiveId();
        if (!id) return false;
        const auto* metadata = workspace_.Find(*id);
        if (metadata && metadata->dirty && !discard && !IsSelfTest()) {
            const int answer = ::MessageBoxW(GetHwnd(), L"Save changes before closing?",
                                              L"Notepad Colon", MB_ICONWARNING | MB_YESNOCANCEL);
            if (answer == IDCANCEL || (answer == IDYES && !SaveActive(false))) return false;
        }
        auto found = std::ranges::find(documents_, *id, &EditorDocument::id);
        if (found == documents_.end()) return false;
        const HWND hwnd = found->editor->GetHwnd();
        if (!workspace_.Close(*id) || adapter_.UnbindPage(*id) != mwfl::DocumentTabStatus::success)
            return false;
        documents_.erase(found);
        if (::IsWindow(hwnd)) ::DestroyWindow(hwnd);
        if (documents_.empty()) NewDocument();
        else {
            SynchronizeTabs(true);
            SyncPresentation(L"Closed");
            static_cast<void>(SaveSessionSnapshot());
        }
        return true;
    }

    std::optional<mwfl::ScintillaTextRange> FindNext() {
        auto* editor = ActiveEditor();
        const auto query = search_.GetText();
        if (!editor || query.empty()) return std::nullopt;
        const auto selection = editor->GetSelection();
        auto match = editor->Find(query, selection.end);
        if (!match) match = editor->Find(query);
        if (match) editor->SetSelection(*match);
        status_.SetText(match ? L"Match selected" : L"No matches");
        return match;
    }

    void ReplaceNext() {
        auto* editor = ActiveEditor();
        const auto match = FindNext();
        if (!editor || !match || !editor->ReplaceTarget(replacement_.GetText())) return;
        status_.SetText(L"Replaced");
    }

    void ReplaceAll() {
        auto* editor = ActiveEditor();
        const auto query = search_.GetText();
        if (!editor || query.empty()) return;
        const auto replacement = replacement_.GetText();
        mwfl::ScintillaPosition cursor = 0;
        std::size_t count = 0;
        while (cursor <= editor->GetLength()) {
            const auto match = editor->Find(query, cursor);
            if (!match) break;
            editor->SetSelection(*match);
            if (!editor->ReplaceTarget(replacement)) break;
            const auto replacement_utf8 = mwfl::ToUtf8(replacement);
            cursor = match->start + static_cast<mwfl::ScintillaPosition>(
                replacement_utf8 ? replacement_utf8->size() : 0);
            ++count;
        }
        status_.SetText(L"Replaced " + std::to_wstring(count) + L" occurrence(s)");
    }

    void SetEncoding(mwfl::TextEncoding encoding) {
        auto* document = ActiveDocument();
        if (!document || document->encoding == encoding) return;
        document->encoding = encoding;
        workspace_.SetDirty(document->id, true);
        SyncPresentation(L"Encoding changed");
    }

    void SetLineEnding(notepad_colon::LineEnding ending) {
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
        SyncPresentation(L"Line endings converted");
    }

    void RefreshRecentCommands() {
        const auto paths = recent_.GetPaths();
        for (std::size_t index = 0; index < recent_.GetMaximumEntries(); ++index) {
            auto* command = commands_.Find({static_cast<WORD>(kRecentBase.value + index)});
            if (!command) continue;
            command->SetEnabled(index < paths.size()).SetVisible(index < paths.size());
            command->SetText(index < paths.size()
                ? L"&" + std::to_wstring(index + 1) + L" " + paths[index].wstring()
                : L"Recent file");
        }
    }

    void RememberPath(const std::filesystem::path& path) {
        if (path.empty() || !recent_.Add(path)) return;
        RefreshRecentCommands();
        if (menu_.GetHandle()) BuildMenu();
        if (!IsSelfTest())
            static_cast<void>(mwfl::SaveRecentFilesToRegistry(HKEY_CURRENT_USER, kSettingsKey, recent_));
    }

    void ResolveSessionPath() {
        if (IsSelfTest()) {
            session_path_ = std::filesystem::temp_directory_path() /
                (L"notepad-colon-gui-" + std::to_wstring(::GetCurrentProcessId()) + L".state");
            std::error_code ignored;
            std::filesystem::remove(session_path_, ignored);
            return;
        }
        wchar_t local_app_data[32768]{};
        const DWORD length = ::GetEnvironmentVariableW(
            L"LOCALAPPDATA", local_app_data, static_cast<DWORD>(std::size(local_app_data)));
        if (length > 0 && length < std::size(local_app_data))
            session_path_ = std::filesystem::path{local_app_data} / L"mwfl" /
                L"Notepad Colon" / L"session.state";
    }

    bool SaveSessionSnapshot() {
        if (session_path_.empty() || restoring_session_) return true;
        notepad_colon::Session session;
        if (const auto active = workspace_.GetActiveId()) {
            const auto index = workspace_.FindIndex(*active);
            if (index) session.active_index = *index;
        }
        for (const auto& metadata : workspace_.GetDocuments()) {
            const auto* document = FindDocument(metadata.id);
            if (!document) return false;
            const auto text = document->editor->GetText();
            if (!text) return false;
            notepad_colon::SessionEntry entry;
            entry.path = metadata.path;
            entry.recovery_text = metadata.dirty || metadata.path.empty() ? *text : L"";
            entry.encoding = ToSessionEncoding(document->encoding);
            entry.line_ending = document->line_ending;
            const auto selection = document->editor->GetSelection();
            entry.view.anchor = selection.start;
            entry.view.caret = selection.end;
            entry.dirty = metadata.dirty;
            session.documents.push_back(std::move(entry));
        }
        return notepad_colon::SaveSessionAtomic(session_path_, session);
    }

    bool RestoreSession() {
        if (session_path_.empty()) return false;
        notepad_colon::Session session;
        if (!notepad_colon::LoadSession(session_path_, session) || session.documents.empty()) return false;
        restoring_session_ = true;
        for (const auto& entry : session.documents) {
            if (!entry.path.empty() && OpenPath(entry.path)) {
                auto* document = ActiveDocument();
                if (document && entry.dirty && !entry.recovery_text.empty()) {
                    document->editor->SetText(entry.recovery_text);
                    document->encoding = FromSessionEncoding(entry.encoding);
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
        SyncPresentation(L"Session restored");
        return true;
    }

    void SynchronizeTabs(bool focus) {
        const auto result = adapter_.Synchronize(workspace_, focus);
        if (!result) throw std::runtime_error("synchronize document tabs failed");
        adapter_.ArrangePages();
    }

    void SyncPresentation(std::wstring_view action) {
        const auto* metadata = workspace_.GetActiveId() ? workspace_.Find(*workspace_.GetActiveId()) : nullptr;
        const auto* document = ActiveDocument();
        SetTitle((metadata ? metadata->title : L"Notepad Colon") +
                 std::wstring(metadata && metadata->dirty ? L" * — Notepad Colon" : L" — Notepad Colon"));
        if (metadata && document) {
            const auto selection = document->editor->GetSelection();
            status_.SetText(std::wstring(action) + L" | Pos " + std::to_wstring(selection.end) +
                L" | " + std::wstring(EncodingName(document->encoding)) + L" | " +
                std::wstring(LineEndingName(document->line_ending)) + L" | " +
                std::wstring(notepad_colon::LanguageName(document->language)) + L" | " +
                std::to_wstring(workspace_.GetCount()) + L" document(s)");
        } else status_.SetText(action);

        const bool has_editor = document != nullptr;
        if (auto* command = commands_.Find(kSave)) command->SetEnabled(has_editor);
        if (auto* command = commands_.Find(kClose)) command->SetEnabled(has_editor);
        if (auto* command = commands_.Find(kUndo)) command->SetEnabled(has_editor && document->editor->CanUndo());
        if (auto* command = commands_.Find(kRedo)) command->SetEnabled(has_editor && document->editor->CanRedo());
        for (const auto id : {kSave, kClose, kUndo, kRedo})
            if (const auto* command = commands_.Find(id)) toolbar_.UpdateCommand(*command);
        ::DrawMenuBar(GetHwnd());
    }

    void RunSelfTest() noexcept {
        int result = 0;
        std::vector<std::filesystem::path> cleanup;
        try {
            auto* first = ActiveDocument();
            if (!first || !first->editor->SetText(L"first 世界\n")) result = 1;
            if (result == 0 && !notepad_colon::ConfigureLanguage(
                    *first->editor, lexilla_, notepad_colon::Language::cpp)) result = 11;
            if (result == 0) {
                const auto old_length = first->editor->GetLength();
                notepad_colon::DuplicateLine(*first->editor);
                if (first->editor->GetLength() <= old_length) result = 12;
                first->editor->Undo();
                notepad_colon::ToggleBookmark(*first->editor);
                if (!notepad_colon::GoToNextBookmark(*first->editor)) result = 13;
                notepad_colon::ToggleRectangularSelection(*first->editor);
                notepad_colon::ToggleRectangularSelection(*first->editor);
                notepad_colon::ToggleWhitespace(*first->editor);
                notepad_colon::ToggleWhitespace(*first->editor);
            }
            NewDocument(L"second document\r\n", L"Second");
            if (result == 0 && workspace_.GetCount() != 2) result = 2;
            notepad_colon::Session snapshot;
            if (result == 0 && (!SaveSessionSnapshot() ||
                !notepad_colon::LoadSession(session_path_, snapshot) ||
                snapshot.documents.size() != 2)) result = 10;
            search_.SetText(L"document");
            replacement_.SetText(L"tab");
            ReplaceNext();
            if (result == 0 && !ActiveEditor()->IsModified()) result = 3;
            auto* second = ActiveDocument();
            if (result == 0 && (!second || !SaveDocument(*second, true))) result = 4;
            if (second) {
                const auto* metadata = workspace_.Find(second->id);
                if (metadata) cleanup.push_back(metadata->path);
            }
            if (result == 0 && !CloseActive(true)) result = 5;
            if (result == 0 && workspace_.GetCount() != 1) result = 6;
            first = ActiveDocument();
            if (result == 0 && (!first || !SaveDocument(*first, true))) result = 7;
            if (first) {
                const auto* metadata = workspace_.Find(first->id);
                if (metadata) cleanup.push_back(metadata->path);
            }
            if (result == 0 && adapter_.GetPages().size() != workspace_.GetCount()) result = 8;
        } catch (...) {
            result = 9;
        }
        for (const auto& path : cleanup) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
        if (!session_path_.empty()) {
            std::error_code ignored;
            std::filesystem::remove(session_path_, ignored);
        }
        adapter_.Detach();
        ::PostQuitMessage(result);
    }

    mwfl::ScintillaRuntime runtime_;
    notepad_colon::LexillaRuntime lexilla_;
    mwfl::DocumentWorkspaceModel workspace_;
    mwfl::DocumentTabWorkspaceAdapter adapter_;
    std::vector<EditorDocument> documents_;
    std::uint64_t next_id_ = 1;
    mwfl::CommandSet commands_;
    mwfl::AcceleratorTable accelerators_;
    mwfl::Menu menu_;
    mwfl::Toolbar toolbar_;
    mwfl::TextBox search_, replacement_;
    mwfl::TabControl tabs_;
    mwfl::StatusBar status_;
    std::filesystem::path session_path_;
    bool restoring_session_ = false;
    mwfl::RecentFileList recent_{10};
};
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    return mwfl::RunApplication<MainWindow>(instance, show_command,
        {.title = L"Notepad Colon", .initial_bounds = {{}, {900.0_dip, 650.0_dip}},
         .use_default_bounds = false}, {.com_apartment = mwfl::ComApartment::sta});
}
