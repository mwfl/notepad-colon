#include <mwfl/mwfl.h>
#include <mwfl/scintilla.h>
#include <mwfl/file_association.h>
#include <mwfl/shell_integration.h>
#include <mwfl/settings_store.h>
#include <mwfl/printing_settings.h>
#include <mwfl/single_instance.h>
#include <mwfl/dialog.h>
#include <notepad_colon/large_file.h>
#include <notepad_colon/comparison.h>
#include <notepad_colon/configuration.h>
#include <notepad_colon/macro.h>
#include <notepad_colon/output.h>
#include <notepad_colon/editing.h>
#include <notepad_colon/preferences.h>
#include <notepad_colon/recovery.h>
#include <notepad_colon/text.h>
#include <notepad_colon/session.h>
#include <notepad_colon/language.h>
#include <notepad_colon/workspace.h>
#include "scintilla_support.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <thread>
#include <unordered_map>
#include <utility>
#include <shellapi.h>
#include <commctrl.h>
#include <Scintilla.h>

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
constexpr mwfl::ControlId kSelectNext{370};
constexpr mwfl::ControlId kSelectAllOccurrences{371};
constexpr mwfl::ControlId kSortAscending{372};
constexpr mwfl::ControlId kSortDescending{373};
constexpr mwfl::ControlId kUniqueLines{374};
constexpr mwfl::ControlId kReverseLines{375};
constexpr mwfl::ControlId kRemoveBlankLines{376};
constexpr mwfl::ControlId kTrimTrailing{377};
constexpr mwfl::ControlId kJoinLines{378};
constexpr mwfl::ControlId kSplitLines{379};
constexpr mwfl::ControlId kTabsToSpaces{380};
constexpr mwfl::ControlId kSpacesToTabs{381};
constexpr mwfl::ControlId kTitleCase{382};
constexpr mwfl::ControlId kSentenceCase{383};
constexpr mwfl::ControlId kJsonEscape{384};
constexpr mwfl::ControlId kJsonUnescape{385};
constexpr mwfl::ControlId kToggleComment{386};
constexpr mwfl::ControlId kBlockComment{387};
constexpr mwfl::ControlId kBase64Encode{388};
constexpr mwfl::ControlId kBase64Decode{389};
constexpr mwfl::ControlId kUrlEncode{390};
constexpr mwfl::ControlId kUrlDecode{391};
constexpr mwfl::ControlId kInsertDateTime{392};
constexpr mwfl::ControlId kInsertSequence{393};
constexpr mwfl::ControlId kRecoveryManager{394};
constexpr mwfl::ControlId kCompareWithFile{395};
constexpr mwfl::ControlId kCompareWithDisk{396};
constexpr mwfl::ControlId kCommandPalette{397};
constexpr mwfl::ControlId kMacroStart{398};
constexpr mwfl::ControlId kMacroStop{399};
constexpr mwfl::ControlId kMacroPlay{400};
constexpr mwfl::ControlId kMacroPlayFive{401};
constexpr mwfl::ControlId kMacroSave{402};
constexpr mwfl::ControlId kMacroManage{403};
constexpr mwfl::ControlId kShortcutSettings{404};
constexpr mwfl::ControlId kExportConfiguration{405};
constexpr mwfl::ControlId kImportConfiguration{406};
constexpr mwfl::ControlId kPrint{407};
constexpr mwfl::ControlId kPrintPreview{408};
constexpr mwfl::ControlId kPrinterSettings{409};
constexpr mwfl::ControlId kExportText{410};
constexpr mwfl::ControlId kExportHtml{411};
constexpr mwfl::ControlId kDocumentStatistics{412};
constexpr mwfl::ControlId kPageSetup{413};

struct PrintOptions {
    double margin_inches = 0.5;
    bool header = true;
    bool footer = true;
    bool line_numbers = true;
    bool syntax_colours = true;
};
constexpr mwfl::ControlId kWordWrap{333};
constexpr mwfl::ControlId kZoomIn{334};
constexpr mwfl::ControlId kZoomOut{335};
constexpr mwfl::ControlId kZoomReset{336};
constexpr mwfl::ControlId kOpenFolder{340};
constexpr mwfl::ControlId kFindInFiles{341};
constexpr mwfl::ControlId kCancelSearch{342};
constexpr mwfl::ControlId kTree{350};
constexpr mwfl::ControlId kResults{351};
constexpr mwfl::ControlId kRegisterAssociation{360};
constexpr mwfl::ControlId kRemoveAssociation{361};
constexpr mwfl::ControlId kPreferences{362};
constexpr mwfl::ControlId kAbout{363};
constexpr mwfl::ControlId kToggleFindBar{364};
constexpr mwfl::ControlId kToggleWorkspace{365};
constexpr mwfl::ControlId kToggleResults{366};
constexpr UINT kSearchCompleteMessage = WM_APP + 0x241;
constexpr UINT kWorkspaceCompleteMessage = WM_APP + 0x242;
constexpr mwfl::TimerId kMonitorTimer{1};
constexpr mwfl::TimerId kActivationTestTimer{2};
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
    notepad_colon::FileState file_state;
    bool external_changed = false;
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

struct CompareScrollSync {
    mwfl::ScintillaEditor* left = nullptr;
    mwfl::ScintillaEditor* right = nullptr;
    bool updating = false;
};

LRESULT CALLBACK CompareEditorSubclass(HWND window, UINT message, WPARAM wparam,
                                       LPARAM lparam, UINT_PTR, DWORD_PTR reference) {
    const auto result = ::DefSubclassProc(window, message, wparam, lparam);
    if (message != WM_VSCROLL && message != WM_MOUSEWHEEL && message != WM_KEYUP) return result;
    auto* sync = reinterpret_cast<CompareScrollSync*>(reference);
    if (!sync || sync->updating) return result;
    auto* source = sync->left->GetHwnd() == window ? sync->left : sync->right;
    auto* target = source == sync->left ? sync->right : sync->left;
    sync->updating = true;
    target->Send(SCI_SETFIRSTVISIBLELINE, source->Send(SCI_GETFIRSTVISIBLELINE));
    sync->updating = false;
    return result;
}

enum class ComparisonLineEdit { replace, insert, remove };

bool EditComparisonLine(mwfl::ScintillaEditor& editor, std::size_t one_based_line,
                        std::wstring_view text, ComparisonLineEdit edit) {
    const auto line = static_cast<LRESULT>(one_based_line > 0 ? one_based_line - 1 : 0);
    const auto count = editor.Send(SCI_GETLINECOUNT);
    auto start = line < count ? editor.Send(SCI_POSITIONFROMLINE, line) : editor.GetLength();
    auto end = start;
    std::wstring replacement(text);
    if (edit == ComparisonLineEdit::replace || edit == ComparisonLineEdit::remove) {
        if (line >= count) return false;
        end = edit == ComparisonLineEdit::remove && line + 1 < count
            ? editor.Send(SCI_POSITIONFROMLINE, line + 1)
            : editor.Send(SCI_GETLINEENDPOSITION, line);
        if (edit == ComparisonLineEdit::remove) replacement.clear();
    } else {
        const auto eol = editor.Send(SCI_GETEOLMODE);
        replacement += eol == SC_EOL_CRLF ? L"\r\n" : eol == SC_EOL_CR ? L"\r" : L"\n";
    }
    const auto utf8 = mwfl::ToUtf8(replacement);
    if (!utf8) return false;
    const bool was_read_only = editor.IsReadOnly();
    if (was_read_only) editor.SetReadOnly(false);
    editor.Send(SCI_BEGINUNDOACTION);
    editor.Send(SCI_SETTARGETRANGE, start, end);
    editor.Send(SCI_REPLACETARGET, utf8->size(), reinterpret_cast<LPARAM>(utf8->data()));
    editor.Send(SCI_ENDUNDOACTION);
    if (was_read_only) editor.SetReadOnly(true);
    return true;
}

std::filesystem::path ExecutablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(),
                                              static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return path;
}

mwfl::FileAssociationSpec TextAssociation() {
    return {.extension = L".txt",
            .prog_id = L"mwfl.notepad-colon.text",
            .owner_id = L"mwfl.notepad-colon",
            .display_name = L"Text document",
            .executable = ExecutablePath(),
            .icon = ExecutablePath(),
            .verbs = {{L"open", L"Open with Notepad Colon", {}}}};
}

class MainWindow final : public mwfl::WindowBase {
public:
    MainWindow(mwfl::SingleInstance& instance,
               std::vector<std::filesystem::path> startup_paths,
               bool self_test, bool activation_test_server)
        : workspace_({1}, 12), instance_(instance),
          startup_paths_(std::move(startup_paths)), self_test_(self_test),
          activation_test_server_(activation_test_server) {}

    void BuildUI() override {
        static_cast<void>(mwfl::SetProcessAppUserModelId(L"mwfl.notepad-colon"));
        if (!runtime_.LoadAdjacent())
            throw std::runtime_error("Scintilla.dll is not available beside Notepad Colon");
        if (!lexilla_.LoadAdjacent())
            throw std::runtime_error("Lexilla.dll is not available beside Notepad Colon");
        if (!IsTestMode()) {
            const auto loaded = mwfl::LoadRecentFilesFromRegistry(HKEY_CURRENT_USER, kSettingsKey, 10);
            if (loaded.Succeeded()) recent_ = *loaded.value;
            LoadPreferences();
        }
        ApplyAppearance();
        BuildCommands();
        default_shortcuts_ = CaptureShortcuts();
        mwfl::EnableFileDrop(GetHwnd());

        mwfl::ControlHost ui{*this};
        ui.Add(toolbar_);
        ui.Add(search_, kSearch, L"");
        ui.Add(replacement_, kReplacement, L"");
        ui.Add(tree_, kTree, mwfl::RectDip{});
        ui.Add(tabs_, mwfl::TabControlOptions{});
        ui.Add(results_, kResults, mwfl::RectDip{}, mwfl::ListViewOptions{});
        ui.Add(status_);
        for (const auto id : {kNew, kOpen, kSave, kUndo, kRedo, kToggleFindBar})
            mwfl::Must(toolbar_.AddCommand(*commands_.Find(id)), "add toolbar command");
        toolbar_.AutoSize();
        mwfl::Must(mwfl::AddColumns(results_, {{L"File", 330}, {L"Line", 70},
                                                {L"Column", 70}, {L"Preview", 520}}),
                   "add search-result columns");
        results_.SetExtendedListStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
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
        mwfl::Must(mwfl::SetAccessibleName(tree_.GetHwnd(), L"Workspace files"), "name workspace tree");
        mwfl::Must(mwfl::SetAccessibleName(results_.GetHwnd(), L"Folder search results"), "name search results");

        ApplyCompactLayout();

        ResolveSessionPath();
        if (!RestoreSession()) NewDocument();
        mwfl::Must(instance_.RegisterWindow(GetHwnd()), "register single-instance window");
        for (const auto& path : startup_paths_) static_cast<void>(OpenPath(path));
        mwfl::Must(monitor_timer_.Start(*this, kMonitorTimer, std::chrono::milliseconds{2000}),
                   "start external-file monitor");
        if (IsSelfTest() && !::PostMessageW(GetHwnd(), kSelfTestMessage, 0, 0))
            throw std::runtime_error("could not schedule GUI self-test");
    }

    mwfl::EventResult OnCommand(const mwfl::CommandEvent& event) override {
        auto* command = commands_.Find(event.id);
        if (!command || !command->IsEnabled() || !command->IsVisible())
            return mwfl::EventResult::Propagate();
        if (!playing_macro_ && macro_recorder_.IsRecording() && IsMacroSafeCommand(event.id))
            macro_recorder_.RecordCommand(static_cast<std::uint16_t>(event.id.value));
        RememberCommand(event.id);
        command->Invoke();
        return mwfl::EventResult::Handled();
    }

    mwfl::EventResult OnNotify(const mwfl::NotifyEvent& event) override {
        if (event.Is(tabs_, TCN_SELCHANGE)) {
            adapter_.ActivateNativeSelection(workspace_);
            SyncPresentation(L"Document selected");
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
            if (!playing_macro_ && macro_recorder_.IsRecording() && notification->character > 0)
                macro_recorder_.RecordText(std::wstring(1, static_cast<wchar_t>(notification->character)));
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
        if (event.id == kSearchCompleteMessage) {
            CompleteSearch();
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
                if (!item.empty()) opened = OpenPath(item) || opened;
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

    mwfl::EventResult OnTimer(mwfl::TimerId id) override {
        if (id == kActivationTestTimer) {
            ::KillTimer(GetHwnd(), kActivationTestTimer.value);
            ::PostQuitMessage(activation_test_result_);
            return mwfl::EventResult::Handled();
        }
        if (id != kMonitorTimer) return mwfl::EventResult::Propagate();
        CheckExternalChanges(false);
        AutoSaveIfDue();
        SaveRecoverySnapshotsIfDue();
        return mwfl::EventResult::Handled();
    }

    mwfl::EventResult OnClose() override {
        monitor_timer_.Stop();
        StopWorkers();
        if (!IsTestMode()) {
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
        instance_.UnregisterWindow();
        return mwfl::EventResult::Propagate();
    }

private:
    bool IsSelfTest() const noexcept {
        return self_test_;
    }
    bool IsTestMode() const noexcept { return self_test_ || activation_test_server_; }

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
            .Add(mwfl::Command(kRegisterAssociation, L"Register .txt Association",
                [this] { ReportAssociation(true); }))
            .Add(mwfl::Command(kRemoveAssociation, L"Remove Owned .txt Association",
                [this] { ReportAssociation(false); }))
            .Add(mwfl::Command(kPreferences, L"&Preferences...",
                [this] { ShowPreferences(); }))
            .Add(mwfl::Command(kRecoveryManager, L"Recovery Manager...",
                [this] { ShowRecoveryManager(); }))
            .Add(mwfl::Command(kCompareWithFile, L"Compare with File...",
                [this] { CompareWithFile(); }))
            .Add(mwfl::Command(kCompareWithDisk, L"Compare with Saved Version",
                [this] { CompareWithDisk(); }))
            .Add(mwfl::Command(kAbout, L"&About Notepad Colon",
                [this] { ::MessageBoxW(GetHwnd(),
                    L"Notepad Colon 0.1.0-beta.1\nNative everyday code editing with MWFL and Scintilla.",
                    L"About Notepad Colon", MB_OK | MB_ICONINFORMATION); }));
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
                     .SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, 'D'}))
            .Add(mwfl::Command(kDeleteLine, L"Delete Line", [this] { if (auto* e = ActiveEditor()) notepad_colon::DeleteLine(*e); })
                     .SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, 'K'}))
            .Add(mwfl::Command(kUppercase, L"UPPERCASE", [this] { if (auto* e = ActiveEditor()) notepad_colon::ChangeCase(*e, true); }))
            .Add(mwfl::Command(kLowercase, L"lowercase", [this] { if (auto* e = ActiveEditor()) notepad_colon::ChangeCase(*e, false); }))
            .Add(mwfl::Command(kIndent, L"Indent", [this] { if (auto* e = ActiveEditor()) notepad_colon::IndentSelection(*e, true); }))
            .Add(mwfl::Command(kOutdent, L"Outdent", [this] { if (auto* e = ActiveEditor()) notepad_colon::IndentSelection(*e, false); }));
        commands_
            .Add(mwfl::Command(kSelectNext, L"Select Next Occurrence", [this] { if (auto* e = ActiveEditor()) notepad_colon::SelectNextOccurrence(*e, false); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'D'}))
            .Add(mwfl::Command(kSelectAllOccurrences, L"Select All Occurrences", [this] { if (auto* e = ActiveEditor()) notepad_colon::SelectNextOccurrence(*e, true); })
                     .SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, 'L'}))
            .Add(mwfl::Command(kSortAscending, L"Sort Lines Ascending", [this] { Transform([](auto s) { return notepad_colon::SortLines(s, notepad_colon::LineOrder::ascending); }); }))
            .Add(mwfl::Command(kSortDescending, L"Sort Lines Descending", [this] { Transform([](auto s) { return notepad_colon::SortLines(s, notepad_colon::LineOrder::descending); }); }))
            .Add(mwfl::Command(kUniqueLines, L"Remove Duplicate Lines", [this] { Transform([](auto s) { return notepad_colon::SortLines(s, notepad_colon::LineOrder::ascending, true); }); }))
            .Add(mwfl::Command(kReverseLines, L"Reverse Lines", [this] { Transform([](auto s) { return notepad_colon::SortLines(s, notepad_colon::LineOrder::reverse); }); }))
            .Add(mwfl::Command(kRemoveBlankLines, L"Remove Blank Lines", [this] { Transform([](auto s) { return notepad_colon::RemoveBlankLines(s); }); }))
            .Add(mwfl::Command(kTrimTrailing, L"Trim Trailing Whitespace", [this] { Transform(notepad_colon::TrimTrailingWhitespace); }))
            .Add(mwfl::Command(kJoinLines, L"Join Lines", [this] { Transform([](auto s) { return notepad_colon::JoinLines(s); }); }))
            .Add(mwfl::Command(kSplitLines, L"Split Lines at Column 80", [this] { Transform([](auto s) { return notepad_colon::SplitLines(s, 80); }); }))
            .Add(mwfl::Command(kTabsToSpaces, L"Convert Tabs to Spaces", [this] { Transform([this](auto s) { return notepad_colon::TabsToSpaces(s, static_cast<std::size_t>(preferences_.tab_width)); }); }))
            .Add(mwfl::Command(kSpacesToTabs, L"Convert Spaces to Tabs", [this] { Transform([this](auto s) { return notepad_colon::SpacesToTabs(s, static_cast<std::size_t>(preferences_.tab_width)); }); }))
            .Add(mwfl::Command(kTitleCase, L"Title Case", [this] { Transform([](auto s) { return notepad_colon::ConvertCase(s, notepad_colon::LetterCase::title); }); }))
            .Add(mwfl::Command(kSentenceCase, L"Sentence case", [this] { Transform([](auto s) { return notepad_colon::ConvertCase(s, notepad_colon::LetterCase::sentence); }); }))
            .Add(mwfl::Command(kJsonEscape, L"Escape JSON String", [this] { Transform(notepad_colon::EscapeJsonString); }))
            .Add(mwfl::Command(kJsonUnescape, L"Unescape JSON String", [this] { Transform([](auto s) { const auto value = notepad_colon::UnescapeJsonString(s); return value.value_or(std::wstring{s}); }); }))
            .Add(mwfl::Command(kToggleComment, L"Toggle Line Comment", [this] { if (auto* e = ActiveEditor()) notepad_colon::ToggleLineComment(*e, "//"); })
                     .SetShortcut({FVIRTKEY | FCONTROL, VK_OEM_2}))
            .Add(mwfl::Command(kBlockComment, L"Wrap in Block Comment", [this] { if (auto* e = ActiveEditor()) notepad_colon::WrapSelection(*e, L"/* ", L" */"); }))
            .Add(mwfl::Command(kBase64Encode, L"Base64 Encode", [this] { Transform([](auto s) { const auto bytes = mwfl::ToUtf8(s); const auto encoded = bytes ? notepad_colon::Base64Encode(*bytes) : std::string{}; return mwfl::FromUtf8(encoded).value_or(std::wstring{}); }); }))
            .Add(mwfl::Command(kBase64Decode, L"Base64 Decode", [this] { Transform([](auto s) { const auto bytes = mwfl::ToUtf8(s); const auto decoded = bytes ? notepad_colon::Base64Decode(*bytes) : std::nullopt; return decoded ? mwfl::FromUtf8(*decoded).value_or(std::wstring{s}) : std::wstring{s}; }); }))
            .Add(mwfl::Command(kUrlEncode, L"URL Encode", [this] { Transform([](auto s) { const auto bytes = mwfl::ToUtf8(s); return bytes ? mwfl::FromUtf8(notepad_colon::UrlEncode(*bytes)).value_or(std::wstring{s}) : std::wstring{s}; }); }))
            .Add(mwfl::Command(kUrlDecode, L"URL Decode", [this] { Transform([](auto s) { const auto bytes = mwfl::ToUtf8(s); const auto decoded = bytes ? notepad_colon::UrlDecode(*bytes) : std::nullopt; return decoded ? mwfl::FromUtf8(*decoded).value_or(std::wstring{s}) : std::wstring{s}; }); }))
            .Add(mwfl::Command(kInsertDateTime, L"Insert Date / Time", [this] { InsertDateTime(); }))
            .Add(mwfl::Command(kInsertSequence, L"Insert Sequence 1-10", [this] { if (auto* e = ActiveEditor()) notepad_colon::InsertText(*e, notepad_colon::GenerateSequence(1, 10, 1)); }));
        commands_
            .Add(mwfl::Command(kWordWrap, L"Word Wrap", [this] { if (auto* e = ActiveEditor()) notepad_colon::ToggleWordWrap(*e); }))
            .Add(mwfl::Command(kZoomIn, L"Zoom In", [this] { if (auto* e = ActiveEditor()) e->SetZoom(e->GetZoom() + 1); })
                     .SetShortcut({FVIRTKEY | FCONTROL, VK_OEM_PLUS}))
            .Add(mwfl::Command(kZoomOut, L"Zoom Out", [this] { if (auto* e = ActiveEditor()) e->SetZoom(e->GetZoom() - 1); })
                     .SetShortcut({FVIRTKEY | FCONTROL, VK_OEM_MINUS}))
            .Add(mwfl::Command(kZoomReset, L"Reset Zoom", [this] { if (auto* e = ActiveEditor()) e->SetZoom(0); })
                     .SetShortcut({FVIRTKEY | FCONTROL, '0'}));
        commands_
            .Add(mwfl::Command(kOpenFolder, L"Open &Folder...", [this] { OpenFolderInteractive(); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'K'}))
            .Add(mwfl::Command(kFindInFiles, L"Find in Files", [this] { StartFolderSearch(); })
                     .SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, 'F'}))
            .Add(mwfl::Command(kCancelSearch, L"Cancel Folder Search", [this] { CancelFolderSearch(); }));
        commands_
            .Add(mwfl::Command(kToggleFindBar, L"Find / Replace Bar", [this] {
                find_bar_visible_ = !find_bar_visible_; ApplyCompactLayout();
                if (find_bar_visible_) search_.Focus();
            }).SetShortcut({FVIRTKEY | FCONTROL, 'F'}))
            .Add(mwfl::Command(kToggleWorkspace, L"Workspace Panel", [this] {
                workspace_visible_ = !workspace_visible_; ApplyCompactLayout();
            }).SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, 'E'}))
            .Add(mwfl::Command(kToggleResults, L"Search Results Panel", [this] {
                results_visible_ = !results_visible_; ApplyCompactLayout();
            }).SetShortcut({FVIRTKEY | FCONTROL, 'J'}));
        commands_
            .Add(mwfl::Command(kCommandPalette, L"Command Palette...", [this] { ShowCommandPalette(); })
                     .SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, 'P'}))
            .Add(mwfl::Command(kMacroStart, L"Start Macro Recording", [this] { StartMacroRecording(); }))
            .Add(mwfl::Command(kMacroStop, L"Stop Macro Recording", [this] { StopMacroRecording(); }))
            .Add(mwfl::Command(kMacroPlay, L"Play Last Macro", [this] { PlayLastMacro(1); })
                     .SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, 'R'}))
            .Add(mwfl::Command(kMacroPlayFive, L"Play Last Macro 5 Times", [this] { PlayLastMacro(5); }))
            .Add(mwfl::Command(kMacroSave, L"Save Last Macro...", [this] { SaveLastMacro(); }))
            .Add(mwfl::Command(kMacroManage, L"Manage Macros...", [this] { ShowMacroManager(); }))
            .Add(mwfl::Command(kShortcutSettings, L"Keyboard Shortcuts...", [this] { ShowShortcutSettings(); }))
            .Add(mwfl::Command(kExportConfiguration, L"Export Settings...", [this] { ExportConfiguration(); }))
            .Add(mwfl::Command(kImportConfiguration, L"Import Settings...", [this] { ImportConfiguration(); }));
        commands_
            .Add(mwfl::Command(kPrint, L"&Print...", [this] { PrintActive(); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'P'}))
            .Add(mwfl::Command(kPrintPreview, L"Print Pre&view...", [this] { ShowPrintPreview(); }))
            .Add(mwfl::Command(kPrinterSettings, L"Printer Settings...", [this] { ConfigurePrinter(); }))
            .Add(mwfl::Command(kPageSetup, L"Page Setup...", [this] { ShowPageSetup(); }))
            .Add(mwfl::Command(kExportText, L"Export Plain Text...", [this] { ExportDocument(false); }))
            .Add(mwfl::Command(kExportHtml, L"Export HTML...", [this] { ExportDocument(true); }))
            .Add(mwfl::Command(kDocumentStatistics, L"Document Statistics...", [this] { ShowDocumentStatistics(); }));
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
        mwfl::Menu file, edit, search, encoding, line_endings, view, code, tools, help;
        mwfl::Must(menu_.Create(), "create menu bar");
        mwfl::Must(file.CreatePopup(), "create file menu");
        mwfl::Must(edit.CreatePopup(), "create edit menu");
        mwfl::Must(search.CreatePopup(), "create search menu");
        mwfl::Must(encoding.CreatePopup(), "create encoding menu");
        mwfl::Must(line_endings.CreatePopup(), "create line endings menu");
        mwfl::Must(view.CreatePopup(), "create view menu");
        mwfl::Must(code.CreatePopup(), "create code menu");
        mwfl::Must(tools.CreatePopup(), "create tools menu");
        mwfl::Must(help.CreatePopup(), "create help menu");
        for (const auto id : {kNew, kOpen, kOpenFolder, kSave, kSaveAs, kSaveAll,
                              kExportText, kExportHtml, kPrintPreview, kPrint,
                              kPageSetup, kPrinterSettings, kClose})
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
        for (const auto id : {kFindNext, kReplaceNext, kReplaceAll, kFindInFiles, kCancelSearch})
            mwfl::Must(search.AppendCommand(*commands_.Find(id)), "append search command");
        for (const auto id : {kUtf8, kUtf8Bom, kUtf16Le, kUtf16Be})
            mwfl::Must(encoding.AppendCommand(*commands_.Find(id)), "append encoding command");
        for (const auto id : {kCrlf, kLf})
            mwfl::Must(line_endings.AppendCommand(*commands_.Find(id)), "append line ending command");
        for (const auto id : {kToggleFindBar, kToggleWorkspace, kToggleResults,
                              kWhitespace, kWordWrap, kZoomIn, kZoomOut, kZoomReset,
                              kRectangular, kToggleFold, kToggleBookmark, kNextBookmark})
            mwfl::Must(view.AppendCommand(*commands_.Find(id)), "append view command");
        for (const auto id : {kMoveLineUp, kMoveLineDown, kDuplicateLine, kDeleteLine,
                              kUppercase, kLowercase, kTitleCase, kSentenceCase,
                              kIndent, kOutdent, kToggleComment, kSelectNext,
                              kSelectAllOccurrences, kSortAscending, kSortDescending,
                              kUniqueLines, kReverseLines, kRemoveBlankLines, kTrimTrailing,
                              kJoinLines, kSplitLines, kTabsToSpaces, kSpacesToTabs,
                              kJsonEscape, kJsonUnescape, kBlockComment,
                              kBase64Encode, kBase64Decode, kUrlEncode, kUrlDecode,
                              kInsertDateTime, kInsertSequence})
            mwfl::Must(code.AppendCommand(*commands_.Find(id)), "append code command");
        for (const auto id : {kCommandPalette, kMacroStart, kMacroStop, kMacroPlay,
                              kMacroPlayFive, kMacroSave, kMacroManage,
                              kShortcutSettings, kExportConfiguration, kImportConfiguration,
                              kDocumentStatistics,
                              kPreferences, kRecoveryManager, kCompareWithFile,
                              kCompareWithDisk, kRegisterAssociation, kRemoveAssociation})
            mwfl::Must(tools.AppendCommand(*commands_.Find(id)), "append tools command");
        mwfl::Must(help.AppendCommand(*commands_.Find(kAbout)), "append about command");
        mwfl::Must(menu_.AppendSubmenu(std::move(file), L"&File"), "append file menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(edit), L"&Edit"), "append edit menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(search), L"&Search"), "append search menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(encoding), L"En&coding"), "append encoding menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(line_endings), L"&EOL"), "append line endings menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(view), L"&View"), "append view menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(code), L"&Code"), "append code menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(tools), L"&Tools"), "append tools menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(help), L"&Help"), "append help menu");
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

    void ApplyCompactLayout() {
        ::ShowWindow(search_.GetHwnd(), find_bar_visible_ ? SW_SHOW : SW_HIDE);
        ::ShowWindow(replacement_.GetHwnd(), find_bar_visible_ ? SW_SHOW : SW_HIDE);
        ::ShowWindow(tree_.GetHwnd(), workspace_visible_ ? SW_SHOW : SW_HIDE);
        ::ShowWindow(results_.GetHwnd(), results_visible_ ? SW_SHOW : SW_HIDE);
        SetLayout(mwfl::Column()
            .Add(toolbar_, mwfl::Auto())
            .Add(mwfl::Row().Gap(4.0_dip).Margin(3.0_dip)
                .Add(search_, mwfl::Stretch())
                .Add(replacement_, mwfl::Stretch()),
                mwfl::Fixed(find_bar_visible_ ? 30.0_dip : 0.0_dip))
            .Add(mwfl::Row().Gap(workspace_visible_ ? 3.0_dip : 0.0_dip)
                .Add(tree_, mwfl::Fixed(workspace_visible_ ? 220.0_dip : 0.0_dip))
                .Add(mwfl::Column().Gap(results_visible_ ? 3.0_dip : 0.0_dip)
                    .Add(tabs_, mwfl::Stretch())
                    .Add(results_, mwfl::Fixed(results_visible_ ? 135.0_dip : 0.0_dip)),
                    mwfl::Stretch()), mwfl::Stretch())
            .Add(status_, mwfl::Fixed(22.0_dip)));
        adapter_.ArrangePages();
        ::InvalidateRect(GetHwnd(), nullptr, TRUE);
    }

    template <typename TransformFunction>
    void Transform(TransformFunction&& transform) {
        if (auto* editor = ActiveEditor(); editor &&
            notepad_colon::TransformSelectionOrDocument(*editor,
                std::forward<TransformFunction>(transform)))
            status_.SetText(L"Text transformed");
    }

    void NewDocument(std::wstring text = {}, std::wstring title = {}) {
        const mwfl::DocumentId id{next_id_++};
        if (title.empty()) title = id.value == 1 ? L"Untitled" : L"Untitled " + std::to_wstring(id.value);
        auto editor = std::make_unique<mwfl::ScintillaEditor>();
        mwfl::Must(editor->Create(GetHwnd(), mwfl::ControlId{static_cast<WORD>(200 + id.value)},
                                  mwfl::RectDip{}, runtime_), "create document editor");
        mwfl::Must(editor->ConfigureCodeEditing(), "configure document editor");
        notepad_colon::ConfigureAdvancedEditing(*editor);
        notepad_colon::ApplyPreferences(*editor, preferences_, IsDark());
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
        std::error_code size_error;
        const auto size = std::filesystem::file_size(path, size_error);
        if (size_error) return false;
        const auto open_mode = notepad_colon::ClassifyFileSize(size);
        if (open_mode == notepad_colon::FileOpenMode::unsupported) {
            status_.SetText(L"File exceeds the 256 MiB safety limit");
            return false;
        }
        const auto loaded = mwfl::ReadTextFile(path);
        if (!loaded.Succeeded()) return false;
        const mwfl::DocumentId id{next_id_++};
        auto editor = std::make_unique<mwfl::ScintillaEditor>();
        if (!editor->Create(GetHwnd(), mwfl::ControlId{static_cast<WORD>(200 + id.value)},
                            mwfl::RectDip{}, runtime_) ||
            !editor->ConfigureCodeEditing() || !editor->SetText(loaded.value->text)) return false;
        notepad_colon::ConfigureAdvancedEditing(*editor);
        notepad_colon::ApplyPreferences(*editor, preferences_, IsDark());
        const auto language = notepad_colon::DetectLanguage(path);
        if (!notepad_colon::ConfigureLanguage(*editor, lexilla_, language)) return false;
        const bool protected_mode = open_mode == notepad_colon::FileOpenMode::protected_read_only;
        if (protected_mode && !editor->SetReadOnly(true)) return false;
        editor->SetSavePoint();
        const auto title = path.filename().wstring();
        if (!workspace_.Add({id, title, path})) return false;
        if (adapter_.BindPage(id, editor->GetHwnd()) != mwfl::DocumentTabStatus::success) return false;
        mwfl::SetAccessibleName(editor->GetHwnd(), title.c_str());
        documents_.push_back({id, std::move(editor), loaded.value->encoding,
                              notepad_colon::DetectLineEnding(loaded.value->text), loaded.value->stamp,
                              protected_mode, language, notepad_colon::CaptureFileState(path)});
        workspace_.Activate(id);
        SynchronizeTabs(true);
        SyncPresentation(protected_mode ? L"Opened in large-file read-only mode" : L"Opened");
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
        if (document.read_only) {
            status_.SetText(L"Large-file protection is read-only");
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
                const auto selected = mwfl::ShowSaveFileDialog({.owner = GetHwnd(), .title = L"Save",
                    .filters = {{L"Text files", L"*.txt"}, {L"All files", L"*.*"}},
                    .default_extension = L"txt", .path_must_exist = false});
                if (!selected.accepted) return false;
                path = selected.path;
            }
        }
        auto text = document.editor->GetText();
        if (!text) return false;
        auto prepared = preferences_.trim_trailing_whitespace_on_save
            ? notepad_colon::TrimTrailingWhitespace(*text) : *text;
        if (preferences_.ensure_final_newline) {
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
                status_.SetText(L"Backup could not be created; save cancelled");
                return false;
            }
        }
        const auto expected = path == metadata->path ? document.stamp : std::nullopt;
        const auto saved = mwfl::WriteTextFileAtomic(path, *text, document.encoding, expected);
        if (!saved.Succeeded() || !saved.stamp) return false;
        document.stamp = saved.stamp;
        document.file_state = notepad_colon::CaptureFileState(path);
        document.external_changed = false;
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

    void ReportAssociation(bool install) {
        const auto result = install ? mwfl::RegisterPerUserFileAssociation(TextAssociation())
                                    : mwfl::RemovePerUserFileAssociation(TextAssociation());
        status_.SetText(result ? (install ? L".txt association registered for this user"
                                          : L"Owned .txt association removed")
                               : L"Association unchanged; Windows ownership/conflict protected");
    }

    bool IsDark() const noexcept {
        return mwfl::ResolveAppearance({ToColorMode(preferences_.theme)}).IsDark();
    }

    static mwfl::ColorMode ToColorMode(notepad_colon::ThemePreference theme) noexcept {
        switch (theme) {
        case notepad_colon::ThemePreference::light: return mwfl::ColorMode::light;
        case notepad_colon::ThemePreference::dark: return mwfl::ColorMode::dark;
        case notepad_colon::ThemePreference::system: return mwfl::ColorMode::system;
        }
        return mwfl::ColorMode::system;
    }

    void ApplyAppearance() {
        SetAppearance({ToColorMode(preferences_.theme), mwfl::Backdrop::mica});
        const bool dark = IsDark();
        for (auto& document : documents_) {
            notepad_colon::ApplyPreferences(*document.editor, preferences_, dark);
            static_cast<void>(notepad_colon::ConfigureLanguage(
                *document.editor, lexilla_, document.language));
        }
    }

    void LoadPreferences() {
        const std::array schema{
            mwfl::SettingDefinition{L"FontName", mwfl::SettingType::string, 256, true},
            mwfl::SettingDefinition{L"FontSize", mwfl::SettingType::dword, 4, true},
            mwfl::SettingDefinition{L"TabWidth", mwfl::SettingType::dword, 4, true},
            mwfl::SettingDefinition{L"Theme", mwfl::SettingType::dword, 4, true},
            mwfl::SettingDefinition{L"AutoSave", mwfl::SettingType::dword, 4, false},
            mwfl::SettingDefinition{L"AutoSaveSeconds", mwfl::SettingType::dword, 4, false},
            mwfl::SettingDefinition{L"TrimTrailingOnSave", mwfl::SettingType::dword, 4, false},
            mwfl::SettingDefinition{L"CreateBackup", mwfl::SettingType::dword, 4, false},
            mwfl::SettingDefinition{L"EnsureFinalNewline", mwfl::SettingType::dword, 4, false}};
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
        preferences_ = notepad_colon::SanitizePreferences(std::move(candidate));
    }

    bool SavePreferences() {
        const std::array values{
            mwfl::SettingValue{L"FontName", preferences_.font_name},
            mwfl::SettingValue{L"FontSize", preferences_.font_size},
            mwfl::SettingValue{L"TabWidth", preferences_.tab_width},
            mwfl::SettingValue{L"Theme", static_cast<std::uint32_t>(preferences_.theme)},
            mwfl::SettingValue{L"AutoSave", static_cast<std::uint32_t>(preferences_.auto_save)},
            mwfl::SettingValue{L"AutoSaveSeconds", preferences_.auto_save_seconds},
            mwfl::SettingValue{L"TrimTrailingOnSave", static_cast<std::uint32_t>(preferences_.trim_trailing_whitespace_on_save)},
            mwfl::SettingValue{L"CreateBackup", static_cast<std::uint32_t>(preferences_.create_backup_before_save)},
            mwfl::SettingValue{L"EnsureFinalNewline", static_cast<std::uint32_t>(preferences_.ensure_final_newline)}};
        return static_cast<bool>(settings_.Save(values));
    }

    void ShowPreferences() {
        mwfl::Label font_label, size_label, tab_label, theme_label, theme_value;
        mwfl::TextBox font, size, tab;
        mwfl::Button system, light, dark, accept, cancel;
        mwfl::CheckBox auto_save, trim_trailing, create_backup, final_newline;
        auto candidate = preferences_;
        mwfl::Dialog* pointer = nullptr;
        mwfl::Dialog dialog({
            .owner = GetHwnd(), .title = L"Notepad Colon Preferences",
            .initial_client_size = {500.0_dip, 390.0_dip}, .resizable = false,
            .callbacks = {
                .initialize = [&](HWND window) {
                    mwfl::ControlHost ui{window};
                    ui.Add(font_label, {401}, L"Font family"); ui.Add(font, {402}, candidate.font_name);
                    ui.Add(size_label, {403}, L"Font size (8-40)"); ui.Add(size, {404}, std::to_wstring(candidate.font_size));
                    ui.Add(tab_label, {405}, L"Tab width (1-16)"); ui.Add(tab, {406}, std::to_wstring(candidate.tab_width));
                    ui.Add(theme_label, {407}, L"Theme"); ui.Add(theme_value, {408}, L"");
                    ui.Add(system, {409}, L"System"); ui.Add(light, {410}, L"Light"); ui.Add(dark, {411}, L"Dark");
                    ui.Add(auto_save, {412}, L"Auto-save named documents every 30 seconds");
                    ui.Add(trim_trailing, {413}, L"Trim trailing whitespace when saving");
                    ui.Add(create_backup, {414}, L"Create .bak file before overwriting");
                    ui.Add(final_newline, {415}, L"Ensure final newline when saving");
                    auto_save.SetChecked(candidate.auto_save);
                    trim_trailing.SetChecked(candidate.trim_trailing_whitespace_on_save);
                    create_backup.SetChecked(candidate.create_backup_before_save);
                    final_newline.SetChecked(candidate.ensure_final_newline);
                    ui.Add(accept, {IDOK}, L"Save"); ui.Add(cancel, {IDCANCEL}, L"Cancel");
                    const auto update_theme = [&] { theme_value.SetText(
                        candidate.theme == notepad_colon::ThemePreference::system ? L"System" :
                        candidate.theme == notepad_colon::ThemePreference::light ? L"Light" : L"Dark"); };
                    update_theme();
                    mwfl::SetAccessibleName(font.GetHwnd(), L"Editor font family");
                    mwfl::SetAccessibleName(size.GetHwnd(), L"Editor font size");
                    mwfl::SetAccessibleName(tab.GetHwnd(), L"Editor tab width");
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
                        theme_value.SetText(id == 409 ? L"System" : id == 410 ? L"Light" : L"Dark");
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
                            ::MessageBoxW(pointer->GetHwnd(), L"Enter a valid font, size (8-40), and tab width (1-16).", L"Preferences", MB_OK | MB_ICONWARNING);
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
            SyncPresentation(L"Preferences applied");
        }
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

    void SaveRecoverySnapshotsIfDue() {
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

    void ShowRecoveryManager() {
        if (!recovery_store_) return;
        auto snapshots = recovery_store_->List();
        mwfl::ListBox list;
        mwfl::Label details;
        mwfl::Button restore, remove, close;
        std::optional<std::size_t> chosen;
        mwfl::Dialog* pointer = nullptr;
        mwfl::Dialog dialog({
            .owner = GetHwnd(), .title = L"Recovery Manager",
            .initial_client_size = {620.0_dip, 340.0_dip}, .resizable = true,
            .callbacks = {
                .initialize = [&](HWND window) {
                    mwfl::ControlHost ui{window};
                    ui.Add(list, {501}, {});
                    ui.Add(details, {502}, L"Local snapshots, newest first");
                    ui.Add(restore, {503}, L"Restore as new document");
                    ui.Add(remove, {504}, L"Delete snapshot");
                    ui.Add(close, {IDCANCEL}, L"Close");
                    for (const auto& item : snapshots) {
                        const auto source = item.original_path.empty() ? L"Unsaved" : item.original_path.wstring();
                        list.AddItem(item.title + L"  —  " + source + L"  (" +
                                     std::to_wstring(item.size) + L" bytes)");
                    }
                    if (!snapshots.empty()) list.SetSelection(0);
                    mwfl::SetAccessibleName(list.GetHwnd(), L"Recovery snapshots");
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
        if (text) NewDocument(*text, L"Recovered — " + snapshots[*chosen].title);
    }

    void CompareWithFile() {
        const auto* document = ActiveDocument();
        if (!document) return;
        const auto left = document->editor->GetText();
        if (!left) return;
        const auto selected = mwfl::ShowOpenFileDialog({
            .owner = GetHwnd(), .title = L"Choose file to compare",
            .filters = {{L"Text and source files", L"*.txt;*.md;*.cpp;*.h;*.json;*.xml;*.ini;*.yaml;*.yml;*.ps1"},
                        {L"All files", L"*.*"}}});
        if (!selected.accepted) return;
        const auto loaded = mwfl::ReadTextFile(selected.path);
        if (!loaded.Succeeded()) { status_.SetText(L"Comparison file could not be opened"); return; }
        ShowComparison(selected.path.filename().wstring(), *left, loaded.value->text);
    }

    void CompareWithDisk() {
        const auto* document = ActiveDocument();
        const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
        if (!document || !metadata || metadata->path.empty()) {
            status_.SetText(L"Save the document before comparing with its saved version");
            return;
        }
        const auto left = document->editor->GetText();
        const auto loaded = mwfl::ReadTextFile(metadata->path);
        if (!left || !loaded.Succeeded()) { status_.SetText(L"Saved version could not be read"); return; }
        ShowComparison(L"Saved — " + metadata->title, *left, loaded.value->text);
    }

    void ShowComparison(std::wstring right_title, std::wstring left_text,
                        std::wstring right_text) {
        mwfl::ScintillaEditor left, right;
        mwfl::Label left_label, right_label, summary;
        mwfl::CheckBox ignore_case, ignore_whitespace, ignore_eol;
        mwfl::Button previous, next, copy_left, copy_right, apply, close;
        notepad_colon::ComparisonResult comparison;
        std::vector<std::size_t> changes;
        std::size_t current = 0;
        std::optional<std::wstring> applied_text;
        CompareScrollSync sync{&left, &right};
        mwfl::Dialog* pointer = nullptr;

        const auto refresh = [&] {
            const auto left_value = left.GetText();
            const auto right_value = right.GetText();
            if (!left_value || !right_value) return;
            comparison = notepad_colon::CompareText(*left_value, *right_value,
                {.ignore_case = ignore_case.IsChecked(),
                 .ignore_whitespace = ignore_whitespace.IsChecked(),
                 .ignore_line_endings = ignore_eol.IsChecked()});
            changes.clear();
            left.Send(SCI_MARKERDELETEALL, 10); right.Send(SCI_MARKERDELETEALL, 10);
            left.Send(SCI_SETINDICATORCURRENT, 20); left.Send(SCI_INDICATORCLEARRANGE, 0, left.GetLength());
            right.Send(SCI_SETINDICATORCURRENT, 20); right.Send(SCI_INDICATORCLEARRANGE, 0, right.GetLength());
            const auto mark_span = [](mwfl::ScintillaEditor& editor, std::size_t line,
                                      std::wstring_view line_text, notepad_colon::TextSpan span) {
                if (!line || !span.length) return;
                const auto prefix = mwfl::ToUtf8(line_text.substr(0, span.start));
                const auto changed = mwfl::ToUtf8(line_text.substr(span.start, span.length));
                if (!prefix || !changed) return;
                const auto position = editor.Send(SCI_POSITIONFROMLINE, line - 1) +
                    static_cast<LRESULT>(prefix->size());
                editor.Send(SCI_SETINDICATORCURRENT, 20);
                editor.Send(SCI_INDICATORFILLRANGE, position, changed->size());
            };
            for (std::size_t index = 0; index < comparison.lines.size(); ++index) {
                const auto& difference = comparison.lines[index];
                if (difference.kind == notepad_colon::DifferenceKind::equal) continue;
                changes.push_back(index);
                if (difference.left_line) left.Send(SCI_MARKERADD, difference.left_line - 1, 10);
                if (difference.right_line) right.Send(SCI_MARKERADD, difference.right_line - 1, 10);
                mark_span(left, difference.left_line, difference.left_text, difference.left_change);
                mark_span(right, difference.right_line, difference.right_text, difference.right_change);
            }
            if (current >= changes.size()) current = changes.empty() ? 0 : changes.size() - 1;
            summary.SetText(comparison.identical ? L"Files are identical" :
                std::to_wstring(comparison.changed_lines) + L" changed line(s)  •  Difference " +
                std::to_wstring(current + 1) + L" of " + std::to_wstring(changes.size()));
        };
        const auto go_to_current = [&] {
            if (changes.empty()) return;
            const auto& difference = comparison.lines[changes[current]];
            if (difference.left_line) left.Send(SCI_GOTOLINE, difference.left_line - 1);
            if (difference.right_line) right.Send(SCI_GOTOLINE, difference.right_line - 1);
            summary.SetText(std::to_wstring(comparison.changed_lines) + L" changed line(s)  •  Difference " +
                std::to_wstring(current + 1) + L" of " + std::to_wstring(changes.size()));
        };
        const auto copy_difference = [&](bool right_to_left) {
            if (changes.empty()) return;
            const auto difference = comparison.lines[changes[current]];
            if (right_to_left) {
                const auto edit = difference.kind == notepad_colon::DifferenceKind::inserted
                    ? ComparisonLineEdit::insert : difference.kind == notepad_colon::DifferenceKind::deleted
                    ? ComparisonLineEdit::remove : ComparisonLineEdit::replace;
                static_cast<void>(EditComparisonLine(left, difference.left_line,
                                                     difference.right_text, edit));
            } else {
                const auto edit = difference.kind == notepad_colon::DifferenceKind::inserted
                    ? ComparisonLineEdit::remove : difference.kind == notepad_colon::DifferenceKind::deleted
                    ? ComparisonLineEdit::insert : ComparisonLineEdit::replace;
                static_cast<void>(EditComparisonLine(right, difference.right_line,
                                                     difference.left_text, edit));
            }
            refresh();
            go_to_current();
        };

        mwfl::Dialog dialog({
            .owner = GetHwnd(), .title = L"Compare Text",
            .initial_client_size = {980.0_dip, 620.0_dip}, .resizable = true,
            .callbacks = {
                .initialize = [&](HWND window) {
                    mwfl::ControlHost ui{window};
                    ui.Add(left_label, {601}, L"Current document");
                    ui.Add(right_label, {602}, right_title);
                    ui.Add(summary, {603}, L"");
                    ui.Add(ignore_case, {604}, L"Ignore case");
                    ui.Add(ignore_whitespace, {605}, L"Ignore whitespace");
                    ui.Add(ignore_eol, {612}, L"Ignore EOL");
                    ignore_eol.SetChecked(true);
                    ui.Add(previous, {606}, L"Previous"); ui.Add(next, {607}, L"Next");
                    ui.Add(copy_left, {608}, L"← Use right");
                    ui.Add(copy_right, {609}, L"Use left →");
                    ui.Add(apply, {IDOK}, L"Apply current side"); ui.Add(close, {IDCANCEL}, L"Close");
                    if (!left.Create(window, {610}, {}, runtime_) ||
                        !right.Create(window, {611}, {}, runtime_)) return false;
                    left.ConfigureCodeEditing(); right.ConfigureCodeEditing();
                    notepad_colon::ApplyPreferences(left, preferences_, IsDark());
                    notepad_colon::ApplyPreferences(right, preferences_, IsDark());
                    left.SetText(left_text); right.SetText(right_text);
                    left.SetReadOnly(true); right.SetReadOnly(true);
                    for (auto* editor : {&left, &right}) {
                        editor->Send(SCI_MARKERDEFINE, 10, SC_MARK_BACKGROUND);
                        editor->Send(SCI_MARKERSETBACK, 10, IsDark() ? RGB(82, 66, 20) : RGB(255, 241, 184));
                        editor->Send(SCI_INDICSETSTYLE, 20, INDIC_ROUNDBOX);
                        editor->Send(SCI_INDICSETFORE, 20, IsDark() ? RGB(255, 190, 60) : RGB(190, 90, 0));
                        editor->Send(SCI_INDICSETALPHA, 20, 70);
                        ::SetWindowSubclass(editor->GetHwnd(), CompareEditorSubclass, 1,
                                            reinterpret_cast<DWORD_PTR>(&sync));
                    }
                    mwfl::SetAccessibleName(left.GetHwnd(), L"Current document comparison side");
                    mwfl::SetAccessibleName(right.GetHwnd(), L"Comparison file side");
                    const auto layout = pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(5.0_dip)
                        .Add(mwfl::Row().Gap(6.0_dip).Add(left_label, mwfl::Stretch())
                            .Add(right_label, mwfl::Stretch()), mwfl::Fixed(24.0_dip))
                        .Add(mwfl::Row().Gap(5.0_dip).Add(left, mwfl::Stretch())
                            .Add(right, mwfl::Stretch()), mwfl::Stretch())
                        .Add(mwfl::Row().Gap(6.0_dip).Add(summary, mwfl::Stretch())
                            .Add(ignore_case, mwfl::Auto()).Add(ignore_whitespace, mwfl::Auto())
                            .Add(ignore_eol, mwfl::Auto()),
                            mwfl::Fixed(28.0_dip))
                        .Add(mwfl::Row().Gap(6.0_dip).Add(previous, mwfl::Auto()).Add(next, mwfl::Auto())
                            .Add(copy_left, mwfl::Auto()).Add(copy_right, mwfl::Auto())
                            .Add(mwfl::Column(), mwfl::Stretch()).Add(apply, mwfl::Auto())
                            .Add(close, mwfl::Fixed(80.0_dip)), mwfl::Fixed(30.0_dip)));
                    refresh(); go_to_current();
                    return layout;
                },
                .command = [&](HWND, WORD id, WORD) {
                    if (id == 604 || id == 605 || id == 612) { refresh(); go_to_current(); return true; }
                    if (id == 606 && !changes.empty()) { current = current ? current - 1 : changes.size() - 1; go_to_current(); return true; }
                    if (id == 607 && !changes.empty()) { current = (current + 1) % changes.size(); go_to_current(); return true; }
                    if (id == 608) { copy_difference(true); return true; }
                    if (id == 609) { copy_difference(false); return true; }
                    if (id == IDOK) { applied_text = left.GetText(); pointer->Accept(); return true; }
                    return false;
                },
                .destroyed = [&] {
                    if (left.GetHwnd()) ::RemoveWindowSubclass(left.GetHwnd(), CompareEditorSubclass, 1);
                    if (right.GetHwnd()) ::RemoveWindowSubclass(right.GetHwnd(), CompareEditorSubclass, 1);
                }}});
        pointer = &dialog;
        static_cast<void>(dialog.ShowModal());
        if (applied_text) {
            if (auto* editor = ActiveEditor())
                static_cast<void>(notepad_colon::ReplaceDocumentText(
                    *editor, *applied_text, editor->GetSelection()));
            SyncPresentation(L"Comparison changes applied");
        }
    }

    static bool IsMacroSafeCommand(mwfl::ControlId id) noexcept {
        return (id.value >= kUndo.value && id.value <= kLf.value) ||
               (id.value >= kToggleFold.value && id.value <= kZoomReset.value) ||
               (id.value >= kSelectNext.value && id.value <= kInsertSequence.value);
    }

    void RememberCommand(mwfl::ControlId id) {
        std::erase(recent_commands_, id);
        recent_commands_.insert(recent_commands_.begin(), id);
        if (recent_commands_.size() > 20) recent_commands_.resize(20);
    }

    void ShowCommandPalette() {
        mwfl::TextBox query; mwfl::ListBox list; mwfl::Label hint;
        mwfl::Button run, cancel;
        std::vector<mwfl::ControlId> visible;
        std::optional<mwfl::ControlId> chosen;
        mwfl::Dialog* pointer = nullptr;
        const auto refresh = [&] {
            list.ClearItems(); visible.clear();
            auto filter = query.GetText();
            std::ranges::transform(filter, filter.begin(),
                [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            const auto add = [&](mwfl::ControlId id) {
                const auto* command = commands_.Find(id);
                if (!command || !command->IsVisible() || !command->IsEnabled()) return;
                auto label = std::wstring(command->GetText()); std::erase(label, L'&');
                auto folded = label;
                std::ranges::transform(folded, folded.begin(),
                    [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
                if ((!filter.empty() && folded.find(filter) == std::wstring::npos) ||
                    std::ranges::find(visible, id) != visible.end()) return;
                visible.push_back(id); list.AddItem(label);
            };
            for (const auto id : recent_commands_) add(id);
            for (const auto& command : commands_.GetCommands()) add(command.GetId());
            if (!visible.empty()) list.SetSelection(0);
            hint.SetText(std::to_wstring(visible.size()) + L" command(s)  •  recent first");
        };
        mwfl::Dialog dialog({.owner = GetHwnd(), .title = L"Command Palette",
            .initial_client_size = {560.0_dip, 430.0_dip}, .resizable = true,
            .callbacks = {
                .initialize = [&](HWND window) {
                    mwfl::ControlHost ui{window}; ui.Add(query, {701}, L""); ui.Add(list, {702}, {});
                    ui.Add(hint, {703}, L""); ui.Add(run, {IDOK}, L"Run"); ui.Add(cancel, {IDCANCEL}, L"Cancel");
                    mwfl::SetAccessibleName(query.GetHwnd(), L"Filter commands");
                    mwfl::SetAccessibleName(list.GetHwnd(), L"Available commands");
                    const auto layout = pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(6.0_dip)
                        .Add(query, mwfl::Fixed(30.0_dip)).Add(list, mwfl::Stretch())
                        .Add(mwfl::Row().Gap(6.0_dip).Add(hint, mwfl::Stretch())
                            .Add(run, mwfl::Fixed(80.0_dip)).Add(cancel, mwfl::Fixed(80.0_dip)), mwfl::Fixed(30.0_dip)));
                    refresh(); query.Focus(); return layout;
                },
                .command = [&](HWND, WORD id, WORD notification) {
                    if (id == 701 && notification == EN_CHANGE) { refresh(); return true; }
                    if (id == IDOK || (id == 702 && notification == LBN_DBLCLK)) {
                        const auto selected = list.GetSelectedIndex();
                        if (selected && static_cast<std::size_t>(*selected) < visible.size()) {
                            chosen = visible[static_cast<std::size_t>(*selected)]; pointer->Accept(); return true;
                        }
                    }
                    return false;
                }}});
        pointer = &dialog; static_cast<void>(dialog.ShowModal());
        if (chosen)
            if (const auto* command = commands_.Find(*chosen); command && command->IsEnabled()) {
                RememberCommand(*chosen); command->Invoke();
            }
    }

    void StartMacroRecording() {
        macro_recorder_.Start();
        status_.SetText(L"Macro recording started — editor commands and typed text only");
    }

    void StopMacroRecording() {
        if (!macro_recorder_.IsRecording()) { status_.SetText(L"No macro recording is active"); return; }
        last_macro_ = macro_recorder_.Stop();
        status_.SetText(L"Macro stopped — " + std::to_wstring(last_macro_.size()) + L" action(s)");
    }

    void PlayMacro(const std::vector<notepad_colon::MacroAction>& actions, std::size_t repeats) {
        if (actions.empty() || playing_macro_) return;
        playing_macro_ = true;
        for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
            for (const auto& action : actions) {
                if (action.kind == notepad_colon::MacroActionKind::command) {
                    const mwfl::ControlId id{action.command_id};
                    if (IsMacroSafeCommand(id))
                        if (const auto* command = commands_.Find(id); command && command->IsEnabled()) command->Invoke();
                } else if (auto* editor = ActiveEditor()) {
                    if (action.kind == notepad_colon::MacroActionKind::insert_text)
                        static_cast<void>(notepad_colon::InsertText(*editor, action.text));
                    else {
                        const auto caret = editor->GetSelection().end;
                        const auto count = (std::min)(static_cast<mwfl::ScintillaPosition>(action.count), caret);
                        editor->Send(SCI_DELETERANGE, caret - count, count);
                    }
                }
            }
        }
        playing_macro_ = false;
        status_.SetText(L"Macro played " + std::to_wstring(repeats) + L" time(s)");
    }

    void PlayLastMacro(std::size_t repeats) { PlayMacro(last_macro_, repeats); }

    void SaveLastMacro() {
        if (last_macro_.empty()) { status_.SetText(L"Record a macro before saving it"); return; }
        mwfl::TextBox name; mwfl::Button save, cancel; std::wstring chosen_name;
        mwfl::Dialog* pointer = nullptr;
        mwfl::Dialog dialog({.owner = GetHwnd(), .title = L"Save Macro",
            .initial_client_size = {420.0_dip, 110.0_dip}, .resizable = false,
            .callbacks = {
                .initialize = [&](HWND window) {
                    mwfl::ControlHost ui{window}; ui.Add(name, {711}, L"");
                    ui.Add(save, {IDOK}, L"Save"); ui.Add(cancel, {IDCANCEL}, L"Cancel");
                    mwfl::SetAccessibleName(name.GetHwnd(), L"Macro name");
                    return pointer->SetLayout(mwfl::Column().Margin(10.0_dip).Gap(8.0_dip)
                        .Add(name, mwfl::Fixed(30.0_dip))
                        .Add(mwfl::Row().Gap(6.0_dip).Add(mwfl::Column(), mwfl::Stretch())
                            .Add(save, mwfl::Fixed(80.0_dip)).Add(cancel, mwfl::Fixed(80.0_dip)), mwfl::Fixed(30.0_dip)));
                },
                .command = [&](HWND, WORD id, WORD) {
                    if (id == IDOK && !name.GetText().empty()) {
                        chosen_name = name.GetText(); pointer->Accept(); return true;
                    }
                    return false;
                }}});
        pointer = &dialog; static_cast<void>(dialog.ShowModal());
        if (chosen_name.empty()) return;
        const auto found = std::ranges::find(saved_macros_, chosen_name, &notepad_colon::SavedMacro::name);
        if (found == saved_macros_.end()) saved_macros_.push_back({chosen_name, last_macro_});
        else found->actions = last_macro_;
        if (!macros_path_.empty()) static_cast<void>(notepad_colon::SaveMacrosAtomic(macros_path_, saved_macros_));
        status_.SetText(L"Macro saved: " + chosen_name);
    }

    void ShowMacroManager() {
        mwfl::ListBox list; mwfl::Button play, remove, close; mwfl::Dialog* pointer = nullptr;
        mwfl::Dialog dialog({.owner = GetHwnd(), .title = L"Macros",
            .initial_client_size = {520.0_dip, 330.0_dip}, .resizable = true,
            .callbacks = {
                .initialize = [&](HWND window) {
                    mwfl::ControlHost ui{window}; ui.Add(list, {721}, {});
                    ui.Add(play, {722}, L"Play"); ui.Add(remove, {723}, L"Delete"); ui.Add(close, {IDCANCEL}, L"Close");
                    for (const auto& macro : saved_macros_)
                        list.AddItem(macro.name + L"  (" + std::to_wstring(macro.actions.size()) + L" actions)");
                    if (!saved_macros_.empty()) list.SetSelection(0);
                    return pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(6.0_dip)
                        .Add(list, mwfl::Stretch())
                        .Add(mwfl::Row().Gap(6.0_dip).Add(play, mwfl::Fixed(80.0_dip))
                            .Add(remove, mwfl::Fixed(80.0_dip)).Add(mwfl::Column(), mwfl::Stretch())
                            .Add(close, mwfl::Fixed(80.0_dip)), mwfl::Fixed(30.0_dip)));
                },
                .command = [&](HWND, WORD id, WORD) {
                    const auto selected = list.GetSelectedIndex();
                    if (id == 722 && selected && static_cast<std::size_t>(*selected) < saved_macros_.size()) {
                        PlayMacro(saved_macros_[static_cast<std::size_t>(*selected)].actions, 1); return true;
                    }
                    if (id == 723 && selected && static_cast<std::size_t>(*selected) < saved_macros_.size()) {
                        saved_macros_.erase(saved_macros_.begin() + *selected); list.RemoveItem(*selected);
                        if (!macros_path_.empty()) static_cast<void>(notepad_colon::SaveMacrosAtomic(macros_path_, saved_macros_));
                        if (!saved_macros_.empty()) list.SetSelection((std::min)(*selected,
                            static_cast<int>(saved_macros_.size() - 1)));
                        return true;
                    }
                    return false;
                }}});
        pointer = &dialog; static_cast<void>(dialog.ShowModal());
    }

    std::vector<notepad_colon::ShortcutBinding> CaptureShortcuts() const {
        std::vector<notepad_colon::ShortcutBinding> result;
        for (const auto& command : commands_.GetCommands()) {
            if (!command.GetShortcut()) continue;
            result.push_back({static_cast<std::uint16_t>(command.GetId().value),
                command.GetShortcut()->modifiers, command.GetShortcut()->key});
        }
        return result;
    }

    bool ApplyShortcuts(const std::vector<notepad_colon::ShortcutBinding>& bindings,
                        bool persist) {
        if (!notepad_colon::FindShortcutConflicts(bindings).empty()) return false;
        for (const auto& command : commands_.GetCommands())
            if (auto* mutable_command = commands_.Find(command.GetId())) mutable_command->ClearShortcut();
        for (const auto& binding : bindings)
            if (auto* command = commands_.Find({binding.command_id}); binding.key && command)
                command->SetShortcut({binding.modifiers, binding.key});
        if (!accelerators_.Create(commands_)) return false;
        SetAccelerators(accelerators_.GetHandle()); BuildMenu();
        if (persist) static_cast<void>(SaveConfigurationFile(configuration_path_));
        return true;
    }

    bool SaveConfigurationFile(const std::filesystem::path& path) const {
        if (path.empty()) return false;
        std::error_code error; std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;
        const auto temporary = path.wstring() + L".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        const auto encoded = notepad_colon::SerializeConfiguration({preferences_, CaptureShortcuts()});
        output.write(encoded.data(), static_cast<std::streamsize>(encoded.size())); output.close();
        if (!output || !::MoveFileExW(temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporary, error); return false;
        }
        return true;
    }

    void ShowShortcutSettings() {
        mwfl::ListBox list; mwfl::TextBox shortcut; mwfl::Label diagnostic;
        mwfl::Button apply, defaults, close;
        std::vector<mwfl::ControlId> ids;
        for (const auto& command : commands_.GetCommands())
            if (command.IsVisible()) ids.push_back(command.GetId());
        auto bindings = CaptureShortcuts();
        mwfl::Dialog* pointer = nullptr;
        const auto refresh = [&] {
            list.ClearItems();
            for (const auto id : ids) {
                const auto* command = commands_.Find(id); if (!command) continue;
                auto label = std::wstring(command->GetText()); std::erase(label, L'&');
                const auto found = std::ranges::find(bindings, static_cast<std::uint16_t>(id.value),
                                                      &notepad_colon::ShortcutBinding::command_id);
                list.AddItem(label + L"    " + (found == bindings.end() ? L"" : notepad_colon::FormatShortcut(*found)));
            }
            if (!ids.empty() && list.GetSelection() == LB_ERR) list.SetSelection(0);
            diagnostic.SetText(notepad_colon::FindShortcutConflicts(bindings).empty()
                ? L"No shortcut conflicts" : L"Resolve duplicate shortcuts before applying");
        };
        const auto load_selected = [&] {
            const auto selected = list.GetSelectedIndex(); if (!selected) return;
            const auto id = ids[static_cast<std::size_t>(*selected)];
            const auto found = std::ranges::find(bindings, static_cast<std::uint16_t>(id.value),
                                                  &notepad_colon::ShortcutBinding::command_id);
            shortcut.SetText(found == bindings.end() ? L"" : notepad_colon::FormatShortcut(*found));
        };
        const auto store_selected = [&] {
            const auto selected = list.GetSelectedIndex(); if (!selected) return false;
            const auto id = ids[static_cast<std::size_t>(*selected)];
            const auto parsed = notepad_colon::ParseShortcut(static_cast<std::uint16_t>(id.value), shortcut.GetText());
            if (!parsed) return false;
            std::erase_if(bindings, [&](const auto& item) { return item.command_id == id.value; });
            if (parsed->key) bindings.push_back(*parsed);
            return true;
        };
        mwfl::Dialog dialog({.owner = GetHwnd(), .title = L"Keyboard Shortcuts",
            .initial_client_size = {650.0_dip, 460.0_dip}, .resizable = true,
            .callbacks = {
                .initialize = [&](HWND window) {
                    mwfl::ControlHost ui{window}; ui.Add(list, {731}, {}); ui.Add(shortcut, {732}, L"");
                    ui.Add(diagnostic, {733}, L""); ui.Add(apply, {IDOK}, L"Apply");
                    ui.Add(defaults, {734}, L"Restore defaults"); ui.Add(close, {IDCANCEL}, L"Close");
                    mwfl::SetAccessibleName(list.GetHwnd(), L"Commands and shortcuts");
                    mwfl::SetAccessibleName(shortcut.GetHwnd(), L"Shortcut such as Ctrl Shift K");
                    const auto layout = pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(6.0_dip)
                        .Add(list, mwfl::Stretch()).Add(shortcut, mwfl::Fixed(30.0_dip))
                        .Add(mwfl::Row().Gap(6.0_dip).Add(diagnostic, mwfl::Stretch())
                            .Add(defaults, mwfl::Auto()).Add(apply, mwfl::Fixed(80.0_dip))
                            .Add(close, mwfl::Fixed(80.0_dip)), mwfl::Fixed(30.0_dip)));
                    refresh(); load_selected(); return layout;
                },
                .command = [&](HWND, WORD id, WORD notification) {
                    if (id == 731 && notification == LBN_SELCHANGE) { load_selected(); return true; }
                    if (id == 734) { bindings = default_shortcuts_; refresh(); load_selected(); return true; }
                    if (id == IDOK) {
                        if (!store_selected() || !notepad_colon::FindShortcutConflicts(bindings).empty()) {
                            ::MessageBoxW(pointer->GetHwnd(), L"Enter a valid, unique shortcut (for example Ctrl+Shift+K), or leave it blank.",
                                          L"Keyboard Shortcuts", MB_OK | MB_ICONWARNING); return true;
                        }
                        if (ApplyShortcuts(bindings, true)) pointer->Accept(); return true;
                    }
                    return false;
                }}});
        pointer = &dialog; static_cast<void>(dialog.ShowModal());
    }

    void ExportConfiguration() {
        const auto selected = mwfl::ShowSaveFileDialog({.owner = GetHwnd(), .title = L"Export settings",
            .filters = {{L"Notepad Colon configuration", L"*.npcconfig"}},
            .default_extension = L"npcconfig", .path_must_exist = false});
        if (selected.accepted)
            status_.SetText(SaveConfigurationFile(selected.path) ? L"Settings exported" : L"Settings export failed");
    }

    void ImportConfiguration() {
        const auto selected = mwfl::ShowOpenFileDialog({.owner = GetHwnd(), .title = L"Import settings",
            .filters = {{L"Notepad Colon configuration", L"*.npcconfig"}, {L"All files", L"*.*"}}});
        if (!selected.accepted) return;
        std::ifstream input(selected.path, std::ios::binary); std::string encoded{
            std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        notepad_colon::Configuration configuration;
        if ((!input && encoded.empty()) || !notepad_colon::DeserializeConfiguration(encoded, configuration) ||
            !ApplyShortcuts(configuration.shortcuts, false)) {
            status_.SetText(L"Settings import rejected: invalid or conflicting configuration"); return;
        }
        preferences_ = configuration.preferences; ApplyAppearance();
        if (!IsTestMode()) static_cast<void>(SavePreferences());
        static_cast<void>(SaveConfigurationFile(configuration_path_));
        status_.SetText(L"Settings and shortcuts imported");
    }

    std::optional<std::wstring> ActiveSelectionText() const {
        const auto* document = const_cast<MainWindow*>(this)->ActiveDocument();
        if (!document) return std::nullopt;
        const auto selection = document->editor->GetSelection();
        if (selection.start == selection.end) return std::wstring{};
        std::string utf8(static_cast<std::size_t>(selection.end - selection.start) + 1, '\0');
        document->editor->Send(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(utf8.data()));
        utf8.resize(static_cast<std::size_t>(selection.end - selection.start));
        return mwfl::FromUtf8(utf8);
    }

    void ShowDocumentStatistics() {
        const auto* document = ActiveDocument(); if (!document) return;
        const auto text = document->editor->GetText(); if (!text) return;
        const auto stats = notepad_colon::CalculateStatistics(*text);
        const auto selection = ActiveSelectionText();
        const auto selection_stats = selection ? notepad_colon::CalculateStatistics(*selection)
                                               : notepad_colon::DocumentStatistics{};
        mwfl::Label values; mwfl::Button close; mwfl::Dialog* pointer = nullptr;
        const auto content = L"Characters                 " + std::to_wstring(stats.characters) +
            L"\nCharacters (no spaces)    " + std::to_wstring(stats.characters_without_whitespace) +
            L"\nWords                         " + std::to_wstring(stats.words) +
            L"\nLines                           " + std::to_wstring(stats.lines) +
            L"\nNon-blank lines             " + std::to_wstring(stats.non_blank_lines) +
            L"\nUTF-8 bytes                  " + std::to_wstring(stats.utf8_bytes) +
            L"\n\nSelected characters       " + std::to_wstring(selection_stats.characters) +
            L"\nSelected words               " + std::to_wstring(selection_stats.words);
        mwfl::Dialog dialog({.owner = GetHwnd(), .title = L"Document Statistics",
            .initial_client_size = {410.0_dip, 300.0_dip}, .resizable = false,
            .callbacks = {.initialize = [&](HWND window) {
                mwfl::ControlHost ui{window}; ui.Add(values, {741}, content); ui.Add(close, {IDOK}, L"Close");
                return pointer->SetLayout(mwfl::Column().Margin(14.0_dip).Gap(8.0_dip)
                    .Add(values, mwfl::Stretch())
                    .Add(mwfl::Row().Add(mwfl::Column(), mwfl::Stretch()).Add(close, mwfl::Fixed(86.0_dip)),
                         mwfl::Fixed(30.0_dip)));
            }}});
        pointer = &dialog; static_cast<void>(dialog.ShowModal());
    }

    void ExportDocument(bool html) {
        const auto* document = ActiveDocument();
        const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
        const auto text = document ? document->editor->GetText() : std::nullopt;
        if (!document || !metadata || !text) return;
        const auto selected = mwfl::ShowSaveFileDialog({.owner = GetHwnd(),
            .title = html ? L"Export HTML" : L"Export plain text",
            .filters = html ? std::vector<mwfl::FileDialogOptions::Filter>{{L"HTML document", L"*.html;*.htm"}}
                            : std::vector<mwfl::FileDialogOptions::Filter>{{L"Text file", L"*.txt"}},
            .default_extension = html ? L"html" : L"txt", .path_must_exist = false});
        if (!selected.accepted) return;
        const auto output = html ? notepad_colon::ExportHtmlDocument(metadata->title, *text, IsDark()) : *text;
        const auto written = mwfl::WriteTextFileAtomic(selected.path, output, mwfl::TextEncoding::utf8);
        status_.SetText(written.Succeeded() ? (html ? L"HTML exported" : L"Plain text exported")
                                           : L"Export failed");
    }

    void ShowPageSetup() {
        auto candidate = print_options_;
        mwfl::Label margin_label; mwfl::TextBox margin;
        mwfl::CheckBox header, footer, line_numbers, colours;
        mwfl::Button save, cancel; mwfl::Dialog* pointer = nullptr;
        mwfl::Dialog dialog({.owner = GetHwnd(), .title = L"Page Setup",
            .initial_client_size = {470.0_dip, 285.0_dip}, .resizable = false,
            .callbacks = {
                .initialize = [&](HWND window) {
                    mwfl::ControlHost ui{window}; ui.Add(margin_label, {761}, L"Margins (inches)");
                    ui.Add(margin, {762}, std::to_wstring(candidate.margin_inches));
                    ui.Add(header, {763}, L"Print document title in header");
                    ui.Add(footer, {764}, L"Print page number in footer");
                    ui.Add(line_numbers, {765}, L"Print line numbers");
                    ui.Add(colours, {766}, L"Print syntax colours on white paper");
                    ui.Add(save, {IDOK}, L"Save"); ui.Add(cancel, {IDCANCEL}, L"Cancel");
                    header.SetChecked(candidate.header); footer.SetChecked(candidate.footer);
                    line_numbers.SetChecked(candidate.line_numbers); colours.SetChecked(candidate.syntax_colours);
                    return pointer->SetLayout(mwfl::Column().Margin(12.0_dip).Gap(7.0_dip)
                        .Add(mwfl::Row().Gap(8.0_dip).Add(margin_label, mwfl::Fixed(150.0_dip))
                            .Add(margin, mwfl::Stretch()), mwfl::Fixed(30.0_dip))
                        .Add(header, mwfl::Fixed(28.0_dip)).Add(footer, mwfl::Fixed(28.0_dip))
                        .Add(line_numbers, mwfl::Fixed(28.0_dip)).Add(colours, mwfl::Fixed(28.0_dip))
                        .Add(mwfl::Row().Gap(6.0_dip).Add(mwfl::Column(), mwfl::Stretch())
                            .Add(save, mwfl::Fixed(82.0_dip)).Add(cancel, mwfl::Fixed(82.0_dip)), mwfl::Fixed(30.0_dip)));
                },
                .command = [&](HWND, WORD id, WORD) {
                    if (id == IDOK) {
                        wchar_t* end{}; const auto value = std::wcstod(margin.GetText().c_str(), &end);
                        if (!end || *end || value < 0.1 || value > 2.0) {
                            ::MessageBoxW(pointer->GetHwnd(), L"Margins must be between 0.1 and 2.0 inches.",
                                          L"Page Setup", MB_OK | MB_ICONWARNING); return true;
                        }
                        candidate = {value, header.IsChecked(), footer.IsChecked(),
                                     line_numbers.IsChecked(), colours.IsChecked()};
                    }
                    return false;
                }}});
        pointer = &dialog;
        if (dialog.ShowModal()) { print_options_ = candidate; status_.SetText(L"Page setup updated"); }
    }

    void ConfigurePrinter() {
        mwfl::PrinterSettings initial;
        if (printer_settings_.IsValid()) initial = printer_settings_.Clone();
        auto result = mwfl::ShowPrintDialog(GetHwnd(), std::move(initial), PD_NOSELECTION);
        if (result.action == mwfl::PrintDialogAction::accepted) {
            printer_settings_ = std::move(result.settings);
            status_.SetText(L"Printer settings updated: " + printer_settings_.PrinterName());
        } else if (result.action == mwfl::PrintDialogAction::cancelled)
            status_.SetText(L"Printer settings cancelled");
        else status_.SetText(L"No printer is available or the printer dialog failed");
    }

    std::vector<mwfl::PrintPage> PaginateForPrinter(mwfl::ScintillaEditor& editor, HDC dc,
                                                     mwfl::ScintillaPosition begin,
                                                     mwfl::ScintillaPosition end) {
        std::vector<mwfl::PrintPage> pages;
        const int dpi_x = ::GetDeviceCaps(dc, LOGPIXELSX), dpi_y = ::GetDeviceCaps(dc, LOGPIXELSY);
        const int width = ::GetDeviceCaps(dc, HORZRES), height = ::GetDeviceCaps(dc, VERTRES);
        const int margin_x = static_cast<int>(dpi_x * print_options_.margin_inches);
        const int margin_y = static_cast<int>(dpi_y * print_options_.margin_inches);
        Sci_RangeToFormatFull format{};
        format.hdc = reinterpret_cast<Sci_SurfaceID>(dc); format.hdcTarget = format.hdc;
        format.rcPage = {0, 0, width, height};
        format.rc = {margin_x, margin_y + (print_options_.header ? dpi_y / 3 : 0),
                     width - margin_x, height - margin_y - (print_options_.footer ? dpi_y / 3 : 0)};
        auto position = begin;
        while (position < end && pages.size() < 10000) {
            format.chrg = {position, end};
            const auto next = editor.Send(SCI_FORMATRANGEFULL, FALSE, reinterpret_cast<LPARAM>(&format));
            if (next <= position) break;
            pages.push_back({{pages.size() + 1}, pages.size(), position, next});
            position = next;
        }
        editor.Send(SCI_FORMATRANGEFULL, FALSE, 0);
        return pages;
    }

    void PrintActive() {
        auto* document = ActiveDocument(); const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
        if (!document || !metadata) return;
        mwfl::PrinterSettings initial;
        if (printer_settings_.IsValid()) initial = printer_settings_.Clone();
        const auto selection = document->editor->GetSelection();
        auto dialog = mwfl::ShowPrintDialog(GetHwnd(), std::move(initial),
            selection.start == selection.end ? PD_NOSELECTION : PD_SELECTION);
        if (dialog.action == mwfl::PrintDialogAction::cancelled) { status_.SetText(L"Printing cancelled"); return; }
        if (dialog.action != mwfl::PrintDialogAction::accepted) { status_.SetText(L"No printer is available"); return; }
        printer_settings_ = std::move(dialog.settings);
        auto backend = mwfl::CreatePrinterBackend(printer_settings_);
        if (!backend) { status_.SetText(L"Printer device could not be opened"); return; }
        mwfl::PrintJob job(std::move(backend.backend));
        const auto begin = (dialog.flags & PD_SELECTION) ? selection.start : 0;
        const auto end = (dialog.flags & PD_SELECTION) ? selection.end : document->editor->GetLength();
        document->editor->Send(SCI_SETPRINTCOLOURMODE,
            print_options_.syntax_colours ? SC_PRINT_COLOURONWHITEDEFAULTBG : SC_PRINT_BLACKONWHITE);
        document->editor->Send(SCI_SETPRINTWRAPMODE, SC_WRAP_WORD);
        const auto line_number_width = document->editor->Send(SCI_GETMARGINWIDTHN, 0);
        if (!print_options_.line_numbers) document->editor->Send(SCI_SETMARGINWIDTHN, 0, 0);
        const auto pages = PaginateForPrinter(*document->editor, job.DeviceContext(), begin, end);
        if (pages.empty()) {
            document->editor->Send(SCI_SETMARGINWIDTHN, 0, line_number_width);
            status_.SetText(L"Nothing to print"); return;
        }
        const auto result = mwfl::PrintPages(job, metadata->title, pages,
            [&](HDC dc, const mwfl::PrintPage& page) {
                const int dpi_x = ::GetDeviceCaps(dc, LOGPIXELSX), dpi_y = ::GetDeviceCaps(dc, LOGPIXELSY);
                const int width = ::GetDeviceCaps(dc, HORZRES), height = ::GetDeviceCaps(dc, VERTRES);
                ::SetBkMode(dc, TRANSPARENT); ::SetTextColor(dc, RGB(0, 0, 0));
                const int margin_x = static_cast<int>(dpi_x * print_options_.margin_inches);
                const int margin_y = static_cast<int>(dpi_y * print_options_.margin_inches);
                if (print_options_.header) {
                    RECT header{margin_x, margin_y, width - margin_x, margin_y + dpi_y / 4};
                    ::DrawTextW(dc, metadata->title.c_str(), -1, &header,
                                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
                if (print_options_.footer) {
                    const auto footer_text = L"Page " + std::to_wstring(page.index + 1) + L" of " + std::to_wstring(pages.size());
                    RECT footer{margin_x, height - margin_y - dpi_y / 4, width - margin_x, height - margin_y};
                    ::DrawTextW(dc, footer_text.c_str(), -1, &footer, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
                }
                Sci_RangeToFormatFull format{}; format.hdc = reinterpret_cast<Sci_SurfaceID>(dc); format.hdcTarget = format.hdc;
                format.rcPage = {0, 0, width, height};
                format.rc = {margin_x, margin_y + (print_options_.header ? dpi_y / 3 : 0),
                             width - margin_x, height - margin_y - (print_options_.footer ? dpi_y / 3 : 0)};
                format.chrg = {page.content_begin, page.content_end};
                return document->editor->Send(SCI_FORMATRANGEFULL, TRUE, reinterpret_cast<LPARAM>(&format)) > page.content_begin;
            }, [](const mwfl::PrintPage&) { return (::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0; });
        document->editor->Send(SCI_FORMATRANGEFULL, FALSE, 0);
        document->editor->Send(SCI_SETMARGINWIDTHN, 0, line_number_width);
        status_.SetText(result == mwfl::PrintOperationStatus::success ? L"Document printed" :
            result == mwfl::PrintOperationStatus::cancelled ? L"Printing cancelled safely" : L"Printing failed");
    }

    void ShowPrintPreview() {
        const auto* document = ActiveDocument(); const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
        const auto text = document ? document->editor->GetText() : std::nullopt;
        if (!document || !metadata || !text) return;
        std::vector<std::wstring> pages(1); std::size_t line = 0, start = 0;
        while (start < text->size()) {
            const auto end = text->find(L'\n', start);
            if (line && line % 55 == 0) pages.emplace_back();
            pages.back().append(text->substr(start, end == std::wstring::npos ? end : end - start));
            pages.back() += L'\n'; ++line;
            if (end == std::wstring::npos) break; start = end + 1;
        }
        mwfl::ScintillaEditor preview; mwfl::Label page_label;
        mwfl::Button previous, next, print, close; std::size_t current = 0; mwfl::Dialog* pointer = nullptr;
        const auto refresh = [&] { preview.SetReadOnly(false); preview.SetText(pages[current]); preview.SetReadOnly(true);
            page_label.SetText(metadata->title + L"  •  Page " + std::to_wstring(current + 1) + L" of " + std::to_wstring(pages.size())); };
        mwfl::Dialog dialog({.owner = GetHwnd(), .title = L"Print Preview",
            .initial_client_size = {760.0_dip, 700.0_dip}, .resizable = true,
            .callbacks = {
                .initialize = [&](HWND window) {
                    mwfl::ControlHost ui{window}; ui.Add(page_label, {751}, L"");
                    ui.Add(previous, {752}, L"Previous"); ui.Add(next, {753}, L"Next");
                    ui.Add(print, {754}, L"Print..."); ui.Add(close, {IDCANCEL}, L"Close");
                    if (!preview.Create(window, {755}, {}, runtime_)) return false;
                    preview.ConfigureCodeEditing(); notepad_colon::ApplyPreferences(preview, preferences_, false);
                    preview.SetReadOnly(true); refresh();
                    return pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(5.0_dip)
                        .Add(page_label, mwfl::Fixed(24.0_dip)).Add(preview, mwfl::Stretch())
                        .Add(mwfl::Row().Gap(6.0_dip).Add(previous, mwfl::Fixed(86.0_dip))
                            .Add(next, mwfl::Fixed(86.0_dip)).Add(mwfl::Column(), mwfl::Stretch())
                            .Add(print, mwfl::Fixed(86.0_dip)).Add(close, mwfl::Fixed(86.0_dip)), mwfl::Fixed(30.0_dip)));
                },
                .command = [&](HWND, WORD id, WORD) {
                    if (id == 752) { current = current ? current - 1 : pages.size() - 1; refresh(); return true; }
                    if (id == 753) { current = (current + 1) % pages.size(); refresh(); return true; }
                    if (id == 754) { pointer->Cancel(); PrintActive(); return true; }
                    return false;
                }}});
        pointer = &dialog; static_cast<void>(dialog.ShowModal());
    }

    void InsertDateTime() {
        SYSTEMTIME time{};
        ::GetLocalTime(&time);
        wchar_t value[64]{};
        swprintf_s(value, L"%04u-%02u-%02u %02u:%02u:%02u",
                   time.wYear, time.wMonth, time.wDay,
                   time.wHour, time.wMinute, time.wSecond);
        if (auto* editor = ActiveEditor()) static_cast<void>(notepad_colon::InsertText(*editor, value));
    }

    void AutoSaveIfDue() {
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
        if (saved) status_.SetText(L"Auto-saved " + std::to_wstring(saved) + L" document(s)");
    }

    bool CloseActive(bool discard = false) {
        const auto id = workspace_.GetActiveId();
        if (!id) return false;
        const auto* metadata = workspace_.Find(*id);
        if (metadata && metadata->dirty && !discard && !IsTestMode()) {
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
        if (!IsTestMode())
            static_cast<void>(mwfl::SaveRecentFilesToRegistry(HKEY_CURRENT_USER, kSettingsKey, recent_));
    }

    void OpenFolderInteractive() {
        const auto selected = mwfl::ShowFolderDialog({GetHwnd(), L"Choose a workspace folder"});
        if (selected.accepted) StartWorkspaceScan(selected.path);
    }

    void StartWorkspaceScan(std::filesystem::path root) {
        if (workspace_worker_.joinable()) {
            workspace_worker_.request_stop();
            workspace_worker_.join();
        }
        workspace_root_ = std::move(root);
        status_.SetText(L"Scanning workspace...");
        const HWND window = GetHwnd();
        const auto scan_root = workspace_root_;
        workspace_worker_ = std::jthread([this, window, scan_root](std::stop_token stop) {
            auto scan = notepad_colon::ScanWorkspace(scan_root, 20000, stop);
            {
                std::scoped_lock lock{worker_mutex_};
                pending_workspace_scan_ = std::move(scan);
            }
            ::PostMessageW(window, kWorkspaceCompleteMessage, 0, 0);
        });
    }

    void CompleteWorkspaceScan() {
        std::optional<notepad_colon::WorkspaceScan> scan;
        {
            std::scoped_lock lock{worker_mutex_};
            scan = std::move(pending_workspace_scan_);
            pending_workspace_scan_.reset();
        }
        if (!scan) return;
        TreeView_DeleteAllItems(tree_.GetHwnd());
        tree_paths_.clear();
        constexpr mwfl::TreeItemId root_id{1};
        tree_.AddItem(root_id, workspace_root_.filename().wstring());
        tree_paths_[root_id.value] = workspace_root_;
        std::unordered_map<std::wstring, mwfl::TreeItemId> directory_ids;
        directory_ids[L""] = root_id;
        std::uint64_t next = 2;
        for (const auto& entry : scan->entries) {
            const mwfl::TreeItemId id{next++};
            const auto parent_text = entry.relative_path.parent_path().wstring();
            const auto parent = directory_ids.contains(parent_text) ? directory_ids[parent_text] : root_id;
            tree_.AddChild(id, entry.relative_path.filename().wstring(), parent);
            tree_paths_[id.value] = workspace_root_ / entry.relative_path;
            if (entry.directory) directory_ids[entry.relative_path.wstring()] = id;
        }
        tree_.Expand(root_id);
        workspace_visible_ = true;
        ApplyCompactLayout();
        status_.SetText(L"Workspace: " + workspace_root_.wstring() + L" | " +
            std::to_wstring(scan->entries.size()) + L" entries" +
            (scan->truncated ? L" (truncated)" : L""));
    }

    void StartFolderSearch() {
        if (workspace_root_.empty()) {
            OpenFolderInteractive();
            return;
        }
        const auto query = search_.GetText();
        if (query.empty()) {
            status_.SetText(L"Enter search text before Find in Files");
            return;
        }
        CancelFolderSearch();
        if (search_worker_.joinable()) search_worker_.join();
        if (auto* command = commands_.Find(kCancelSearch)) command->SetEnabled(true);
        menu_.UpdateCommand(*commands_.Find(kCancelSearch));
        status_.SetText(L"Searching workspace...");
        const HWND window = GetHwnd();
        const auto root = workspace_root_;
        search_worker_ = std::jthread([this, window, root, query](std::stop_token stop) {
            auto result = notepad_colon::SearchWorkspace(root, query, {}, stop);
            {
                std::scoped_lock lock{worker_mutex_};
                pending_search_result_ = std::move(result);
            }
            ::PostMessageW(window, kSearchCompleteMessage, 0, 0);
        });
    }

    void CancelFolderSearch() {
        if (search_worker_.joinable()) search_worker_.request_stop();
    }

    void CompleteSearch() {
        std::optional<notepad_colon::SearchResult> result;
        {
            std::scoped_lock lock{worker_mutex_};
            result = std::move(pending_search_result_);
            pending_search_result_.reset();
        }
        if (!result) return;
        search_results_ = std::move(*result);
        ListView_DeleteAllItems(results_.GetHwnd());
        for (std::size_t index = 0; index < search_results_.matches.size(); ++index) {
            const auto& match = search_results_.matches[index];
            const mwfl::ListItemId id{index + 1};
            results_.AddItem(id, match.path.lexically_relative(workspace_root_).wstring());
            results_.SetItemText(id, 1, std::to_wstring(match.line));
            results_.SetItemText(id, 2, std::to_wstring(match.column));
            results_.SetItemText(id, 3, match.preview);
        }
        if (auto* command = commands_.Find(kCancelSearch)) command->SetEnabled(false);
        menu_.UpdateCommand(*commands_.Find(kCancelSearch));
        results_visible_ = true;
        ApplyCompactLayout();
        status_.SetText((search_results_.cancelled ? L"Search cancelled | " : L"Search complete | ") +
            std::to_wstring(search_results_.matches.size()) + L" matches in " +
            std::to_wstring(search_results_.files_searched) + L" files" +
            (search_results_.truncated ? L" (truncated)" : L""));
    }

    void CheckExternalChanges(bool force) {
        if (IsTestMode() && !force) return;
        for (auto& document : documents_) {
            const auto* metadata = workspace_.Find(document.id);
            if (!metadata || metadata->path.empty()) continue;
            const auto current = notepad_colon::CaptureFileState(metadata->path);
            if (current == document.file_state || document.external_changed) continue;
            if (!current.exists && !IsTestMode()) {
                const auto answer = ::MessageBoxW(GetHwnd(),
                    (metadata->title + L" was deleted outside Notepad Colon.\n\n"
                     L"Choose Yes to keep its contents as an unsaved document, or No to keep "
                     L"the original path blocked from overwrite.").c_str(),
                    L"File deleted on disk", MB_YESNO | MB_ICONWARNING);
                if (answer == IDYES) {
                    workspace_.Rename(document.id, metadata->title, {});
                    workspace_.SetDirty(document.id, true);
                    document.stamp.reset();
                    document.file_state = current;
                    document.external_changed = false;
                    SynchronizeTabs(false);
                    SyncPresentation(L"Kept deleted file as an unsaved document");
                } else {
                    document.external_changed = true;
                    status_.SetText(metadata->title + L" is missing on disk; use Save As to preserve it");
                }
                continue;
            }
            if (!metadata->dirty && current.exists) {
                const auto loaded = mwfl::ReadTextFile(metadata->path);
                if (loaded.Succeeded() && document.editor->SetText(loaded.value->text)) {
                    document.encoding = loaded.value->encoding;
                    document.line_ending = notepad_colon::DetectLineEnding(loaded.value->text);
                    document.stamp = loaded.value->stamp;
                    document.file_state = current;
                    document.editor->SetSavePoint();
                    workspace_.SetDirty(document.id, false);
                    SyncPresentation(L"Reloaded external change");
                }
            } else if (!IsTestMode() && current.exists) {
                const auto answer = ::MessageBoxW(GetHwnd(),
                    (metadata->title + L" changed on disk while it has unsaved edits.\n\n"
                     L"Yes: compare with disk\nNo: reload and discard local edits\nCancel: keep local edits").c_str(),
                    L"External edit conflict", MB_YESNOCANCEL | MB_ICONWARNING);
                if (answer == IDYES) {
                    const auto local = document.editor->GetText();
                    const auto disk = mwfl::ReadTextFile(metadata->path);
                    if (local && disk.Succeeded())
                        ShowComparison(L"Disk — " + metadata->title, *local, disk.value->text);
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
                        SyncPresentation(L"Reloaded disk version");
                    }
                } else {
                    document.external_changed = true;
                    status_.SetText(metadata->title + L" conflict retained; use Compare or Save As");
                }
            } else {
                document.external_changed = true;
                status_.SetText(metadata->title + L" changed on disk; Save As or reload before overwriting");
            }
        }
    }

    void StopWorkers() {
        for (auto* worker : {&search_worker_, &workspace_worker_}) {
            if (!worker->joinable()) continue;
            worker->request_stop();
            worker->join();
        }
    }

    void ResolveSessionPath() {
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
            macros_path_ = session_path_.parent_path() / L"macros.state";
            static_cast<void>(notepad_colon::LoadMacros(macros_path_, saved_macros_));
            configuration_path_ = session_path_.parent_path() / L"settings.npcconfig";
            std::ifstream input(configuration_path_, std::ios::binary);
            const std::string encoded{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
            notepad_colon::Configuration configuration;
            if (!encoded.empty() && notepad_colon::DeserializeConfiguration(encoded, configuration)) {
                preferences_ = configuration.preferences;
                ApplyAppearance();
                static_cast<void>(ApplyShortcuts(configuration.shortcuts, false));
            }
        }
    }

    bool SaveSessionSnapshot() {
        if (session_path_.empty() || restoring_session_) return true;
        notepad_colon::Session session;
        session.workspace_path = workspace_root_;
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
        if (!session.workspace_path.empty() && std::filesystem::is_directory(session.workspace_path))
            StartWorkspaceScan(session.workspace_path);
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
        if (auto* command = commands_.Find(kSave))
            command->SetEnabled(has_editor && !document->read_only);
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
            const notepad_colon::Preferences test_preferences{
                L"Consolas", 13, 3, notepad_colon::ThemePreference::dark};
            if (auto* editor = ActiveEditor()) {
                notepad_colon::ApplyPreferences(*editor, test_preferences, true);
                if (!notepad_colon::PreferencesApplied(*editor, test_preferences)) result = 20;
                notepad_colon::ApplyPreferences(*editor, preferences_, IsDark());
            } else result = 20;
            const std::wstring preference_key = L"Software\\mwfl\\Tests\\NotepadColonPrefs-" +
                                                std::to_wstring(::GetCurrentProcessId());
            mwfl::VersionedSettingsStore preference_store{HKEY_CURRENT_USER, preference_key, 1};
            const std::array preference_values{
                mwfl::SettingValue{L"FontName", std::wstring{L"Cascadia Mono"}},
                mwfl::SettingValue{L"FontSize", std::uint32_t{12}},
                mwfl::SettingValue{L"TabWidth", std::uint32_t{2}},
                mwfl::SettingValue{L"Theme", std::uint32_t{2}}};
            const std::array preference_schema{
                mwfl::SettingDefinition{L"FontName", mwfl::SettingType::string, 256, true},
                mwfl::SettingDefinition{L"FontSize", mwfl::SettingType::dword, 4, true},
                mwfl::SettingDefinition{L"TabWidth", mwfl::SettingType::dword, 4, true},
                mwfl::SettingDefinition{L"Theme", mwfl::SettingType::dword, 4, true}};
            if (!preference_store.Save(preference_values) ||
                !preference_store.Load(preference_schema)) result = 21;
            const std::array<std::wstring_view, 4> preference_names{
                L"FontName", L"FontSize", L"TabWidth", L"Theme"};
            static_cast<void>(preference_store.RemoveOwned(preference_names));
            ::RegDeleteTreeW(HKEY_CURRENT_USER, preference_key.c_str());
            auto association = TextAssociation();
            association.extension = L".npc-self-test";
            association.prog_id = L"mwfl.notepad-colon.self-test";
            const std::wstring isolated = L"Software\\mwfl\\Tests\\NotepadColon-" +
                                          std::to_wstring(::GetCurrentProcessId());
            const auto registered = mwfl::RegisterFileAssociation(
                HKEY_CURRENT_USER, isolated, association, false);
            const auto removed = mwfl::RemoveFileAssociation(
                HKEY_CURRENT_USER, isolated, association, false);
            ::RegDeleteTreeW(HKEY_CURRENT_USER, isolated.c_str());
            if (!registered || !removed) result = 19;
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
                if (metadata) {
                    cleanup.push_back(metadata->path);
                    auto backup = metadata->path; backup += L".bak"; cleanup.push_back(backup);
                    const auto saved_preferences = preferences_;
                    preferences_.trim_trailing_whitespace_on_save = true;
                    preferences_.ensure_final_newline = true;
                    preferences_.create_backup_before_save = true;
                    second->editor->SetText(L"clean  \t");
                    if (!SaveDocument(*second, false)) result = 22;
                    const auto cleaned = mwfl::ReadTextFile(metadata->path);
                    if (result == 0 && (!cleaned.Succeeded() || cleaned.value->text != L"clean\r\n" ||
                        !std::filesystem::is_regular_file(backup))) result = 23;
                    preferences_ = saved_preferences;
                }
            }
            if (result == 0 && second) {
                const auto* metadata = workspace_.Find(second->id);
                if (!metadata || !mwfl::WriteTextFileAtomic(
                        metadata->path, L"external replacement is longer\n",
                        mwfl::TextEncoding::utf8).Succeeded()) result = 14;
                CheckExternalChanges(true);
                const auto reloaded = second->editor->GetText();
                if (result == 0 && (!reloaded || *reloaded != L"external replacement is longer\n")) result = 15;
            }
            const auto test_workspace = std::filesystem::temp_directory_path() /
                (L"notepad-colon-gui-workspace-" + std::to_wstring(::GetCurrentProcessId()));
            std::filesystem::create_directories(test_workspace / L"src");
            const auto workspace_file = test_workspace / L"src" / L"match.txt";
            if (!mwfl::WriteTextFileAtomic(workspace_file, L"folder needle result\n",
                                           mwfl::TextEncoding::utf8).Succeeded()) result = 16;
            workspace_root_ = test_workspace;
            {
                std::scoped_lock lock{worker_mutex_};
                pending_workspace_scan_ = notepad_colon::ScanWorkspace(test_workspace);
            }
            CompleteWorkspaceScan();
            if (result == 0 && TreeView_GetCount(tree_.GetHwnd()) < 3) result = 17;
            {
                std::scoped_lock lock{worker_mutex_};
                pending_search_result_ = notepad_colon::SearchWorkspace(test_workspace, L"needle");
            }
            CompleteSearch();
            if (result == 0 && ListView_GetItemCount(results_.GetHwnd()) != 1) result = 18;
            std::error_code workspace_ignored;
            std::filesystem::remove_all(test_workspace, workspace_ignored);
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
    mwfl::TreeView tree_;
    mwfl::TabControl tabs_;
    mwfl::ListView results_;
    mwfl::StatusBar status_;
    std::filesystem::path session_path_;
    bool restoring_session_ = false;
    bool find_bar_visible_ = false;
    bool workspace_visible_ = false;
    bool results_visible_ = false;
    mwfl::RecentFileList recent_{10};
    std::filesystem::path workspace_root_;
    std::unordered_map<std::uint64_t, std::filesystem::path> tree_paths_;
    notepad_colon::SearchResult search_results_;
    std::jthread search_worker_;
    std::jthread workspace_worker_;
    std::mutex worker_mutex_;
    std::optional<notepad_colon::SearchResult> pending_search_result_;
    std::optional<notepad_colon::WorkspaceScan> pending_workspace_scan_;
    mwfl::UiTimer monitor_timer_;
    std::chrono::steady_clock::time_point last_auto_save_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_recovery_snapshot_ =
        std::chrono::steady_clock::now() - std::chrono::seconds{30};
    std::unique_ptr<notepad_colon::RecoveryStore> recovery_store_;
    notepad_colon::MacroRecorder macro_recorder_;
    std::vector<notepad_colon::MacroAction> last_macro_;
    std::vector<notepad_colon::SavedMacro> saved_macros_;
    std::filesystem::path macros_path_;
    std::filesystem::path configuration_path_;
    std::vector<notepad_colon::ShortcutBinding> default_shortcuts_;
    std::vector<mwfl::ControlId> recent_commands_;
    bool playing_macro_ = false;
    mwfl::PrinterSettings printer_settings_;
    PrintOptions print_options_;
    mwfl::SingleInstance& instance_;
    std::vector<std::filesystem::path> startup_paths_;
    bool self_test_ = false;
    bool activation_test_server_ = false;
    int activation_test_result_ = 4;
    notepad_colon::Preferences preferences_;
    mwfl::VersionedSettingsStore settings_{HKEY_CURRENT_USER,
                                            L"Software\\mwfl\\Notepad Colon\\Preferences", 1};
};
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    int count{};
    wchar_t** arguments = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    bool self_test = false;
    bool activation_test_server = false;
    bool activation_test_client = false;
    std::vector<std::filesystem::path> paths;
    for (int index = 1; arguments && index < count; ++index) {
        const std::wstring_view argument{arguments[index]};
        if (argument == L"--self-test") self_test = true;
        else if (argument == L"--activation-test-server") activation_test_server = true;
        else if (argument == L"--activation-test-client") activation_test_client = true;
        else if (!argument.starts_with(L"--")) {
            std::error_code error;
            auto absolute = std::filesystem::absolute(argument, error);
            paths.push_back(error ? std::filesystem::path{argument} : std::move(absolute));
        }
    }
    if (arguments) ::LocalFree(arguments);
    const std::wstring instance_id = self_test
        ? L"mwfl.notepad-colon.self-test." + std::to_wstring(::GetCurrentProcessId())
        : (activation_test_server || activation_test_client)
            ? L"mwfl.notepad-colon.activation-test" : L"mwfl.notepad-colon.v1";
    mwfl::SingleInstance single_instance{instance_id};
    if (!single_instance.IsPrimary()) {
        std::wstring payload;
        for (const auto& path : paths) {
            if (!payload.empty()) payload.push_back(L'\n');
            payload.append(path.wstring());
        }
        const auto result = single_instance.ForwardActivation(payload);
        if (!result.Delivered()) {
            if (activation_test_client) return 2;
            ::MessageBoxW(nullptr, L"The existing Notepad Colon instance did not respond.",
                          L"Notepad Colon", MB_OK | MB_ICONERROR);
            return 2;
        }
        return 0;
    }
    if (activation_test_client) return 3;
    return mwfl::RunApplication<MainWindow>(instance, self_test ? SW_HIDE : show_command,
        {.title = L"Notepad Colon", .initial_bounds = {{}, {900.0_dip, 650.0_dip}},
         .use_default_bounds = false}, {.com_apartment = mwfl::ComApartment::sta},
         single_instance, std::move(paths), self_test, activation_test_server);
}
