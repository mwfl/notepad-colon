#include <mwfl/mwfl.h>
#include <mwfl/scintilla.h>
#include <mwfl/file_association.h>
#include <mwfl/shell_integration.h>
#include <mwfl/settings_store.h>
#include <mwfl/printing_settings.h>
#include <mwfl/single_instance.h>
#include <mwfl/dialog.h>
#include <notepad_colon/large_file.h>
#include <notepad_colon/large_file_buffer.h>
#include <notepad_colon/comparison.h>
#include <notepad_colon/configuration.h>
#include <notepad_colon/macro.h>
#include <notepad_colon/mapped_file.h>
#include <notepad_colon/output.h>
#include <notepad_colon/editing.h>
#include <notepad_colon/editor_config.h>
#include <notepad_colon/git_status.h>
#include <notepad_colon/encoding_analysis.h>
#include <notepad_colon/preferences.h>
#include <notepad_colon/recovery.h>
#include <notepad_colon/text.h>
#include <notepad_colon/session.h>
#include <notepad_colon/session_writer.h>
#include <notepad_colon/language.h>
#include <notepad_colon/language_registry.h>
#include <notepad_colon/lightweight_completion.h>
#include <notepad_colon/workspace.h>
#include <notepad_colon/workspace_state.h>
#include "scintilla_support.h"
#include "wasm_syntax_client.h"
#include "../resource.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
constexpr mwfl::ControlId kSearchMatchCase{444};
constexpr mwfl::ControlId kSearchWholeWord{445};
constexpr mwfl::ControlId kSearchRegex{446};
constexpr mwfl::ControlId kSearchSelection{447};
constexpr mwfl::ControlId kGoToLineColumn{448};
constexpr mwfl::ControlId kDocumentSymbols{449};
constexpr mwfl::ControlId kQuickOpen{450};
constexpr mwfl::ControlId kPreviousSearch{451};
constexpr mwfl::ControlId kFollowTail{452};
constexpr mwfl::ControlId kRefreshGitChanges{453};
constexpr mwfl::ControlId kOpenTerminal{454};
constexpr mwfl::ControlId kCompleteWord{455};
constexpr mwfl::ControlId kMarkAll{456};
constexpr mwfl::ControlId kClearSearchMarks{457};
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
constexpr mwfl::ControlId kWorkspaceRefresh{414};
constexpr mwfl::ControlId kWorkspaceNewFile{415};
constexpr mwfl::ControlId kWorkspaceNewFolder{416};
constexpr mwfl::ControlId kWorkspaceRename{417};
constexpr mwfl::ControlId kWorkspaceRecycle{418};
constexpr mwfl::ControlId kWorkspaceReveal{419};
constexpr mwfl::ControlId kWorkspaceCopyPath{420};
constexpr mwfl::ControlId kWorkspaceCopyRelativePath{421};
constexpr mwfl::ControlId kWorkspaceRemoveRoot{422};
constexpr mwfl::ControlId kSaveNamedSession{423};
constexpr mwfl::ControlId kOpenNamedSession{424};
constexpr mwfl::ControlId kPinTab{425};
constexpr mwfl::ControlId kSortTabs{426};
constexpr mwfl::ControlId kCloseOtherTabs{427};
constexpr mwfl::ControlId kCloseLeftTabs{428};
constexpr mwfl::ControlId kCloseRightTabs{429};
constexpr mwfl::ControlId kOpenNewWindow{430};
constexpr mwfl::ControlId kEncodingInfo{431};
constexpr mwfl::ControlId kReopenSystemAnsi{432};
constexpr mwfl::ControlId kReopenWindows1252{433};
constexpr mwfl::ControlId kReopenGb18030{434};
constexpr mwfl::ControlId kSaveSystemAnsi{435};
constexpr mwfl::ControlId kPreviousLargeWindow{436};
constexpr mwfl::ControlId kNextLargeWindow{437};
constexpr mwfl::ControlId kEnglishUi{438};
constexpr mwfl::ControlId kChineseUi{439};
constexpr mwfl::ControlId kWorkspaceFilter{440};
constexpr mwfl::ControlId kWorkspaceManager{441};
constexpr mwfl::ControlId kFavoriteWorkspace{442};
constexpr mwfl::ControlId kReloadLanguages{443};
constexpr mwfl::ControlId kLanguageBase{500};
constexpr mwfl::ControlId kCustomLanguageBase{600};

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
constexpr mwfl::TimerId kWorkspaceFilterTimer{3};
constexpr mwfl::TimerId kIncrementalSearchTimer{4};
constexpr auto kSessionDebounce = std::chrono::milliseconds{1500};
constexpr std::wstring_view kSettingsKey = L"Software\\mwfl\\Notepad Colon";
constexpr mwfl::ControlId kSearch{130};
constexpr mwfl::ControlId kReplacement{131};

std::vector<std::wstring> SplitGlobs(std::wstring_view value) {
    std::vector<std::wstring> patterns;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(L';', start);
        auto pattern = std::wstring{value.substr(start,
            (end == std::wstring_view::npos ? value.size() : end) - start)};
        while (!pattern.empty() && std::iswspace(pattern.front())) pattern.erase(pattern.begin());
        while (!pattern.empty() && std::iswspace(pattern.back())) pattern.pop_back();
        if (!pattern.empty()) patterns.push_back(std::move(pattern));
        if (end == std::wstring_view::npos) break;
        start = end + 1;
    }
    return patterns;
}
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
    notepad_colon::EncodingKind detected_encoding = notepad_colon::EncodingKind::utf8;
    unsigned int ansi_code_page = 65001;
    notepad_colon::EncodingAnalysis encoding_analysis;
    std::unique_ptr<notepad_colon::MappedFile> mapped_file;
    std::unique_ptr<notepad_colon::LargeFileBuffer> large_buffer;
    std::uint64_t mapped_offset = 0;
    std::uint64_t mapped_decoded_offset = 0;
    std::size_t mapped_window_size = 8u * 1024 * 1024;
    bool loading_large_window = false;
    std::unique_ptr<notepad_colon::TreeSitterDocument> syntax_tree;
    std::unique_ptr<notepad_colon::WasmSyntaxClient> wasm_syntax;
    std::string language_id = "plain-text";
    notepad_colon::EditorConfigSettings editor_config;
    bool follow_tail = false;
};

struct BackgroundTask {
    std::shared_ptr<std::atomic_bool> done;
    std::jthread thread;
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

std::optional<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return std::nullopt;
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uintmax_t>(end) >
        static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)())) return std::nullopt;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), end)) return std::nullopt;
    return bytes;
}

std::optional<mwfl::FileStamp> QueryFileStamp(const std::filesystem::path& path) noexcept {
    const HANDLE file = ::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION information{};
    const bool succeeded = ::GetFileInformationByHandle(file, &information) != FALSE;
    ::CloseHandle(file);
    if (!succeeded) return std::nullopt;
    ULARGE_INTEGER size{}, write{}, identity{};
    size.LowPart = information.nFileSizeLow; size.HighPart = information.nFileSizeHigh;
    write.LowPart = information.ftLastWriteTime.dwLowDateTime;
    write.HighPart = information.ftLastWriteTime.dwHighDateTime;
    identity.LowPart = information.nFileIndexLow; identity.HighPart = information.nFileIndexHigh;
    return mwfl::FileStamp{size.QuadPart, write.QuadPart, identity.QuadPart,
                           information.dwVolumeSerialNumber};
}

mwfl::TextEncoding ToMwflEncoding(notepad_colon::EncodingKind encoding) noexcept {
    switch (encoding) {
    case notepad_colon::EncodingKind::utf8_bom: return mwfl::TextEncoding::utf8_bom;
    case notepad_colon::EncodingKind::utf16_le: return mwfl::TextEncoding::utf16_le;
    case notepad_colon::EncodingKind::utf16_be: return mwfl::TextEncoding::utf16_be;
    default: return mwfl::TextEncoding::utf8;
    }
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
            .verbs = {{L"open", L"Open with Notepad::", {}}}};
}

class MainWindow final : public mwfl::WindowBase {
public:
    MainWindow(mwfl::SingleInstance& instance,
               std::vector<std::filesystem::path> startup_paths,
               bool self_test, bool activation_test_server, bool large_file_self_test)
        : workspace_({1}, 12), instance_(instance),
          startup_paths_(std::move(startup_paths)), self_test_(self_test),
          activation_test_server_(activation_test_server),
          large_file_self_test_(large_file_self_test) {}

    void BuildUI() override {
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
        mwfl::Must(toolbar_images_.Create(20, 20, ILC_COLOR24 | ILC_MASK, 7, 1),
                   "create toolbar image list");
        const auto strip = reinterpret_cast<HBITMAP>(::LoadImageW(
            ::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDB_TOOLBAR), IMAGE_BITMAP,
            0, 0, LR_CREATEDIBSECTION));
        mwfl::Must(strip != nullptr &&
                       ImageList_AddMasked(toolbar_images_.GetHandle(), strip,
                                           RGB(255, 0, 255)) == 0,
                   "load toolbar images");
        ::DeleteObject(strip);
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
        ::SendMessageW(toolbar_.GetHwnd(), TB_SETBUTTONSIZE, 0, MAKELPARAM(32, 30));
        ::SendMessageW(toolbar_.GetHwnd(), TB_SETPADDING, 0, MAKELPARAM(6, 5));
        toolbar_.AutoSize();
        mwfl::Must(mwfl::SetAccessibleName(toolbar_.GetHwnd(), L"Primary commands"),
                   "name toolbar");
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
        mwfl::Must(mwfl::SetAccessibleName(workspace_filter_.GetHwnd(), L"Filter workspace files"),
                   "name workspace filter");
        mwfl::Must(mwfl::SetAccessibleName(results_.GetHwnd(), L"Folder search results"), "name search results");

        ApplyCompactLayout();

        ResolveSessionPath();
        if (!RestoreSession()) {
            NewDocument();
            if (!workspace_catalog_.Roots().empty()) StartWorkspaceScan(workspace_catalog_.Roots().back());
        }
        mwfl::Must(instance_.RegisterWindow(GetHwnd()), "register single-instance window");
        for (const auto& path : startup_paths_) static_cast<void>(OpenPath(path));
        mwfl::Must(monitor_timer_.Start(*this, kMonitorTimer, std::chrono::milliseconds{2000}),
                   "start external-file monitor");
        if ((IsSelfTest() || large_file_self_test_) &&
            !::PostMessageW(GetHwnd(), kSelfTestMessage, 0, 0))
            throw std::runtime_error("could not schedule GUI self-test");
    }

    mwfl::EventResult OnCommand(const mwfl::CommandEvent& event) override {
        if (event.id == kSearch && event.notification == EN_CHANGE) {
            ::SetTimer(GetHwnd(), kIncrementalSearchTimer.value, 120, nullptr);
            return mwfl::EventResult::Handled();
        }
        if (event.id == kWorkspaceFilter) {
            ::SetTimer(GetHwnd(), kWorkspaceFilterTimer.value, 150, nullptr);
            return mwfl::EventResult::Handled();
        }
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
            StyleVisibleSyntax(*document);
        }
        if (notification->kind == mwfl::ScintillaNotificationKind::modified)
            UpdateLargeBuffer(*document, *reinterpret_cast<const SCNotification*>(&event.header));
        if (notification->kind == mwfl::ScintillaNotificationKind::modified)
            UpdateSyntaxTree(*document, *reinterpret_cast<const SCNotification*>(&event.header));
        workspace_.SetUndoState(document->id, document->editor->CanUndo(), document->editor->CanRedo());
        SyncPresentation(L"Editing");
        if (notification->kind == mwfl::ScintillaNotificationKind::modified) {
            session_snapshot_due_ = std::chrono::steady_clock::now() + kSessionDebounce;
            session_snapshot_pending_ = true;
        }
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

    mwfl::EventResult OnClose() override {
        monitor_timer_.Stop();
        if (!IsTestMode()) {
            for (const auto& document : workspace_.GetDocuments()) {
                if (!document.dirty) continue;
                if (::MessageBoxW(GetHwnd(),
                                  L"Close Notepad::? Unsaved documents will be restored next time.",
                                  L"Notepad::", MB_ICONINFORMATION | MB_OKCANCEL) != IDOK)
                    return mwfl::EventResult::Handled();
                break;
            }
            if (!SaveSessionSnapshot() || !session_writer_.Flush())
                return mwfl::EventResult::Handled();
            static_cast<void>(mwfl::SaveRecentFilesToRegistry(
                HKEY_CURRENT_USER, kSettingsKey, recent_));
            SaveWorkspaceCatalog();
            static_cast<void>(SaveConfigurationFile(configuration_path_));
        }
        StopWorkers();
        adapter_.Detach();
        instance_.UnregisterWindow();
        return mwfl::EventResult::Propagate();
    }

private:
    bool IsSelfTest() const noexcept {
        return self_test_;
    }
    bool IsTestMode() const noexcept {
        return self_test_ || activation_test_server_ || large_file_self_test_;
    }

    void BuildCommands() {
        LoadLanguageDefinitions();
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
            .Add(mwfl::Command(kCompleteWord, L"Complete Word", [this] {
                ShowLocalCompletion();
            }).SetShortcut({FVIRTKEY | FCONTROL, VK_SPACE}))
            .Add(mwfl::Command(kFindNext, L"&Find Next", [this] { static_cast<void>(FindNext()); })
                     .SetShortcut({FVIRTKEY, VK_F3}))
            .Add(mwfl::Command(kReplaceNext, L"&Replace Next", [this] { ReplaceNext(); })
                     .SetShortcut({FVIRTKEY | FCONTROL, 'H'}))
            .Add(mwfl::Command(kReplaceAll, L"Replace &All", [this] { ReplaceAll(); }))
            .Add(mwfl::Command(kSearchMatchCase, L"Match Case", [this] {
                ToggleSearchOption(kSearchMatchCase, search_match_case_); }))
            .Add(mwfl::Command(kSearchWholeWord, L"Whole Word", [this] {
                ToggleSearchOption(kSearchWholeWord, search_whole_word_); }))
            .Add(mwfl::Command(kSearchRegex, L"Regular Expression", [this] {
                ToggleSearchOption(kSearchRegex, search_regex_); }))
            .Add(mwfl::Command(kSearchSelection, L"In Selection", [this] {
                ToggleSearchSelection(); }))
            .Add(mwfl::Command(kGoToLineColumn, L"Go to Line / Column...", [this] {
                ShowGoToLineColumn(); }).SetShortcut({FVIRTKEY | FCONTROL, 'G'}))
            .Add(mwfl::Command(kDocumentSymbols, L"Document Symbols...", [this] {
                ShowDocumentSymbols(); }).SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, 'O'}))
            .Add(mwfl::Command(kQuickOpen, L"Quick Open File...", [this] {
                ShowQuickOpen(); }).SetShortcut({FVIRTKEY | FCONTROL, 'P'}))
            .Add(mwfl::Command(kPreviousSearch, L"Use Previous Search", [this] {
                if (search_history_.empty()) status_.SetText(L"Search history is empty");
                else { search_.SetText(search_history_.front()); search_.Focus(); }
            }))
            .Add(mwfl::Command(kMarkAll, L"Mark All Matches", [this] { MarkAllSearchMatches(); }))
            .Add(mwfl::Command(kClearSearchMarks, L"Clear Search Marks", [this] {
                if (auto* editor = ActiveEditor()) notepad_colon::ClearSearchMarks(*editor);
            }))
            .Add(mwfl::Command(kFollowTail, L"Follow Growing File", [this] {
                auto* document = ActiveDocument();
                if (!document || !document->large_buffer) {
                    status_.SetText(L"Follow mode is available for large files"); return;
                }
                document->follow_tail = !document->follow_tail;
                status_.SetText(document->follow_tail ? L"Following appended data" : L"Follow mode stopped");
            }))
            .Add(mwfl::Command(kRefreshGitChanges, L"Refresh Git Change Markers", [this] {
                RefreshGitChangeMarkers(); }))
            .Add(mwfl::Command(kOpenTerminal, L"Open Terminal Here", [this] {
                OpenWorkspaceTerminal(); }))
            .Add(mwfl::Command(kUtf8, L"UTF-&8", [this] { SetEncoding(mwfl::TextEncoding::utf8); }))
            .Add(mwfl::Command(kUtf8Bom, L"UTF-8 &BOM", [this] { SetEncoding(mwfl::TextEncoding::utf8_bom); }))
            .Add(mwfl::Command(kUtf16Le, L"UTF-16 &LE", [this] { SetEncoding(mwfl::TextEncoding::utf16_le); }))
            .Add(mwfl::Command(kUtf16Be, L"UTF-16 B&E", [this] { SetEncoding(mwfl::TextEncoding::utf16_be); }))
            .Add(mwfl::Command(kCrlf, L"Windows (&CRLF)", [this] { SetLineEnding(notepad_colon::LineEnding::crlf); }))
            .Add(mwfl::Command(kLf, L"Unix (&LF)", [this] { SetLineEnding(notepad_colon::LineEnding::lf); }));
        commands_
            .Add(mwfl::Command(kEncodingInfo, L"Document Encoding Information...", [this] { ShowEncodingInformation(); }))
            .Add(mwfl::Command(kReopenSystemAnsi, L"Reopen with System ANSI", [this] { ReopenActiveWithCodePage(::GetACP()); }))
            .Add(mwfl::Command(kReopenWindows1252, L"Reopen with Windows-1252", [this] { ReopenActiveWithCodePage(1252); }))
            .Add(mwfl::Command(kReopenGb18030, L"Reopen with GB18030", [this] { ReopenActiveWithCodePage(54936); }))
            .Add(mwfl::Command(kSaveSystemAnsi, L"Convert to System ANSI", [this] { SetAnsiEncoding(::GetACP()); }));
        commands_
            .Add(mwfl::Command(kEnglishUi, L"English UI", [this] { SetUiLanguage(false); }))
            .Add(mwfl::Command(kChineseUi, L"简体中文界面", [this] { SetUiLanguage(true); }));
        commands_.Add(mwfl::Command(kReloadLanguages, L"Reload Language Definitions",
            [this] { ReloadLanguageDefinitions(); }));
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
            .Add(mwfl::Command(kAbout, L"&About Notepad::",
                [this] { ::MessageBoxW(GetHwnd(),
                    L"Notepad:: 0.1.0-beta.1\nNative everyday code editing with MWFL and Scintilla.",
                    L"About Notepad::", MB_OK | MB_ICONINFORMATION); }));
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
            .Add(mwfl::Command(kToggleComment, L"Toggle Line Comment", [this] {
                ToggleActiveLineComment();
            })
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
            .Add(mwfl::Command(kWorkspaceRefresh, L"Refresh Workspace", [this] { RefreshWorkspace(); }))
            .Add(mwfl::Command(kWorkspaceNewFile, L"New File in Selected Folder...", [this] { CreateWorkspaceSelection(false); }))
            .Add(mwfl::Command(kWorkspaceNewFolder, L"New Folder in Selected Folder...", [this] { CreateWorkspaceSelection(true); }))
            .Add(mwfl::Command(kWorkspaceRename, L"Rename Selected Item...", [this] { RenameWorkspaceSelection(); }))
            .Add(mwfl::Command(kWorkspaceRecycle, L"Move Selected Item to Recycle Bin", [this] { RecycleWorkspaceSelection(); }))
            .Add(mwfl::Command(kWorkspaceReveal, L"Reveal Selected Item in Explorer", [this] { RevealWorkspaceSelection(); }))
            .Add(mwfl::Command(kWorkspaceCopyPath, L"Copy Full Path", [this] { CopyWorkspacePath(false); }))
            .Add(mwfl::Command(kWorkspaceCopyRelativePath, L"Copy Relative Path", [this] { CopyWorkspacePath(true); }))
            .Add(mwfl::Command(kWorkspaceRemoveRoot, L"Remove Selected Workspace Root", [this] { RemoveWorkspaceRoot(); }));
        commands_
            .Add(mwfl::Command(kWorkspaceManager, L"Recent / Favorite Workspaces...", [this] { ShowWorkspaceManager(); }))
            .Add(mwfl::Command(kFavoriteWorkspace, L"Favorite Active Workspace", [this] { ToggleFavoriteWorkspace(); }));
        commands_
            .Add(mwfl::Command(kSaveNamedSession, L"Save Named Session...", [this] { SaveNamedSession(); }))
            .Add(mwfl::Command(kOpenNamedSession, L"Open Named Session...", [this] { OpenNamedSession(); }));
        commands_
            .Add(mwfl::Command(kPinTab, L"Pin / Unpin Active Tab", [this] { TogglePinActive(); }))
            .Add(mwfl::Command(kSortTabs, L"Sort Tabs by Name", [this] { SortTabs(); }))
            .Add(mwfl::Command(kCloseOtherTabs, L"Close Other Tabs", [this] { CloseRelativeTabs(0); }))
            .Add(mwfl::Command(kCloseLeftTabs, L"Close Tabs to the Left", [this] { CloseRelativeTabs(-1); }))
            .Add(mwfl::Command(kCloseRightTabs, L"Close Tabs to the Right", [this] { CloseRelativeTabs(1); }))
            .Add(mwfl::Command(kOpenNewWindow, L"Open Active Document in New Window", [this] { OpenActiveInNewWindow(); }))
            .Add(mwfl::Command(kPreviousLargeWindow, L"Previous Large-file Window", [this] { MoveLargeFileWindow(false); }))
            .Add(mwfl::Command(kNextLargeWindow, L"Next Large-file Window", [this] { MoveLargeFileWindow(true); }));
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
        AddLanguageCommands();
        RefreshRecentCommands();
    }

    void AddLanguageCommands() {
        for (const auto language : notepad_colon::AllLanguages()) {
            const auto id = mwfl::ControlId{static_cast<WORD>(
                kLanguageBase.value + static_cast<WORD>(language))};
            commands_.Add(mwfl::Command(id, std::wstring(notepad_colon::LanguageName(language)),
                [this, language] { SetActiveLanguage(language); }));
        }
        std::size_t custom_index = 0;
        for (const auto& language : language_registry_.Languages()) {
            if (language.builtin) continue;
            const auto id = mwfl::ControlId{static_cast<WORD>(
                kCustomLanguageBase.value + custom_index)};
            const auto language_id = language.id;
            commands_.Add(mwfl::Command(id, language.name,
                [this, language_id] { SetActiveRegisteredLanguage(language_id); }));
            ++custom_index;
        }
    }

    void ApplyUiLanguage() {
        const auto set = [&](mwfl::ControlId id, std::wstring_view english, std::wstring_view chinese) {
            if (auto* command = commands_.Find(id)) command->SetText(std::wstring(chinese_ui_ ? chinese : english));
        };
        set(kNew, L"&New", L"新建(&N)"); set(kOpen, L"&Open...", L"打开(&O)...");
        set(kOpenFolder, L"Open &Folder...", L"打开文件夹(&F)...");
        set(kSave, L"&Save", L"保存(&S)"); set(kSaveAs, L"Save &As...", L"另存为(&A)...");
        set(kSaveAll, L"Save A&ll", L"全部保存(&L)"); set(kClose, L"&Close", L"关闭(&C)");
        set(kExit, L"E&xit", L"退出(&X)"); set(kUndo, L"&Undo", L"撤销(&U)");
        set(kRedo, L"&Redo", L"重做(&R)"); set(kCut, L"Cu&t", L"剪切(&T)");
        set(kCopy, L"&Copy", L"复制(&C)"); set(kPaste, L"&Paste", L"粘贴(&P)");
        set(kSelectAll, L"Select &All", L"全选(&A)"); set(kFindNext, L"&Find Next", L"查找下一个(&F)");
        set(kFindInFiles, L"Find in Files", L"在文件中查找");
        set(kToggleWorkspace, L"Workspace Panel", L"工作区面板");
        set(kToggleResults, L"Search Results Panel", L"搜索结果面板");
        set(kPreferences, L"&Preferences...", L"首选项(&P)...");
        set(kEncodingInfo, L"Document Encoding Information...", L"文档编码信息...");
        set(kPrint, L"&Print...", L"打印(&P)...");
        set(kAbout, L"&About Notepad::", L"关于 Notepad::(&A)");
    }

    void SetUiLanguage(bool chinese) {
        if (chinese_ui_ == chinese) return;
        chinese_ui_ = chinese; ApplyUiLanguage(); BuildMenu();
        if (!IsTestMode()) static_cast<void>(SavePreferences());
        status_.SetText(chinese_ui_ ? L"界面语言已切换为简体中文" : L"UI language changed to English");
    }

    void BuildMenu() {
        mwfl::Menu file, edit, search, encoding, line_endings, language, view, code, tools, help;
        mwfl::Must(menu_.Create(), "create menu bar");
        mwfl::Must(file.CreatePopup(), "create file menu");
        mwfl::Must(edit.CreatePopup(), "create edit menu");
        mwfl::Must(search.CreatePopup(), "create search menu");
        mwfl::Must(encoding.CreatePopup(), "create encoding menu");
        mwfl::Must(line_endings.CreatePopup(), "create line endings menu");
        mwfl::Must(language.CreatePopup(), "create language menu");
        mwfl::Must(view.CreatePopup(), "create view menu");
        mwfl::Must(code.CreatePopup(), "create code menu");
        mwfl::Must(tools.CreatePopup(), "create tools menu");
        mwfl::Must(help.CreatePopup(), "create help menu");
        for (const auto id : {kNew, kOpen, kOpenFolder, kSave, kSaveAs, kSaveAll,
                              kSaveNamedSession, kOpenNamedSession,
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
        for (const auto id : {kUndo, kRedo, kCut, kCopy, kPaste, kSelectAll,
                              kCompleteWord,
                              kPinTab, kSortTabs, kCloseOtherTabs, kCloseLeftTabs,
                              kCloseRightTabs, kOpenNewWindow, kPreviousLargeWindow, kNextLargeWindow})
            mwfl::Must(edit.AppendCommand(*commands_.Find(id)), "append edit command");
        for (const auto id : {kFindNext, kReplaceNext, kReplaceAll, kSearchMatchCase,
                              kSearchWholeWord, kSearchRegex, kSearchSelection,
                              kGoToLineColumn, kDocumentSymbols, kQuickOpen, kPreviousSearch,
                              kMarkAll, kClearSearchMarks, kFindInFiles, kCancelSearch})
            mwfl::Must(search.AppendCommand(*commands_.Find(id)), "append search command");
        for (const auto id : {kUtf8, kUtf8Bom, kUtf16Le, kUtf16Be, kSaveSystemAnsi,
                              kEncodingInfo, kReopenSystemAnsi, kReopenWindows1252, kReopenGb18030})
            mwfl::Must(encoding.AppendCommand(*commands_.Find(id)), "append encoding command");
        for (const auto id : {kCrlf, kLf})
            mwfl::Must(line_endings.AppendCommand(*commands_.Find(id)), "append line ending command");
        for (const auto value : notepad_colon::AllLanguages()) {
            const auto id = mwfl::ControlId{static_cast<WORD>(
                kLanguageBase.value + static_cast<WORD>(value))};
            mwfl::Must(language.AppendCommand(*commands_.Find(id)), "append language command");
        }
        std::size_t custom_index = 0;
        for (const auto& definition : language_registry_.Languages()) {
            if (definition.builtin) continue;
            const auto id = mwfl::ControlId{static_cast<WORD>(
                kCustomLanguageBase.value + custom_index)};
            mwfl::Must(language.AppendCommand(*commands_.Find(id)), "append custom language command");
            ++custom_index;
        }
        if (custom_index != 0)
            mwfl::Must(language.AppendSeparator(), "append custom language separator");
        mwfl::Must(language.AppendCommand(*commands_.Find(kReloadLanguages)),
                   "append reload languages command");
        for (const auto id : {kToggleFindBar, kToggleWorkspace, kToggleResults,
                              kWhitespace, kWordWrap, kZoomIn, kZoomOut, kZoomReset,
                              kRectangular, kToggleFold, kToggleBookmark, kNextBookmark,
                              kFollowTail, kRefreshGitChanges})
            mwfl::Must(view.AppendCommand(*commands_.Find(id)), "append view command");
        for (const auto id : {kWorkspaceRefresh, kWorkspaceNewFile, kWorkspaceNewFolder,
                              kWorkspaceRename, kWorkspaceRecycle, kWorkspaceReveal,
                              kWorkspaceCopyPath, kWorkspaceCopyRelativePath, kWorkspaceRemoveRoot,
                              kWorkspaceManager, kFavoriteWorkspace})
            mwfl::Must(view.AppendCommand(*commands_.Find(id)), "append workspace command");
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
                              kCompareWithDisk, kRegisterAssociation, kRemoveAssociation,
                              kEnglishUi, kChineseUi, kOpenTerminal})
            mwfl::Must(tools.AppendCommand(*commands_.Find(id)), "append tools command");
        mwfl::Must(help.AppendCommand(*commands_.Find(kAbout)), "append about command");
        mwfl::Must(menu_.AppendSubmenu(std::move(file), chinese_ui_ ? L"文件(&F)" : L"&File"), "append file menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(edit), chinese_ui_ ? L"编辑(&E)" : L"&Edit"), "append edit menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(search), chinese_ui_ ? L"搜索(&S)" : L"&Search"), "append search menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(encoding), chinese_ui_ ? L"编码(&C)" : L"En&coding"), "append encoding menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(line_endings), chinese_ui_ ? L"换行符(&O)" : L"&EOL"), "append line endings menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(language), chinese_ui_ ? L"语言(&L)" : L"&Language"), "append language menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(view), chinese_ui_ ? L"视图(&V)" : L"&View"), "append view menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(code), chinese_ui_ ? L"代码(&D)" : L"&Code"), "append code menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(tools), chinese_ui_ ? L"工具(&T)" : L"&Tools"), "append tools menu");
        mwfl::Must(menu_.AppendSubmenu(std::move(help), chinese_ui_ ? L"帮助(&H)" : L"&Help"), "append help menu");
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
        ::ShowWindow(workspace_filter_.GetHwnd(), workspace_visible_ ? SW_SHOW : SW_HIDE);
        ::ShowWindow(results_.GetHwnd(), results_visible_ ? SW_SHOW : SW_HIDE);
        SetLayout(mwfl::Column()
            .Add(toolbar_, mwfl::Auto())
            .Add(mwfl::Row().Gap(4.0_dip).Margin(3.0_dip)
                .Add(search_, mwfl::Stretch())
                .Add(replacement_, mwfl::Stretch()),
                mwfl::Fixed(find_bar_visible_ ? 30.0_dip : 0.0_dip))
            .Add(mwfl::Row().Gap(workspace_visible_ ? 3.0_dip : 0.0_dip)
                .Add(mwfl::Column().Gap(3.0_dip)
                    .Add(workspace_filter_, mwfl::Fixed(workspace_visible_ ? 26.0_dip : 0.0_dip))
                    .Add(tree_, mwfl::Stretch()),
                    mwfl::Fixed(workspace_visible_ ? 220.0_dip : 0.0_dip))
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
            *editor, lexilla_, notepad_colon::Language::plain_text, IsDark()), "configure plain-text lexer");
        mwfl::Must(editor->SetText(text), "set document text");
        editor->SetSavePoint();
        mwfl::Must(static_cast<bool>(workspace_.Add({id, title, {}})), "add document metadata");
        mwfl::Must(adapter_.BindPage(id, editor->GetHwnd()) == mwfl::DocumentTabStatus::success,
                   "bind document page");
        mwfl::Must(mwfl::SetAccessibleName(editor->GetHwnd(), title.c_str()), "name document editor");
        documents_.push_back({id, std::move(editor)});
        InitializeSyntaxTree(documents_.back());
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
            status_.SetText(L"File exceeds the 4 GiB mapped-view limit");
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
            status_.SetText(L"Binary content detected; use a hex editor"); return false;
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
                status_.SetText(L"File changed while it was being opened; try again");
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
            status_.SetText(L"Opened with text safety warnings; use Document Encoding Information");
        } else SyncPresentation(protected_mode
            ? (large_editable ? L"Opened in large-file streaming edit mode"
                              : L"Opened in large-file read-only mode")
            : L"Opened");
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
                status_.SetText(L"Backup could not be created; save cancelled");
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
                status_.SetText(L"ANSI save cancelled: text is not representable or the disk file changed");
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
            const auto* registered = language_registry_.Find(document.language_id);
            if (registered && !registered->builtin)
                static_cast<void>(notepad_colon::ConfigureRegisteredLanguage(
                    *document.editor, lexilla_, *registered, dark));
            else static_cast<void>(notepad_colon::ConfigureLanguage(
                    *document.editor, lexilla_, document.language, dark,
                    document.mapped_file ? notepad_colon::SyntaxPerformanceMode::lightweight
                                         : notepad_colon::SyntaxPerformanceMode::full));
            if (document.syntax_tree || document.wasm_syntax) {
                notepad_colon::ConfigureTreeSitterStyles(*document.editor, dark);
                StyleVisibleSyntax(document);
            }
        }
    }

    void SetActiveLanguage(notepad_colon::Language language) {
        auto* document = ActiveDocument();
        if (!document || document->language == language) return;
        if (!notepad_colon::ConfigureLanguage(
                *document->editor, lexilla_, language, IsDark(),
                document->mapped_file ? notepad_colon::SyntaxPerformanceMode::lightweight
                                      : notepad_colon::SyntaxPerformanceMode::full)) {
            status_.SetText(L"Could not load the selected syntax lexer");
            return;
        }
        document->language = language;
        for (const auto& definition : language_registry_.Languages())
            if (definition.builtin == language) { document->language_id = definition.id; break; }
        InitializeSyntaxTree(*document);
        status_.SetText(L"Language: " + std::wstring(notepad_colon::LanguageName(language)));
    }

    static std::filesystem::path LanguageDirectory(bool test_mode) {
        if (test_mode) return std::filesystem::temp_directory_path() /
            (L"notepad-colon-gui-languages-" + std::to_wstring(::GetCurrentProcessId()));
        wchar_t local_app_data[32768]{};
        const DWORD length = ::GetEnvironmentVariableW(
            L"LOCALAPPDATA", local_app_data, static_cast<DWORD>(std::size(local_app_data)));
        return length > 0 && length < std::size(local_app_data)
            ? std::filesystem::path{local_app_data} / L"mwfl" / L"Notepad Colon" / L"languages"
            : std::filesystem::path{};
    }

    void LoadLanguageDefinitions() {
        language_registry_.ResetBuiltins();
        const auto directory = LanguageDirectory(IsTestMode());
        if (!directory.empty()) static_cast<void>(language_registry_.LoadDirectory(directory));
    }

    void ReloadLanguageDefinitions() {
        LoadLanguageDefinitions();
        AddLanguageCommands();
        BuildMenu();
        mwfl::Must(accelerators_.Create(commands_), "recreate language accelerators");
        SetAccelerators(accelerators_.GetHandle());
        const auto custom = std::ranges::count_if(language_registry_.Languages(),
            [](const auto& language) { return !language.builtin.has_value(); });
        status_.SetText(L"Language definitions reloaded: " + std::to_wstring(custom) +
            L" custom, " + std::to_wstring(language_registry_.Errors().size()) + L" error(s)");
    }

    void SetActiveRegisteredLanguage(std::string_view id) {
        const auto* language = language_registry_.Find(id);
        auto* document = ActiveDocument();
        if (!language || !document) return;
        if (language->builtin) { SetActiveLanguage(*language->builtin); return; }
        if (!notepad_colon::ConfigureRegisteredLanguage(
                *document->editor, lexilla_, *language, IsDark())) {
            status_.SetText(L"Could not load the custom language fallback lexer");
            return;
        }
        document->language = notepad_colon::Language::plain_text;
        document->language_id = language->id;
        InitializeSyntaxTree(*document);
        status_.SetText(L"Language: " + language->name);
    }

    static std::optional<std::string> EditorUtf8(const mwfl::ScintillaEditor& editor) noexcept {
        try {
            const auto length = editor.GetLength();
            if (length < 0 || length > static_cast<mwfl::ScintillaPosition>(UINT32_MAX))
                return std::nullopt;
            std::string text(static_cast<std::size_t>(length) + 1, '\0');
            editor.Send(SCI_GETTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()));
            text.resize(static_cast<std::size_t>(length));
            return text;
        } catch (...) { return std::nullopt; }
    }

    void InitializeSyntaxTree(EditorDocument& document) noexcept {
        document.syntax_tree.reset();
        document.wasm_syntax.reset();
        if (document.mapped_file) return;
        const auto text = EditorUtf8(*document.editor);
        if (!text || text->size() > 8u * 1024 * 1024) return;
        try {
            if (const auto* language = language_registry_.Find(document.language_id);
                language && language->tree_sitter && language->tree_sitter->grammar == "wasm") {
                auto syntax = std::make_unique<notepad_colon::WasmSyntaxClient>();
                const auto host = ExecutablePath().parent_path() / L"notepad-colon-language-host.exe";
                if (!syntax->Start(host, language->tree_sitter->wasm_language_name,
                    language->tree_sitter->wasm_bytes, language->tree_sitter->highlights_query,
                    language->tree_sitter->symbols_query) || !syntax->Parse(*text)) {
                    status_.SetText(L"Wasm language host rejected or timed out"); return;
                }
                document.wasm_syntax = std::move(syntax);
                notepad_colon::ConfigureTreeSitterStyles(*document.editor, IsDark());
                StyleVisibleSyntax(document);
                return;
            }
            auto syntax = std::make_unique<notepad_colon::TreeSitterDocument>();
            bool configured = false;
            if (document.language == notepad_colon::Language::cpp) configured = syntax->ConfigureCpp();
            else if (document.language == notepad_colon::Language::json) configured = syntax->ConfigureJson();
            else if (document.language == notepad_colon::Language::python)
                configured = syntax->ConfigurePython();
            else if (document.language == notepad_colon::Language::javascript)
                configured = syntax->ConfigureJavaScript();
            else if (document.language == notepad_colon::Language::typescript) {
                const auto* metadata = workspace_.Find(document.id);
                configured = syntax->ConfigureTypeScript(
                    metadata && _wcsicmp(metadata->path.extension().c_str(), L".tsx") == 0);
            }
            else if (const auto* language = language_registry_.Find(document.language_id);
                     language && language->tree_sitter) {
                if (language->tree_sitter->grammar == "cpp")
                    configured = syntax->ConfigureCpp(language->tree_sitter->highlights_query,
                                                       language->tree_sitter->symbols_query);
                else if (language->tree_sitter->grammar == "json")
                    configured = syntax->ConfigureJson(language->tree_sitter->highlights_query,
                                                        language->tree_sitter->symbols_query);
            }
            if (!configured || !syntax->Parse(*text)) return;
            document.syntax_tree = std::move(syntax);
            notepad_colon::ConfigureTreeSitterStyles(*document.editor, IsDark());
            StyleVisibleSyntax(document);
        } catch (...) { document.syntax_tree.reset(); document.wasm_syntax.reset(); }
    }

    void StyleVisibleSyntax(EditorDocument& document) noexcept {
        if (!document.syntax_tree && !document.wasm_syntax) return;
        const auto first_visible = document.editor->Send(SCI_GETFIRSTVISIBLELINE);
        const auto visible_count = document.editor->Send(SCI_LINESONSCREEN);
        const auto first_line = document.editor->Send(
            SCI_DOCLINEFROMVISIBLE, (std::max<LRESULT>)(0, first_visible - 64));
        const auto last_line = document.editor->Send(
            SCI_DOCLINEFROMVISIBLE, first_visible + visible_count + 64);
        const auto start = document.editor->Send(SCI_POSITIONFROMLINE, first_line);
        auto end = document.editor->Send(SCI_GETLINEENDPOSITION, last_line);
        if (end < 0) end = document.editor->GetLength();
        const auto start_byte = static_cast<std::uint32_t>((std::max<LRESULT>)(0, start));
        const auto end_byte = static_cast<std::uint32_t>((std::max<LRESULT>)(0, end));
        if (document.syntax_tree)
            notepad_colon::ApplyTreeSitterHighlights(*document.editor, *document.syntax_tree,
                                                     start_byte, end_byte);
        else {
            const auto spans = document.wasm_syntax->Highlights(start_byte, end_byte);
            notepad_colon::ApplySyntaxSpans(*document.editor, spans, start_byte, end_byte);
        }
    }

    void UpdateLargeBuffer(EditorDocument& document, const SCNotification& notification) noexcept {
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
            status_.SetText(L"Large-file edit could not be recorded; switched to read-only protection");
        } else workspace_.SetDirty(document.id, true);
    }

    void UpdateSyntaxTree(EditorDocument& document, const SCNotification& notification) noexcept {
        if ((!document.syntax_tree && !document.wasm_syntax) ||
            (notification.modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) == 0)
            return;
        const auto text = EditorUtf8(*document.editor);
        if (!text) return;
        const auto start = static_cast<std::uint32_t>((std::max<Sci_Position>)(0, notification.position));
        const auto length = static_cast<std::uint32_t>((std::max<Sci_Position>)(0, notification.length));
        const auto line = static_cast<std::uint32_t>((std::max<Sci_Position>)(0,
            document.editor->Send(SCI_LINEFROMPOSITION, start)));
        const auto line_start = document.editor->Send(SCI_POSITIONFROMLINE, line);
        const auto column = start - static_cast<std::uint32_t>((std::max<LRESULT>)(0, line_start));
        auto advanced_row = line;
        auto advanced_column = column;
        if (notification.text) {
            for (std::uint32_t index = 0; index < length; ++index) {
                if (notification.text[index] == '\n') { ++advanced_row; advanced_column = 0; }
                else ++advanced_column;
            }
        }
        notepad_colon::SyntaxEdit edit;
        edit.start_byte = start;
        edit.start_row = line;
        edit.start_column = column;
        if ((notification.modificationType & SC_MOD_INSERTTEXT) != 0) {
            edit.old_end_byte = start;
            edit.new_end_byte = start + length;
            edit.old_end_row = line;
            edit.old_end_column = column;
            edit.new_end_row = advanced_row;
            edit.new_end_column = advanced_column;
        } else {
            edit.old_end_byte = start + length;
            edit.new_end_byte = start;
            edit.old_end_row = advanced_row;
            edit.old_end_column = advanced_column;
            edit.new_end_row = line;
            edit.new_end_column = column;
        }
        const bool reparsed = document.syntax_tree
            ? document.syntax_tree->Reparse(*text, edit)
            : document.wasm_syntax->Reparse(*text, edit);
        if (reparsed) StyleVisibleSyntax(document);
        else if (document.wasm_syntax) {
            document.wasm_syntax.reset();
            status_.SetText(L"Wasm language host stopped after a timeout or protocol failure");
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
            mwfl::SettingDefinition{L"EnsureFinalNewline", mwfl::SettingType::dword, 4, false},
            mwfl::SettingDefinition{L"ChineseUi", mwfl::SettingType::dword, 4, false}};
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
        if (const auto* value = mwfl::FindSetting(loaded.values, L"ChineseUi"))
            chinese_ui_ = std::get<std::uint32_t>(value->data) != 0;
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
            mwfl::SettingValue{L"EnsureFinalNewline", static_cast<std::uint32_t>(preferences_.ensure_final_newline)},
            mwfl::SettingValue{L"ChineseUi", static_cast<std::uint32_t>(chinese_ui_)}};
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
            .owner = GetHwnd(), .title = L"Notepad:: Preferences",
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

    bool SaveLargeDocument(EditorDocument& document, const std::filesystem::path& path,
                           const mwfl::WorkspaceDocument& metadata) {
        if (!document.large_buffer ||
            (document.detected_encoding != notepad_colon::EncodingKind::utf8 &&
             document.detected_encoding != notepad_colon::EncodingKind::utf8_bom))
            return false;
        if (path == metadata.path && notepad_colon::CaptureFileState(path) != document.file_state) {
            status_.SetText(L"Large-file save cancelled because the disk file changed");
            return false;
        }
        if (preferences_.create_backup_before_save && std::filesystem::exists(path)) {
            auto backup = path;
            backup += L".bak";
            if (!::CopyFileW(path.c_str(), backup.c_str(), FALSE)) {
                status_.SetText(L"Large-file backup could not be created; save cancelled");
                return false;
            }
        }
        if (document.mapped_file) document.mapped_file->Close();
        if (!document.large_buffer->SaveAs(path)) {
            if (document.mapped_file) static_cast<void>(document.mapped_file->Open(metadata.path));
            status_.SetText(L"Large-file streaming save failed; the original file was retained");
            return false;
        }
        if (document.mapped_file && !document.mapped_file->Open(path)) {
            status_.SetText(L"Saved, but the mapped view could not be reopened");
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
        SyncPresentation(L"Large file saved by streaming modified pieces");
        static_cast<void>(SaveSessionSnapshot());
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

    void ShowGoToLineColumn() {
        const auto value = PromptWorkspaceName(L"Go to Line / Column", L"1:1", false);
        auto* editor = ActiveEditor();
        if (!value || !editor) return;
        wchar_t* end = nullptr;
        const auto line = std::wcstoull(value->c_str(), &end, 10);
        std::uint64_t column = 1;
        if (end && (*end == L':' || *end == L',')) column = std::wcstoull(end + 1, nullptr, 10);
        if (line == 0 || column == 0) { status_.SetText(L"Line and column must be positive"); return; }
        const auto line_index = static_cast<LRESULT>((std::min<std::uint64_t>)(
            line - 1, static_cast<std::uint64_t>((std::max<LRESULT>)(0, editor->Send(SCI_GETLINECOUNT) - 1))));
        const auto position = editor->Send(SCI_FINDCOLUMN, line_index,
            static_cast<LPARAM>((std::min<std::uint64_t>)(column - 1, INT_MAX)));
        editor->SetSelection({position, position}); editor->Send(SCI_SCROLLCARET); editor->Focus();
        status_.SetText(L"Moved to line " + std::to_wstring(line_index + 1));
    }

    void RefreshGitChangeMarkers() {
        auto* document = ActiveDocument();
        const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
        if (!document || !metadata || metadata->path.empty()) {
            status_.SetText(L"Save the document before reading Git changes"); return;
        }
        const auto changes = notepad_colon::QueryGitChangedLines(metadata->path);
        document->editor->Send(SCI_MARKERDEFINE, 3, SC_MARK_LEFTRECT);
        document->editor->Send(SCI_MARKERSETBACK, 3, RGB(70, 180, 90));
        document->editor->Send(SCI_MARKERDELETEALL, 3);
        for (const auto line : changes.added_or_modified)
            if (line > 0) document->editor->Send(SCI_MARKERADD, line - 1, 3);
        status_.SetText(changes.repository
            ? L"Git markers: " + std::to_wstring(changes.added_or_modified.size()) + L" changed line(s)"
            : L"Git is unavailable or the file is not in a repository");
    }

    void OpenWorkspaceTerminal() {
        auto directory = workspace_root_;
        if (const auto* document = ActiveDocument())
            if (const auto* metadata = workspace_.Find(document->id); metadata && !metadata->path.empty())
                directory = metadata->path.parent_path();
        if (directory.empty()) { status_.SetText(L"Open a workspace or saved document first"); return; }
        auto command = L"cmd.exe /K cd /d \"" + directory.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        if (::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                             CREATE_NEW_CONSOLE, nullptr, directory.c_str(), &startup, &process)) {
            ::CloseHandle(process.hThread); ::CloseHandle(process.hProcess);
            status_.SetText(L"Terminal opened");
        } else status_.SetText(L"Could not open terminal");
    }

    void ShowQuickOpen() {
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
        if (paths.empty()) { status_.SetText(L"Open a workspace before Quick Open"); return; }
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
        mwfl::Dialog dialog({.owner = GetHwnd(), .title = L"Quick Open File",
            .initial_client_size = {620.0_dip, 430.0_dip}, .resizable = true,
            .callbacks = {.initialize = [&](HWND window) {
                mwfl::ControlHost ui{window}; ui.Add(query, {743}, L""); ui.Add(list, {744}, {});
                ui.Add(open, {IDOK}, L"Open"); ui.Add(cancel, {IDCANCEL}, L"Cancel");
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
        if (chosen && !OpenPath(paths[*chosen])) status_.SetText(L"Quick Open failed");
    }

    void ToggleActiveLineComment() {
        auto* document = ActiveDocument();
        if (!document) return;
        std::string_view prefix = "//";
        switch (document->language) {
        case notepad_colon::Language::python:
        case notepad_colon::Language::powershell:
        case notepad_colon::Language::yaml:
        case notepad_colon::Language::cmake: prefix = "#"; break;
        case notepad_colon::Language::sql: prefix = "--"; break;
        case notepad_colon::Language::ini: prefix = ";"; break;
        case notepad_colon::Language::batch: prefix = "rem "; break;
        default: break;
        }
        notepad_colon::ToggleLineComment(*document->editor, prefix);
    }

    void ShowLocalCompletion() {
        auto* document = ActiveDocument();
        if (!document || document->large_buffer || document->mapped_file) {
            status_.SetText(L"Local completion is disabled for large files"); return;
        }
        const auto text = EditorUtf8(*document->editor);
        if (!text) return;
        std::vector<notepad_colon::DocumentSymbol> symbols;
        if (document->syntax_tree) symbols = document->syntax_tree->Symbols(*text);
        else if (document->wasm_syntax) symbols = document->wasm_syntax->Symbols();
        const auto caret = document->editor->GetSelection().end;
        const auto completion = notepad_colon::CompleteLocally(
            *text, static_cast<std::size_t>(caret), document->language, symbols);
        if (completion.candidates.empty()) {
            status_.SetText(L"No local completion candidates"); return;
        }
        std::string list;
        for (const auto& candidate : completion.candidates) {
            if (!list.empty()) list.push_back(' ');
            list += candidate;
        }
        document->editor->Send(SCI_AUTOCSETIGNORECASE, TRUE);
        document->editor->Send(SCI_AUTOCSETMAXHEIGHT, 12);
        document->editor->Send(SCI_AUTOCSETDROPRESTOFWORD, TRUE);
        document->editor->Send(SCI_AUTOCSHOW, completion.prefix_bytes,
                               reinterpret_cast<LPARAM>(list.c_str()));
    }

    void ShowDocumentSymbols() {
        auto* document = ActiveDocument();
        const auto text = document ? EditorUtf8(*document->editor) : std::nullopt;
        if (!document || (!document->syntax_tree && !document->wasm_syntax) || !text) {
            status_.SetText(L"Document symbols require an active Tree-sitter language"); return;
        }
        const auto symbols = document->syntax_tree
            ? document->syntax_tree->Symbols(*text) : document->wasm_syntax->Symbols();
        if (symbols.empty()) { status_.SetText(L"No document symbols found"); return; }
        mwfl::TextBox query; mwfl::ListBox list; mwfl::Button open, cancel;
        std::vector<std::size_t> visible; std::optional<std::size_t> chosen;
        mwfl::Dialog* pointer = nullptr;
        const auto refresh = [&] {
            list.ClearItems(); visible.clear();
            auto filter = query.GetText(); std::ranges::transform(filter, filter.begin(), ::towlower);
            for (std::size_t index = 0; index < symbols.size(); ++index) {
                auto name = mwfl::FromUtf8(symbols[index].name).value_or(L"?");
                auto folded = name; std::ranges::transform(folded, folded.begin(), ::towlower);
                if (!filter.empty() && folded.find(filter) == std::wstring::npos) continue;
                visible.push_back(index);
                list.AddItem(name + L"  [" + mwfl::FromUtf8(symbols[index].kind).value_or(L"symbol") + L"]");
            }
            if (!visible.empty()) list.SetSelection(0);
        };
        mwfl::Dialog dialog({.owner = GetHwnd(), .title = L"Document Symbols",
            .initial_client_size = {520.0_dip, 420.0_dip}, .resizable = true,
            .callbacks = {.initialize = [&](HWND window) {
                mwfl::ControlHost ui{window}; ui.Add(query, {741}, L""); ui.Add(list, {742}, {});
                ui.Add(open, {IDOK}, L"Go to"); ui.Add(cancel, {IDCANCEL}, L"Cancel");
                const auto layout = pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(6.0_dip)
                    .Add(query, mwfl::Fixed(30.0_dip)).Add(list, mwfl::Stretch())
                    .Add(mwfl::Row().Gap(6.0_dip).Add(mwfl::Column(), mwfl::Stretch())
                        .Add(open, mwfl::Fixed(80.0_dip)).Add(cancel, mwfl::Fixed(80.0_dip)),
                        mwfl::Fixed(30.0_dip)));
                refresh(); query.Focus(); return layout;
            }, .command = [&](HWND, WORD id, WORD notification) {
                if (id == 741 && notification == EN_CHANGE) { refresh(); return true; }
                if (id == IDOK || (id == 742 && notification == LBN_DBLCLK)) {
                    const auto selected = list.GetSelectedIndex();
                    if (selected && static_cast<std::size_t>(*selected) < visible.size()) {
                        chosen = visible[static_cast<std::size_t>(*selected)]; pointer->Accept(); return true;
                    }
                }
                return false;
            }}});
        pointer = &dialog; static_cast<void>(dialog.ShowModal());
        if (chosen) {
            const auto& symbol = symbols[*chosen];
            document->editor->SetSelection({symbol.start_byte, symbol.start_byte});
            document->editor->Send(SCI_SCROLLCARET); document->editor->Focus();
        }
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
        const auto encoded = notepad_colon::SerializeConfiguration(
            {preferences_, CaptureShortcuts(), search_history_});
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
            .filters = {{L"Notepad:: configuration", L"*.npcconfig"}},
            .default_extension = L"npcconfig", .path_must_exist = false});
        if (selected.accepted)
            status_.SetText(SaveConfigurationFile(selected.path) ? L"Settings exported" : L"Settings export failed");
    }

    void ImportConfiguration() {
        const auto selected = mwfl::ShowOpenFileDialog({.owner = GetHwnd(), .title = L"Import settings",
            .filters = {{L"Notepad:: configuration", L"*.npcconfig"}, {L"All files", L"*.*"}}});
        if (!selected.accepted) return;
        std::ifstream input(selected.path, std::ios::binary); std::string encoded{
            std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        notepad_colon::Configuration configuration;
        if ((!input && encoded.empty()) || !notepad_colon::DeserializeConfiguration(encoded, configuration) ||
            !ApplyShortcuts(configuration.shortcuts, false)) {
            status_.SetText(L"Settings import rejected: invalid or conflicting configuration"); return;
        }
        preferences_ = configuration.preferences; ApplyAppearance();
        search_history_ = configuration.search_history;
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
                                              L"Notepad::", MB_ICONWARNING | MB_YESNOCANCEL);
            if (answer == IDCANCEL || (answer == IDYES && !SaveActive(false))) return false;
        }
        auto found = std::ranges::find(documents_, *id, &EditorDocument::id);
        if (found == documents_.end()) return false;
        const HWND hwnd = found->editor->GetHwnd();
        if (!workspace_.Close(*id) || adapter_.UnbindPage(*id) != mwfl::DocumentTabStatus::success)
            return false;
        std::erase(pinned_documents_, *id);
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

    bool IsPinned(mwfl::DocumentId id) const noexcept {
        return std::ranges::find(pinned_documents_, id) != pinned_documents_.end();
    }

    void TogglePinActive() {
        const auto id = workspace_.GetActiveId();
        if (!id) return;
        const auto found = std::ranges::find(pinned_documents_, *id);
        if (found == pinned_documents_.end()) {
            pinned_documents_.push_back(*id);
            static_cast<void>(workspace_.Move(*id, 0));
            status_.SetText(L"Tab pinned");
        } else {
            pinned_documents_.erase(found);
            status_.SetText(L"Tab unpinned");
        }
        SynchronizeTabs(true);
    }

    void SortTabs() {
        std::vector<mwfl::WorkspaceDocument> ordered(workspace_.GetDocuments().begin(),
                                                     workspace_.GetDocuments().end());
        std::ranges::stable_sort(ordered, [&](const auto& left, const auto& right) {
            const bool left_pinned = IsPinned(left.id), right_pinned = IsPinned(right.id);
            if (left_pinned != right_pinned) return left_pinned;
            return ::CompareStringOrdinal(left.title.c_str(), -1, right.title.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
        for (std::size_t index = 0; index < ordered.size(); ++index)
            static_cast<void>(workspace_.Move(ordered[index].id, index));
        SynchronizeTabs(true); status_.SetText(L"Tabs sorted (pinned tabs first)");
    }

    void CloseRelativeTabs(int direction) {
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

    void OpenActiveInNewWindow() {
        const auto* metadata = workspace_.GetActiveId() ? workspace_.Find(*workspace_.GetActiveId()) : nullptr;
        if (!metadata || metadata->path.empty()) {
            status_.SetText(L"Save the document before opening it in another window"); return;
        }
        auto command_line = L"\"" + ExecutablePath().wstring() + L"\" \"" + metadata->path.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        if (::CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                             &startup, &process)) {
            ::CloseHandle(process.hThread); ::CloseHandle(process.hProcess);
            status_.SetText(L"Document sent to a new window");
        } else status_.SetText(L"Could not open a new window");
    }

    void ToggleSearchOption(mwfl::ControlId id, bool& value) {
        value = !value;
        if (auto* command = commands_.Find(id)) {
            command->SetChecked(value);
            menu_.UpdateCommand(*command);
        }
    }

    void ToggleSearchSelection() {
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
        status_.SetText(search_selection_ ? L"Search is limited to the captured selection"
                                          : L"Search uses the whole document");
    }

    mwfl::ScintillaSearchFlags SearchFlags() const noexcept {
        auto flags = mwfl::ScintillaSearchFlags::none;
        if (search_match_case_) flags = flags | mwfl::ScintillaSearchFlags::match_case;
        if (search_whole_word_) flags = flags | mwfl::ScintillaSearchFlags::whole_word;
        if (search_regex_) flags = flags | mwfl::ScintillaSearchFlags::regular_expression;
        return flags;
    }

    void IncrementalSearch() {
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

    void RememberSearch(std::wstring_view query) {
        if (query.empty()) return;
        std::erase(search_history_, query);
        search_history_.insert(search_history_.begin(), std::wstring{query});
        if (search_history_.size() > 20) search_history_.resize(20);
    }

    void MarkAllSearchMatches() {
        auto* document = ActiveDocument();
        if (!document) return;
        const auto query = search_.GetText();
        if (query.empty()) return;
        RememberSearch(query);
        const auto scope = search_scope_.value_or(mwfl::ScintillaTextRange{
            0, document->editor->GetLength()});
        const auto count = notepad_colon::MarkAllMatches(
            *document->editor, query, SearchFlags(), scope);
        status_.SetText(L"Marked " + std::to_wstring(count) + L" occurrence(s)" +
            (count == 10000 ? L" (limit reached)" : L"") +
            (document->large_buffer ? L" in the visible large-file window" : L""));
    }

    bool LoadLargeFileWindow(EditorDocument& document, std::uint64_t offset,
                             std::optional<std::pair<std::uint64_t, std::size_t>> selection = {}) {
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

    void MoveLargeFileWindow(bool forward) {
        auto* document = ActiveDocument();
        if (!document || !document->large_buffer) {
            status_.SetText(L"The active document is not using mapped large-file mode"); return;
        }
        const auto size = document->large_buffer->Size();
        const auto step = static_cast<std::uint64_t>(document->mapped_window_size);
        const auto offset = forward
            ? (std::min)(size, document->mapped_offset + step)
            : (document->mapped_offset > step ? document->mapped_offset - step : 0);
        if (offset == document->mapped_offset || offset >= size) {
            status_.SetText(forward ? L"End of large file" : L"Start of large file"); return;
        }
        const auto window = document->large_buffer->ReadTextWindow(
            offset, document->mapped_window_size, document->detected_encoding,
            document->ansi_code_page);
        if (!window) { status_.SetText(L"Window boundary contains invalid text"); return; }
        document->loading_large_window = true;
        document->editor->SetReadOnly(false);
        const bool replaced = document->editor->SetText(window->text);
        document->editor->SetReadOnly(document->read_only);
        document->loading_large_window = false;
        if (!replaced) return;
        document->mapped_offset = offset;
        document->mapped_decoded_offset = window->decoded_offset;
        if (!document->large_buffer->IsModified()) document->editor->SetSavePoint();
        status_.SetText(L"Large file | bytes " + std::to_wstring(document->mapped_offset) + L"–" +
            std::to_wstring(window->byte_end) + L" of " +
            std::to_wstring(size));
    }

    std::optional<mwfl::ScintillaTextRange> FindNext() {
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
        status_.SetText(match ? L"Match selected" :
            (document->large_buffer && search_regex_ ?
                L"No match in this window; full-file regular expressions are disabled for large files"
                : L"No matches"));
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
        status_.SetText(L"Replaced " + std::to_wstring(count) + L" occurrence(s)" +
            (ActiveDocument() && ActiveDocument()->large_buffer ? L" in the loaded large-file window" : L""));
    }

    void ShowEncodingInformation() {
        const auto* document = ActiveDocument();
        if (!document) return;
        std::wstring name = L"UTF-8";
        switch (document->detected_encoding) {
        case notepad_colon::EncodingKind::utf8_bom: name = L"UTF-8 with BOM"; break;
        case notepad_colon::EncodingKind::utf16_le: name = L"UTF-16 LE"; break;
        case notepad_colon::EncodingKind::utf16_be: name = L"UTF-16 BE"; break;
        case notepad_colon::EncodingKind::ansi: name = L"ANSI code page " + std::to_wstring(document->ansi_code_page); break;
        case notepad_colon::EncodingKind::binary: name = L"Binary"; break;
        case notepad_colon::EncodingKind::utf8: break;
        }
        const auto& analysis = document->encoding_analysis;
        const auto message = L"Encoding: " + name +
            L"\nInvalid byte sequences: " + std::to_wstring(analysis.invalid_byte_offsets.size()) +
            L"\nLine endings: " + std::to_wstring(analysis.eol.crlf) + L" CRLF, " +
            std::to_wstring(analysis.eol.lf) + L" LF, " + std::to_wstring(analysis.eol.cr) + L" CR" +
            L"\nMixed line endings: " + (analysis.eol.Mixed() ? std::wstring(L"Yes") : std::wstring(L"No")) +
            L"\nBidi / zero-width controls: " + std::to_wstring(analysis.unicode_risks.size());
        ::MessageBoxW(GetHwnd(), message.c_str(), L"Document Encoding Information", MB_OK |
            ((!analysis.invalid_byte_offsets.empty() || analysis.eol.Mixed() || !analysis.unicode_risks.empty())
                ? MB_ICONWARNING : MB_ICONINFORMATION));
    }

    void ReopenActiveWithCodePage(unsigned int code_page) {
        auto* document = ActiveDocument();
        const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
        if (!document || !metadata || metadata->path.empty()) return;
        if (metadata->dirty && ::MessageBoxW(GetHwnd(),
            L"Reopening discards unsaved edits. Continue?", L"Reopen with Encoding",
            MB_YESNO | MB_ICONWARNING) != IDYES) return;
        const auto bytes = ReadFileBytes(metadata->path);
        if (!bytes) return;
        const auto decoded = notepad_colon::DecodeBytes(*bytes, notepad_colon::EncodingKind::ansi, code_page);
        if (!decoded) { status_.SetText(L"The selected code page cannot decode this file"); return; }
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
        workspace_.SetDirty(document->id, false); SyncPresentation(L"Reopened with code page " + std::to_wstring(code_page));
    }

    void SetAnsiEncoding(unsigned int code_page) {
        auto* document = ActiveDocument();
        if (!document) return;
        const auto text = document->editor->GetText();
        if (!text || !notepad_colon::EncodeText(*text, notepad_colon::EncodingKind::ansi, code_page)) {
            ::MessageBoxW(GetHwnd(), L"Some characters cannot be represented by this ANSI code page.",
                          L"Encoding Conversion", MB_OK | MB_ICONWARNING); return;
        }
        document->detected_encoding = notepad_colon::EncodingKind::ansi;
        document->ansi_code_page = code_page;
        workspace_.SetDirty(document->id, true);
        SyncPresentation(L"Will save using ANSI code page " + std::to_wstring(code_page));
    }

    void SetEncoding(mwfl::TextEncoding encoding) {
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

    std::optional<std::filesystem::path> SelectedWorkspacePath() const {
        const auto item = TreeView_GetSelection(tree_.GetHwnd());
        if (!item) return std::nullopt;
        TVITEMW details{};
        details.mask = TVIF_PARAM;
        details.hItem = item;
        if (!TreeView_GetItem(tree_.GetHwnd(), &details)) return std::nullopt;
        const auto found = tree_paths_.find(static_cast<std::uint64_t>(details.lParam));
        return found == tree_paths_.end() ? std::nullopt
                                         : std::optional<std::filesystem::path>{found->second};
    }

    std::optional<std::wstring> PromptWorkspaceName(std::wstring_view title,
                                                     std::wstring_view initial = {},
                                                     bool validate_name = true) {
        mwfl::TextBox name; mwfl::Button accept, cancel; std::optional<std::wstring> result;
        mwfl::Dialog* pointer = nullptr;
        mwfl::Dialog dialog({.owner = GetHwnd(), .title = std::wstring(title),
            .initial_client_size = {400.0_dip, 104.0_dip}, .resizable = false,
            .callbacks = {
                .initialize = [&](HWND window) {
                    mwfl::ControlHost ui{window}; ui.Add(name, {731}, std::wstring(initial));
                    ui.Add(accept, {IDOK}, L"OK"); ui.Add(cancel, {IDCANCEL}, L"Cancel");
                    mwfl::SetAccessibleName(name.GetHwnd(), L"Workspace item name");
                    return pointer->SetLayout(mwfl::Column().Margin(10.0_dip).Gap(8.0_dip)
                        .Add(name, mwfl::Fixed(28.0_dip))
                        .Add(mwfl::Row().Gap(6.0_dip).Add(mwfl::Column(), mwfl::Stretch())
                            .Add(accept, mwfl::Fixed(76.0_dip)).Add(cancel, mwfl::Fixed(76.0_dip)),
                            mwfl::Fixed(28.0_dip)));
                },
                .command = [&](HWND, WORD id, WORD) {
                    if (id != IDOK) return false;
                    const auto value = name.GetText();
                    if (validate_name && !notepad_colon::IsValidWorkspaceName(value)) {
                        ::MessageBoxW(pointer->GetHwnd(), L"Enter a valid Windows file or folder name.",
                                      L"Invalid name", MB_OK | MB_ICONWARNING);
                        return true;
                    }
                    result = value; pointer->Accept(); return true;
                }}});
        pointer = &dialog; static_cast<void>(dialog.ShowModal()); return result;
    }

    void RefreshWorkspace() {
        if (!workspace_catalog_.Roots().empty()) StartWorkspaceScan(workspace_catalog_.Roots().back());
    }

    void SaveWorkspaceCatalog() {
        if (!workspace_catalog_path_.empty())
            static_cast<void>(notepad_colon::SaveWorkspaceCatalogAtomic(
                workspace_catalog_path_, workspace_catalog_));
    }

    void ToggleFavoriteWorkspace() {
        if (workspace_root_.empty()) { status_.SetText(L"Open a workspace first"); return; }
        const bool favorite = !std::ranges::any_of(workspace_catalog_.Favorites(),
            [&](const auto& path) { return SamePath(path, workspace_root_); });
        workspace_catalog_.SetFavorite(workspace_root_, favorite); SaveWorkspaceCatalog();
        status_.SetText(favorite ? L"Workspace added to favorites" : L"Workspace removed from favorites");
    }

    void ShowWorkspaceManager() {
        std::vector<std::filesystem::path> paths;
        for (const auto& path : workspace_catalog_.Favorites()) paths.push_back(path);
        for (const auto& path : workspace_catalog_.Recent())
            if (!std::ranges::any_of(paths, [&](const auto& existing) { return SamePath(existing, path); }))
                paths.push_back(path);
        mwfl::ListBox list; mwfl::Button open, favorite, close; mwfl::Dialog* pointer = nullptr;
        mwfl::Dialog dialog({.owner = GetHwnd(), .title = L"Recent and Favorite Workspaces",
            .initial_client_size = {620.0_dip, 330.0_dip}, .resizable = true,
            .callbacks = {
                .initialize = [&](HWND window) {
                    mwfl::ControlHost ui{window}; ui.Add(list, {781}, {});
                    ui.Add(open, {782}, L"Open"); ui.Add(favorite, {783}, L"Favorite / Unfavorite");
                    ui.Add(close, {IDCANCEL}, L"Close");
                    for (const auto& path : paths) {
                        const bool starred = std::ranges::any_of(workspace_catalog_.Favorites(),
                            [&](const auto& item) { return SamePath(item, path); });
                        list.AddItem((starred ? L"★  " : L"   ") + path.wstring());
                    }
                    if (!paths.empty()) list.SetSelection(0);
                    return pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(6.0_dip)
                        .Add(list, mwfl::Stretch())
                        .Add(mwfl::Row().Gap(6.0_dip).Add(open, mwfl::Fixed(80.0_dip))
                            .Add(favorite, mwfl::Fixed(150.0_dip)).Add(mwfl::Column(), mwfl::Stretch())
                            .Add(close, mwfl::Fixed(80.0_dip)), mwfl::Fixed(28.0_dip)));
                },
                .command = [&](HWND, WORD id, WORD) {
                    const auto index = list.GetSelection();
                    if (index == LB_ERR || static_cast<std::size_t>(index) >= paths.size()) return false;
                    const auto path = paths[static_cast<std::size_t>(index)];
                    if (id == 782) {
                        if (std::filesystem::is_directory(path)) { pointer->Accept(); StartWorkspaceScan(path); }
                        else ::MessageBoxW(pointer->GetHwnd(), L"This workspace folder no longer exists.",
                                           L"Workspace", MB_OK | MB_ICONWARNING);
                        return true;
                    }
                    if (id == 783) {
                        const bool is_favorite = std::ranges::any_of(workspace_catalog_.Favorites(),
                            [&](const auto& item) { return SamePath(item, path); });
                        workspace_catalog_.SetFavorite(path, !is_favorite); SaveWorkspaceCatalog();
                        pointer->Accept(); return true;
                    }
                    return false;
                }}});
        pointer = &dialog; static_cast<void>(dialog.ShowModal());
    }

    void CreateWorkspaceSelection(bool directory) {
        auto selected = SelectedWorkspacePath();
        if (!selected) { status_.SetText(L"Select a workspace folder first"); return; }
        std::error_code error;
        auto parent = std::filesystem::is_directory(*selected, error) ? *selected : selected->parent_path();
        const auto name = PromptWorkspaceName(directory ? L"New Folder" : L"New File");
        if (!name) return;
        if (!notepad_colon::CreateWorkspaceItem(parent, *name, directory, workspace_catalog_.Roots())) {
            ::MessageBoxW(GetHwnd(), L"The item could not be created safely.", L"Workspace",
                          MB_OK | MB_ICONERROR); return;
        }
        RefreshWorkspace();
        if (!directory) static_cast<void>(OpenPath(parent / *name));
    }

    void RenameWorkspaceSelection() {
        const auto selected = SelectedWorkspacePath();
        if (!selected) return;
        const auto name = PromptWorkspaceName(L"Rename Workspace Item", selected->filename().wstring());
        if (!name) return;
        if (!notepad_colon::RenameWorkspaceItem(*selected, *name, workspace_catalog_.Roots()))
            ::MessageBoxW(GetHwnd(), L"Workspace roots cannot be renamed, and the new name must be unused.",
                          L"Rename", MB_OK | MB_ICONWARNING);
        else RefreshWorkspace();
    }

    void RecycleWorkspaceSelection() {
        const auto selected = SelectedWorkspacePath();
        if (!selected) return;
        const auto prompt = L"Move this item to the Recycle Bin?\n\n" + selected->wstring();
        if (::MessageBoxW(GetHwnd(), prompt.c_str(), L"Workspace", MB_YESNO | MB_ICONWARNING) != IDYES) return;
        if (!notepad_colon::RecycleWorkspaceItem(*selected, workspace_catalog_.Roots()))
            ::MessageBoxW(GetHwnd(), L"Workspace roots cannot be deleted, or the operation failed.",
                          L"Workspace", MB_OK | MB_ICONERROR);
        else RefreshWorkspace();
    }

    void RevealWorkspaceSelection() {
        const auto selected = SelectedWorkspacePath();
        if (!selected) return;
        const auto parameters = L"/select,\"" + selected->wstring() + L"\"";
        ::ShellExecuteW(GetHwnd(), L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
    }

    void CopyWorkspacePath(bool relative) {
        const auto selected = SelectedWorkspacePath();
        if (!selected) return;
        auto value = *selected;
        if (relative) for (const auto& root : workspace_catalog_.Roots())
            if (notepad_colon::IsWithinWorkspaceRoots(*selected, {root})) {
                value = selected->lexically_relative(root); break;
            }
        status_.SetText(mwfl::SetClipboardText(GetHwnd(), value.wstring())
            ? L"Path copied" : L"Could not access clipboard");
    }

    void RemoveWorkspaceRoot() {
        const auto selected = SelectedWorkspacePath();
        if (!selected || !std::ranges::any_of(workspace_catalog_.Roots(),
            [&](const auto& root) { return SamePath(root, *selected); })) {
            status_.SetText(L"Select a workspace root to remove"); return;
        }
        static_cast<void>(workspace_catalog_.RemoveRoot(*selected));
        if (workspace_catalog_.Roots().empty()) {
            TreeView_DeleteAllItems(tree_.GetHwnd()); tree_paths_.clear(); workspace_visible_ = false;
            ApplyCompactLayout();
        } else RefreshWorkspace();
        static_cast<void>(SaveSessionSnapshot());
        SaveWorkspaceCatalog();
    }

    void OpenFolderInteractive() {
        const auto selected = mwfl::ShowFolderDialog({GetHwnd(), L"Choose a workspace folder"});
        if (selected.accepted) StartWorkspaceScan(selected.path);
    }

    void StartWorkspaceScan(std::filesystem::path root) {
        ReapCompletedWorkers(workspace_workers_);
        for (auto& worker : workspace_workers_) worker.thread.request_stop();
        ++workspace_generation_;
        static_cast<void>(workspace_catalog_.AddRoot(root));
        SaveWorkspaceCatalog();
        workspace_root_ = std::move(root);
        workspace_scans_.clear();
        workspace_lazy_ = true;
        RenderLazyWorkspaceRoots();
        workspace_visible_ = true;
        ApplyCompactLayout();
        status_.SetText(L"Workspace opened (folders load on demand)");
    }

    void RenderLazyWorkspaceRoots() {
        TreeView_DeleteAllItems(tree_.GetHwnd());
        tree_paths_.clear();
        workspace_loaded_nodes_.clear();
        next_tree_id_ = 1;
        for (const auto& root : workspace_catalog_.Roots()) {
            const mwfl::TreeItemId id{next_tree_id_++};
            tree_.AddItem(id, root.filename().empty() ? root.wstring() : root.filename().wstring());
            tree_paths_[id.value] = root;
            LoadWorkspaceChildren(id);
            tree_.Expand(id);
        }
    }

    HTREEITEM FindTreeItem(mwfl::TreeItemId id, HTREEITEM item = nullptr) const {
        if (!item) item = TreeView_GetRoot(tree_.GetHwnd());
        while (item) {
            TVITEMW value{}; value.mask = TVIF_PARAM; value.hItem = item;
            if (TreeView_GetItem(tree_.GetHwnd(), &value) &&
                static_cast<std::uint64_t>(value.lParam) == id.value) return item;
            if (auto child = TreeView_GetChild(tree_.GetHwnd(), item))
                if (auto found = FindTreeItem(id, child)) return found;
            item = TreeView_GetNextSibling(tree_.GetHwnd(), item);
        }
        return nullptr;
    }

    void LoadWorkspaceChildren(mwfl::TreeItemId parent) {
        if (workspace_loaded_nodes_.contains(parent.value)) return;
        const auto found = tree_paths_.find(parent.value);
        if (found == tree_paths_.end()) return;
        if (auto handle = FindTreeItem(parent)) {
            while (auto child = TreeView_GetChild(tree_.GetHwnd(), handle))
                TreeView_DeleteItem(tree_.GetHwnd(), child);
        }
        std::error_code error;
        if (!std::filesystem::is_directory(found->second, error) || error) return;
        struct Child { std::filesystem::path path; bool directory; };
        std::vector<Child> children;
        for (std::filesystem::directory_iterator iterator(found->second,
                 std::filesystem::directory_options::skip_permission_denied, error), end;
             iterator != end && !error && children.size() < 2000; iterator.increment(error)) {
            const bool directory = iterator->is_directory(error);
            if (error) { error.clear(); continue; }
            if (directory && notepad_colon::IsExcludedWorkspaceDirectory(
                    iterator->path().filename().wstring())) continue;
            children.push_back({iterator->path(), directory});
        }
        std::ranges::sort(children, [](const auto& left, const auto& right) {
            if (left.directory != right.directory) return left.directory;
            return ::CompareStringOrdinal(left.path.filename().c_str(), -1,
                right.path.filename().c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
        auto filter = workspace_filter_.GetText();
        std::ranges::transform(filter, filter.begin(), ::towlower);
        for (const auto& child : children) {
            auto name = child.path.filename().wstring();
            auto folded = name; std::ranges::transform(folded, folded.begin(), ::towlower);
            if (!child.directory && !filter.empty() && folded.find(filter) == std::wstring::npos)
                continue;
            const mwfl::TreeItemId id{next_tree_id_++};
            tree_.AddChild(id, name, parent);
            tree_paths_[id.value] = child.path;
            if (child.directory) {
                const mwfl::TreeItemId placeholder{next_tree_id_++};
                tree_.AddChild(placeholder, L"…", id);
            }
        }
        workspace_loaded_nodes_.insert(parent.value);
    }

    void CompleteWorkspaceScan() {
        std::vector<std::pair<std::filesystem::path, notepad_colon::WorkspaceScan>> scans;
        {
            std::scoped_lock lock{worker_mutex_};
            if (!pending_workspace_scans_ ||
                pending_workspace_scans_->first != workspace_generation_.load()) return;
            scans = std::move(pending_workspace_scans_->second);
            pending_workspace_scans_.reset();
        }
        if (scans.empty()) return;
        workspace_lazy_ = false;
        workspace_scans_ = std::move(scans);
        RenderWorkspaceTree();
        std::size_t entries = 0; bool truncated = false;
        for (const auto& [root, scan] : workspace_scans_) {
            static_cast<void>(root); entries += scan.entries.size(); truncated = truncated || scan.truncated;
        }
        workspace_visible_ = true;
        ApplyCompactLayout();
        status_.SetText(L"Workspace: " + std::to_wstring(workspace_scans_.size()) + L" root(s) | " +
            std::to_wstring(entries) + L" entries" + (truncated ? L" (truncated)" : L""));
    }

    void RenderWorkspaceTree() {
        TreeView_DeleteAllItems(tree_.GetHwnd());
        tree_paths_.clear();
        std::uint64_t next = 1;
        auto filter = workspace_filter_.GetText();
        std::ranges::transform(filter, filter.begin(), [](wchar_t value) {
            return static_cast<wchar_t>(std::towlower(value));
        });
        for (const auto& [root, scan] : workspace_scans_) {
            const mwfl::TreeItemId root_id{next++};
            const auto label = root.filename().empty() ? root.wstring() : root.filename().wstring();
            tree_.AddItem(root_id, label);
            tree_paths_[root_id.value] = root;
            std::unordered_map<std::wstring, mwfl::TreeItemId> directory_ids;
            directory_ids[L""] = root_id;
            for (const auto& entry : scan.entries) {
                if (!entry.directory && !filter.empty() &&
                    entry.search_key.find(filter) == std::wstring::npos) continue;
                const mwfl::TreeItemId id{next++};
                const auto parent_text = entry.relative_path.parent_path().wstring();
                const auto parent = directory_ids.contains(parent_text) ? directory_ids[parent_text] : root_id;
                tree_.AddChild(id, entry.relative_path.filename().wstring(), parent);
                tree_paths_[id.value] = root / entry.relative_path;
                if (entry.directory) directory_ids[entry.relative_path.wstring()] = id;
            }
            tree_.Expand(root_id);
        }
    }

    bool PromptFolderSearch(std::wstring& query, bool& open_documents_only) {
        mwfl::Label query_label, include_label, exclude_label;
        mwfl::TextBox query_box, include_box, exclude_box;
        mwfl::CheckBox open_only;
        mwfl::Button search_button, cancel;
        bool accepted = false;
        mwfl::Dialog* pointer = nullptr;
        mwfl::Dialog dialog({.owner = GetHwnd(), .title = L"Find in Files",
            .initial_client_size = {560.0_dip, 270.0_dip}, .resizable = false,
            .callbacks = {.initialize = [&](HWND window) {
                mwfl::ControlHost ui{window};
                ui.Add(query_label, {750}, L"Find"); ui.Add(query_box, {751}, query);
                ui.Add(include_label, {752}, L"Include files (semicolon-separated globs)");
                ui.Add(include_box, {753}, search_include_globs_);
                ui.Add(exclude_label, {754}, L"Exclude files (semicolon-separated globs)");
                ui.Add(exclude_box, {755}, search_exclude_globs_);
                ui.Add(open_only, {756}, L"Search open documents only");
                open_only.SetChecked(open_documents_only);
                ui.Add(search_button, {IDOK}, L"Search"); ui.Add(cancel, {IDCANCEL}, L"Cancel");
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

    void StartFolderSearch() {
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
        const auto generation = ++search_generation_;
        if (auto* command = commands_.Find(kCancelSearch)) command->SetEnabled(true);
        menu_.UpdateCommand(*commands_.Find(kCancelSearch));
        status_.SetText(L"Searching workspace...");
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
                    {
                        std::scoped_lock lock{worker_mutex_};
                        if (generation != search_generation_.load()) { done->store(true); return; }
                        pending_search_result_ = {generation, std::move(result)};
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
            {
                std::scoped_lock lock{worker_mutex_};
                if (generation != search_generation_.load()) { done->store(true); return; }
                pending_search_result_ = {generation, std::move(result)};
            }
            ::PostMessageW(window, kSearchCompleteMessage,
                           static_cast<WPARAM>(generation), 0);
            done->store(true);
        })});
    }

    void CancelFolderSearch() {
        for (auto& worker : search_workers_) worker.thread.request_stop();
    }

    void CompleteSearch() {
        std::optional<notepad_colon::SearchResult> result;
        {
            std::scoped_lock lock{worker_mutex_};
            if (!pending_search_result_ ||
                pending_search_result_->first != search_generation_.load()) return;
            result = std::move(pending_search_result_->second);
            pending_search_result_.reset();
        }
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
            (search_results_.cancelled ? L"Search cancelled | " : L"Search complete | ") +
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
                    status_.SetText(L"Follow mode loaded appended data");
                    continue;
                }
                document.follow_tail = false;
                status_.SetText(L"Follow mode stopped because the file could not be reopened");
            }
            if (!current.exists && !IsTestMode()) {
                const auto answer = ::MessageBoxW(GetHwnd(),
                    (metadata->title + L" was deleted outside Notepad::.\n\n"
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
        for (auto& worker : search_workers_) worker.thread.request_stop();
        for (auto& worker : workspace_workers_) worker.thread.request_stop();
        search_workers_.clear();
        workspace_workers_.clear();
    }

    static void ReapCompletedWorkers(std::vector<BackgroundTask>& workers) {
        std::erase_if(workers, [](const auto& worker) { return worker.done->load(); });
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

    std::optional<notepad_colon::Session> CaptureSession() {
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

    bool SaveSessionSnapshot() {
        if (session_path_.empty() || restoring_session_) return true;
        const auto session = CaptureSession();
        if (!session) return false;
        session_writer_.Queue(session_path_, std::move(*session));
        session_snapshot_pending_ = false;
        return true;
    }

    void QueueSessionSnapshotIfDue() {
        if (!session_snapshot_pending_ || restoring_session_ ||
            std::chrono::steady_clock::now() < session_snapshot_due_) return;
        static_cast<void>(SaveSessionSnapshot());
    }

    void SaveNamedSession() {
        const auto selected = mwfl::ShowSaveFileDialog({.owner = GetHwnd(), .title = L"Save Named Session",
            .filters = {{L"Notepad:: session", L"*.npcsession"}},
            .default_extension = L"npcsession"});
        if (!selected.accepted) return;
        const auto session = CaptureSession();
        status_.SetText(session && notepad_colon::SaveSessionAtomic(selected.path, *session)
            ? L"Named session saved" : L"Named session could not be saved");
    }

    void OpenNamedSession() {
        const auto selected = mwfl::ShowOpenFileDialog({.owner = GetHwnd(), .title = L"Open Named Session",
            .filters = {{L"Notepad:: session", L"*.npcsession"}}});
        if (!selected.accepted) return;
        notepad_colon::Session session;
        if (!notepad_colon::LoadSession(selected.path, session)) {
            ::MessageBoxW(GetHwnd(), L"This session file is invalid or unreadable.", L"Open Session",
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
            } else if (!entry.recovery_text.empty()) NewDocument(entry.recovery_text, L"Recovered session note");
        }
        status_.SetText(L"Named session opened");
    }

    bool RestoreSession() {
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
        SetTitle((metadata ? metadata->title : L"Notepad::") +
                 std::wstring(metadata && metadata->dirty ? L" * — Notepad::" : L" — Notepad::"));
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
        if (large_file_self_test_) {
            RunLargeFileSelfTest();
            return;
        }
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
                    *first->editor, lexilla_, notepad_colon::Language::cpp, true)) result = 11;
            if (result == 0 && !first->editor->SetText(
                    L"int value = 42; // syntax colour\n")) result = 24;
            if (result == 0) {
                first->editor->Send(SCI_COLOURISE, 0, -1);
                if (first->editor->Send(SCI_GETSTYLEAT, 0) != 5 ||
                    first->editor->Send(SCI_STYLEGETFORE, 5) != RGB(86, 156, 214)) result = 24;
            }
            if (result == 0) {
                constexpr std::string_view source = "int value = 42; return value; // syntax colour\n";
                notepad_colon::TreeSitterDocument syntax;
                if (!first->editor->SetText(L"int value = 42; return value; // syntax colour\n") ||
                    !syntax.ConfigureCpp() || !syntax.Parse(source)) result = 26;
                else {
                    notepad_colon::ConfigureTreeSitterStyles(*first->editor, true);
                    notepad_colon::ApplyTreeSitterHighlights(
                        *first->editor, syntax, 0, static_cast<std::uint32_t>(source.size()));
                    const auto return_position = source.find("return");
                    if (first->editor->Send(SCI_GETSTYLEAT, return_position) !=
                            40 + static_cast<int>(notepad_colon::SyntaxKind::keyword) ||
                        first->editor->Send(SCI_STYLEGETFORE,
                            40 + static_cast<int>(notepad_colon::SyntaxKind::keyword)) !=
                            RGB(86, 156, 214)) result = 26;
                }
            }
            if (result == 0) {
                const auto language_directory = LanguageDirectory(true);
                std::error_code ignored;
                std::filesystem::remove_all(language_directory, ignored);
                std::filesystem::create_directories(language_directory, ignored);
                const auto fixture = ExecutablePath().parent_path() / L"tree-sitter-json-test.wasm";
                std::filesystem::copy_file(fixture, language_directory / L"tree-sitter-json.wasm",
                    std::filesystem::copy_options::overwrite_existing, ignored);
                std::ofstream(language_directory / L"json-highlights.scm", std::ios::binary) <<
                    "(string) @string\n(number) @number\n(pair key: (string) @property)\n";
                std::ofstream(language_directory / L"wasm-test.json", std::ios::binary) <<
                    R"({"id":"wasm-self-test","name":"Wasm Self Test","extensions":[".wtest"],"fallbackLexer":"json","treeSitter":{"grammar":"wasm","language":"json","module":"tree-sitter-json.wasm","highlights":"json-highlights.scm"}})";
                LoadLanguageDefinitions();
                first->language = notepad_colon::Language::plain_text;
                first->language_id = "wasm-self-test";
                constexpr std::string_view json = R"({"name":"colon","count":42})";
                if (ignored || !first->editor->SetText(L"{\"name\":\"colon\",\"count\":42}") ) result = 27;
                else {
                    InitializeSyntaxTree(*first);
                    const auto property = json.find("\"name\"");
                    if (!first->wasm_syntax) result = 27;
                    else {
                        const auto spans = first->wasm_syntax->Highlights(
                            0, static_cast<std::uint32_t>(json.size()));
                        notepad_colon::ApplySyntaxSpans(*first->editor, spans, 0,
                            static_cast<std::uint32_t>(json.size()));
                        if (first->editor->Send(SCI_GETSTYLEAT, property) !=
                            40 + static_cast<int>(notepad_colon::SyntaxKind::property)) result = 28;
                    }
                }
                first->wasm_syntax.reset();
                std::filesystem::remove_all(language_directory, ignored);
                language_registry_.ResetBuiltins();
            }
            if (result == 0) {
                for (const auto language : notepad_colon::AllLanguages()) {
                    if (!notepad_colon::ConfigureLanguage(
                            *first->editor, lexilla_, language, false)) {
                        result = 25;
                        break;
                    }
                }
                static_cast<void>(notepad_colon::ConfigureLanguage(
                    *first->editor, lexilla_, notepad_colon::Language::cpp, IsDark()));
            }
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
                !session_writer_.Flush() ||
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
            static_cast<void>(workspace_catalog_.AddRoot(test_workspace));
            StartWorkspaceScan(test_workspace);
            if (result == 0 && TreeView_GetCount(tree_.GetHwnd()) != 3) result = 29;
            {
                std::scoped_lock lock{worker_mutex_};
                pending_workspace_scans_ = std::pair{workspace_generation_.load(),
                    std::vector<std::pair<std::filesystem::path, notepad_colon::WorkspaceScan>>{
                        {test_workspace, notepad_colon::ScanWorkspace(test_workspace)}}};
            }
            CompleteWorkspaceScan();
            if (result == 0 && TreeView_GetCount(tree_.GetHwnd()) < 3) result = 17;
            {
                std::scoped_lock lock{worker_mutex_};
                pending_search_result_ = std::pair{search_generation_.load(),
                    notepad_colon::SearchWorkspace(test_workspace, L"needle")};
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
            std::filesystem::remove(workspace_catalog_path_, ignored);
        }
        adapter_.Detach();
        ::PostQuitMessage(result);
    }

    void RunLargeFileSelfTest() noexcept {
        int result = 0;
        constexpr std::string_view marker = "/*NPC-LARGE-EDIT*/";
        auto* document = ActiveDocument();
        const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
        if (!document || !metadata || !document->large_buffer || document->read_only ||
            document->detected_encoding != notepad_colon::EncodingKind::utf8) result = 40;
        if (result == 0) {
            document->editor->Send(SCI_INSERTTEXT, 0, reinterpret_cast<LPARAM>(marker.data()));
            if (!document->large_buffer->IsModified() ||
                document->large_buffer->Size() != document->file_state.size + marker.size())
                result = 41;
        }
        if (result == 0 && !SaveDocument(*document, false)) result = 42;
        if (result == 0) {
            const auto prefix = document->large_buffer->Read(0, marker.size());
            if (std::string(prefix.begin(), prefix.end()) != marker ||
                document->large_buffer->IsModified() || workspace_.Find(document->id)->dirty)
                result = 43;
        }
        StopWorkers();
        ::PostQuitMessage(result);
    }

    mwfl::ScintillaRuntime runtime_;
    notepad_colon::LexillaRuntime lexilla_;
    notepad_colon::LanguageRegistry language_registry_;
    mwfl::DocumentWorkspaceModel workspace_;
    mwfl::DocumentTabWorkspaceAdapter adapter_;
    std::vector<EditorDocument> documents_;
    std::vector<mwfl::DocumentId> pinned_documents_;
    std::uint64_t next_id_ = 1;
    mwfl::CommandSet commands_;
    mwfl::AcceleratorTable accelerators_;
    mwfl::Menu menu_;
    mwfl::ImageList toolbar_images_;
    mwfl::Toolbar toolbar_;
    mwfl::TextBox search_, replacement_, workspace_filter_;
    mwfl::TreeView tree_;
    mwfl::TabControl tabs_;
    mwfl::ListView results_;
    mwfl::StatusBar status_;
    std::filesystem::path session_path_;
    notepad_colon::SessionWriter session_writer_;
    bool session_snapshot_pending_ = false;
    std::chrono::steady_clock::time_point session_snapshot_due_{};
    bool restoring_session_ = false;
    bool find_bar_visible_ = false;
    bool search_match_case_ = false;
    bool search_whole_word_ = false;
    bool search_regex_ = false;
    std::wstring search_include_globs_{L"*"};
    std::wstring search_exclude_globs_{L"*.min.js;*.map"};
    bool search_selection_ = false;
    std::optional<mwfl::ScintillaTextRange> search_scope_;
    std::vector<std::wstring> search_history_;
    bool workspace_visible_ = false;
    bool results_visible_ = false;
    mwfl::RecentFileList recent_{10};
    std::filesystem::path workspace_root_;
    notepad_colon::WorkspaceCatalog workspace_catalog_;
    std::unordered_map<std::uint64_t, std::filesystem::path> tree_paths_;
    std::unordered_set<std::uint64_t> workspace_loaded_nodes_;
    std::uint64_t next_tree_id_ = 1;
    bool workspace_lazy_ = false;
    std::vector<std::pair<std::filesystem::path, notepad_colon::WorkspaceScan>> workspace_scans_;
    notepad_colon::SearchResult search_results_;
    std::vector<BackgroundTask> search_workers_;
    std::vector<BackgroundTask> workspace_workers_;
    std::atomic<std::uint64_t> search_generation_{0};
    std::atomic<std::uint64_t> workspace_generation_{0};
    std::mutex worker_mutex_;
    std::optional<std::pair<std::uint64_t, notepad_colon::SearchResult>> pending_search_result_;
    std::optional<std::pair<std::uint64_t,
        std::vector<std::pair<std::filesystem::path, notepad_colon::WorkspaceScan>>>>
        pending_workspace_scans_;
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
    std::filesystem::path workspace_catalog_path_;
    std::vector<notepad_colon::ShortcutBinding> default_shortcuts_;
    std::vector<mwfl::ControlId> recent_commands_;
    bool playing_macro_ = false;
    mwfl::PrinterSettings printer_settings_;
    PrintOptions print_options_;
    mwfl::SingleInstance& instance_;
    std::vector<std::filesystem::path> startup_paths_;
    bool self_test_ = false;
    bool activation_test_server_ = false;
    bool large_file_self_test_ = false;
    bool chinese_ui_ = false;
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
    bool large_file_self_test = false;
    bool activation_test_server = false;
    bool activation_test_client = false;
    std::vector<std::filesystem::path> paths;
    for (int index = 1; arguments && index < count; ++index) {
        const std::wstring_view argument{arguments[index]};
        if (argument == L"--self-test") self_test = true;
        else if (argument == L"--large-file-self-test") large_file_self_test = true;
        else if (argument == L"--activation-test-server") activation_test_server = true;
        else if (argument == L"--activation-test-client") activation_test_client = true;
        else if (!argument.starts_with(L"--")) {
            std::error_code error;
            auto absolute = std::filesystem::absolute(argument, error);
            paths.push_back(error ? std::filesystem::path{argument} : std::move(absolute));
        }
    }
    if (arguments) ::LocalFree(arguments);
    const std::wstring instance_id = (self_test || large_file_self_test)
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
            ::MessageBoxW(nullptr, L"The existing Notepad:: instance did not respond.",
                          L"Notepad::", MB_OK | MB_ICONERROR);
            return 2;
        }
        return 0;
    }
    if (activation_test_client) return 3;
    return mwfl::RunApplication<MainWindow>(instance,
        (self_test || large_file_self_test) ? SW_HIDE : show_command,
        {.title = L"Notepad::", .initial_bounds = {{}, {900.0_dip, 650.0_dip}},
         .use_default_bounds = false}, {.com_apartment = mwfl::ComApartment::sta},
         single_instance, std::move(paths), self_test, activation_test_server,
         large_file_self_test);
}
