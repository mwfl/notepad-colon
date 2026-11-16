#pragma once

#include <mwfl/mwfl.h>
#include <mwfl/scintilla.h>
#include <mwfl/file_association.h>
#include <mwfl/shell_integration.h>
#include <mwfl/settings_store.h>
#include <mwfl/printing_settings.h>
#include <mwfl/single_instance.h>
#include <mwfl/dialog.h>
#include <commdlg.h>
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
#include <notepad_colon/encoding_detection.h>
#include <notepad_colon/preferences.h>
#include <notepad_colon/recovery.h>
#include <notepad_colon/recovery_writer.h>
#include <notepad_colon/text.h>
#include <notepad_colon/session.h>
#include <notepad_colon/session_writer.h>
#include <notepad_colon/shell_menu.h>
#include <notepad_colon/language.h>
#include <notepad_colon/language_registry.h>
#include <notepad_colon/localization.h>
#include <notepad_colon/latest_operation.h>
#include <notepad_colon/lightweight_completion.h>
#include <notepad_colon/line_tools.h>
#include <notepad_colon/workspace.h>
#include <notepad_colon/replace_plan.h>
#include <notepad_colon/workspace_state.h>
#include "../scintilla_support.h"
#include "../wasm_syntax_client.h"
#include "../../resource.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
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

namespace notepad_colon::app {

using notepad_colon::Tr;

// Width of a push button that fits `caption` with normal padding. Automatic
// sizing leaves no padding at all, which crowds CJK captions in particular.
inline mwfl::LayoutLength ButtonWidth(std::wstring_view caption) noexcept {
    float width = 28.0f;
    for (const wchar_t character : caption)
        if (character != L'&') width += character >= 0x2E80 ? 15.0f : 7.5f;
    return mwfl::Fixed(mwfl::Dip((std::max)(width, 76.0f)));
}

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
constexpr mwfl::ControlId kSplitView{458};
constexpr mwfl::ControlId kRotateSplit{459};
constexpr mwfl::ControlId kFocusOtherView{460};
constexpr mwfl::ControlId kReplaceInFiles{461};
constexpr mwfl::ControlId kAddShellMenus{462};
constexpr mwfl::ControlId kRemoveShellMenus{463};
constexpr mwfl::ControlId kRainbowColumns{464};
constexpr mwfl::ControlId kFilterLines{465};
constexpr mwfl::ControlId kHighlightLines{466};
constexpr mwfl::ControlId kHighlightRules{467};
constexpr mwfl::ControlId kReviewChanges{468};
constexpr mwfl::ControlId kGoToSourceLine{469};
constexpr mwfl::ControlId kExtractColumn{470};
constexpr mwfl::ControlId kSortColumnAscending{471};
constexpr mwfl::ControlId kSortColumnDescending{472};
constexpr mwfl::ControlId kReplacementHistory{473};
// Margin marker on lines another program changed and nobody reviewed yet.
constexpr int kReviewMarker = 4;
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
constexpr mwfl::ControlId kReopenAutomatic{433};
constexpr mwfl::ControlId kReopenCodePageBase{900};
constexpr mwfl::ControlId kConvertCodePageBase{920};
constexpr mwfl::ControlId kSaveSystemAnsi{435};
constexpr mwfl::ControlId kPreviousLargeWindow{436};
constexpr mwfl::ControlId kNextLargeWindow{437};
constexpr mwfl::ControlId kEnglishUi{438};
constexpr mwfl::ControlId kChineseUi{439};
inline mwfl::ControlId UiLanguageCommandId(notepad_colon::UiLanguage language) noexcept {
    const auto value = static_cast<WORD>(language);
    return {static_cast<WORD>(value < 2 ? kEnglishUi.value + value : 472 + value)};
}
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
constexpr UINT kReplaceCompleteMessage = WM_APP + 0x243;
constexpr UINT kFilterCompleteMessage = WM_APP + 0x244;
constexpr UINT kExtractCompleteMessage = WM_APP + 0x245;
constexpr mwfl::TimerId kMonitorTimer{1};
constexpr mwfl::TimerId kActivationTestTimer{2};
constexpr mwfl::TimerId kWorkspaceFilterTimer{3};
constexpr mwfl::TimerId kIncrementalSearchTimer{4};
constexpr auto kSessionDebounce = std::chrono::milliseconds{1500};
constexpr std::wstring_view kSettingsKey = L"Software\\mwfl\\Notepad Colon";
constexpr mwfl::ControlId kSearch{130};
constexpr mwfl::ControlId kReplacement{131};

inline std::vector<std::wstring> SplitGlobs(std::wstring_view value) {
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

// Where the rows of a line-filter result came from.
struct FilterLink {
    mwfl::DocumentId source{};
    std::filesystem::path path;                                  // Empty for an unsaved source.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> lines;  // Line number, byte offset; ascending.
};

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
    // Visible-range decorations (rainbow columns, highlighted log lines).
    char delimiter = 0;            // Non-zero while rainbow columns are on.
    bool highlight_lines = false;
    std::uint64_t revision = 0;    // Bumped by every text change.
    struct Decoration {
        LRESULT first = -1, last = -1;
        std::uint64_t revision = 0;
        char delimiter = 0;
        bool highlight = false;
        int palette = -1;          // 0 light, 1 dark, -1 not configured.
        std::uint64_t rules = 0;
    };
    Decoration decorations[2];     // Primary view, split view.
    // Set on a line-filter result tab so a row can lead back to its source line.
    std::shared_ptr<const FilterLink> filter_link;
    // Text before the first external change that has not been reviewed yet.
    std::optional<std::wstring> review_base;
    std::shared_ptr<const notepad_colon::LargeFileBuffer> recovery_snapshot;
    std::uint64_t recovery_revision = UINT64_MAX;
    std::uint64_t history_revision = UINT64_MAX;
    bool legacy_encoding_override = false;
    bool unicode_bom = true;
    std::optional<notepad_colon::LineEnding> line_ending_override;
    // Second view onto the same Scintilla document (declared last so it is
    // destroyed before the primary editor that owns the document).
    std::unique_ptr<mwfl::ScintillaEditor> split_editor;
    bool split_side_by_side = true;
};

// A Replace in Files request travels through two background phases: a
// frozen preview with recovery snapshots, then the selected guarded writes.
struct PendingReplace {
    std::wstring query;
    std::wstring replacement;
    notepad_colon::ReplaceOptions options;
    bool applying = false;
    bool running = false;
    bool restoring = false;
    bool cancel_requested = false;
    notepad_colon::ReplacePlan plan;
    std::vector<notepad_colon::ReplaceBufferSnapshot> buffers;
    std::size_t open_documents_changed = 0;
    std::size_t open_replacements = 0;
};

struct BackgroundTask {
    std::shared_ptr<std::atomic_bool> done;
    std::jthread thread;
};

inline std::wstring_view EncodingName(mwfl::TextEncoding encoding) noexcept {
    switch (encoding) {
    case mwfl::TextEncoding::utf8: return L"UTF-8";
    case mwfl::TextEncoding::utf8_bom: return L"UTF-8 BOM";
    case mwfl::TextEncoding::utf16_le: return L"UTF-16 LE";
    case mwfl::TextEncoding::utf16_be: return L"UTF-16 BE";
    }
    return L"Unknown";
}

struct EditorDocument;
// Status-bar name of the encoding the document will be saved with.
std::wstring DocumentEncodingName(const EditorDocument& document);

inline std::wstring_view LineEndingName(notepad_colon::LineEnding ending) noexcept {
    switch (ending) {
    case notepad_colon::LineEnding::crlf: return L"CRLF";
    case notepad_colon::LineEnding::lf: return L"LF";
    case notepad_colon::LineEnding::cr: return L"CR";
    }
    return L"Unknown";
}

inline notepad_colon::Encoding ToSessionEncoding(mwfl::TextEncoding encoding) noexcept {
    switch (encoding) {
    case mwfl::TextEncoding::utf8: return notepad_colon::Encoding::utf8;
    case mwfl::TextEncoding::utf8_bom: return notepad_colon::Encoding::utf8_bom;
    case mwfl::TextEncoding::utf16_le: return notepad_colon::Encoding::utf16_le;
    case mwfl::TextEncoding::utf16_be: return notepad_colon::Encoding::utf16_be;
    }
    return notepad_colon::Encoding::utf8;
}

inline mwfl::TextEncoding FromSessionEncoding(notepad_colon::Encoding encoding) noexcept {
    switch (encoding) {
    case notepad_colon::Encoding::utf8: return mwfl::TextEncoding::utf8;
    case notepad_colon::Encoding::utf8_bom: return mwfl::TextEncoding::utf8_bom;
    case notepad_colon::Encoding::utf16_le: return mwfl::TextEncoding::utf16_le;
    case notepad_colon::Encoding::utf16_be: return mwfl::TextEncoding::utf16_be;
    case notepad_colon::Encoding::ansi: return mwfl::TextEncoding::utf8;
    }
    return mwfl::TextEncoding::utf8;
}

inline bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) noexcept {
    const auto a = left.lexically_normal().native();
    const auto b = right.lexically_normal().native();
    return ::CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                  b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

inline std::optional<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path) {
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

inline std::optional<mwfl::FileStamp> QueryFileStamp(const std::filesystem::path& path) noexcept {
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

inline mwfl::TextEncoding ToMwflEncoding(notepad_colon::EncodingKind encoding) noexcept {
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

inline LRESULT CALLBACK CompareEditorSubclass(HWND window, UINT message, WPARAM wparam,
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

inline bool EditComparisonLine(mwfl::ScintillaEditor& editor, std::size_t one_based_line,
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

inline std::filesystem::path ExecutablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(),
                                              static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return path;
}

inline std::filesystem::path CommandProcessorPath() {
    std::wstring executable(32768, L'\0');
    const DWORD length = ::GetEnvironmentVariableW(
        L"ComSpec", executable.data(), static_cast<DWORD>(executable.size()));
    if (length != 0 && length < executable.size()) {
        executable.resize(length);
        return executable;
    }
    executable.resize(MAX_PATH);
    const UINT system_length = ::GetSystemDirectoryW(
        executable.data(), static_cast<UINT>(executable.size()));
    if (system_length == 0 || system_length >= executable.size()) return {};
    executable.resize(system_length);
    return std::filesystem::path{executable} / L"cmd.exe";
}

inline mwfl::FileAssociationSpec TextAssociation() {
    return {.extension = L".txt",
            .prog_id = L"mwfl.notepad-colon.text",
            .owner_id = L"mwfl.notepad-colon",
            .display_name = L"Text document",
            .executable = ExecutablePath(),
            .icon = ExecutablePath(),
            .verbs = {{L"open", L"Open with Notepad::", {}}}};
}

// Windows does not expose a documented switch that lets classic HMENU bars and
// popup menus follow app dark mode. Current Windows 10/11 builds expose this
// best-effort opt-in through uxtheme; keep the native light-theme fallback when
// the entry points are unavailable.
enum class PreferredAppMode : int { default_mode, allow_dark };

inline void EnableNativeDarkMenuSupport() noexcept {
    const HMODULE theme = ::LoadLibraryExW(
        L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!theme) return;
    using SetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode);
    const auto set_mode = reinterpret_cast<SetPreferredAppMode>(
        ::GetProcAddress(theme, MAKEINTRESOURCEA(135)));
    if (set_mode) static_cast<void>(set_mode(PreferredAppMode::allow_dark));
}

inline void RefreshNativeMenuTheme() noexcept {
    const HMODULE theme = ::GetModuleHandleW(L"uxtheme.dll");
    if (!theme) return;
    using FlushMenuThemes = void(WINAPI*)();
    const auto flush = reinterpret_cast<FlushMenuThemes>(
        ::GetProcAddress(theme, MAKEINTRESOURCEA(136)));
    if (flush) flush();
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

    void BuildUI() override;

    mwfl::EventResult OnCommand(const mwfl::CommandEvent& event) override;

    mwfl::EventResult OnNotify(const mwfl::NotifyEvent& event) override;

    mwfl::EventResult OnResize(const mwfl::ResizeEvent&) override;

    mwfl::EventResult OnAppearanceChanged(const mwfl::AppearanceState& state);

    mwfl::EventResult OnMessage(const mwfl::WindowMessage& event) override;

    mwfl::EventResult OnTimer(mwfl::TimerId id) override;

    mwfl::EventResult OnClose() override;

private:
    bool IsSelfTest() const noexcept {
        return self_test_;
    }
    bool IsTestMode() const noexcept {
        return self_test_ || activation_test_server_ || large_file_self_test_;
    }

    void BuildCommands();

    void AddLanguageCommands();

    void ApplyUiLanguage();
    void RefreshShortcutLabels();
    static std::wstring CommandLabel(const mwfl::Command& command, bool keep_mnemonic);
    void RememberCommandTitles();

    void SetUiLanguage(notepad_colon::UiLanguage language);

    void BuildMenu();

    EditorDocument* FindDocument(mwfl::DocumentId id) noexcept;

    EditorDocument* FindByHwnd(HWND hwnd) noexcept;

    EditorDocument* ActiveDocument() noexcept;

    mwfl::ScintillaEditor* ActiveEditor() noexcept;

    void ApplyCompactLayout();

    template <typename TransformFunction>
    void Transform(TransformFunction&& transform) {
        if (auto* editor = ActiveEditor(); editor &&
            notepad_colon::TransformSelectionOrDocument(*editor,
                std::forward<TransformFunction>(transform)))
            status_.SetText(L"Text transformed");
    }

    void NewDocument(std::wstring text = {}, std::wstring title = {});

    bool OpenPath(const std::filesystem::path& path, unsigned int ansi_code_page = 0,
                  bool pin_legacy_encoding = true, bool recovery_copy = false,
                  std::optional<notepad_colon::EncodingKind> known_encoding = {});

    void OpenInteractive();

    bool SaveDocument(EditorDocument& document, bool choose_path);

    void ReportAssociation(bool install);
    void ReportShellMenus(bool install);
    void RemoveTestState() noexcept;
    bool OpenPathOrFolder(const std::filesystem::path& path);

    bool IsDark() const noexcept {
        return mwfl::ResolveAppearance({ToColorMode(preferences_.theme)}).IsDark();
    }

    static mwfl::ColorMode ToColorMode(notepad_colon::ThemePreference theme) noexcept;

    void ApplyAppearance();

    void ApplyEditorAppearance(bool dark);

    void ApplyNativeControlColors(const mwfl::AppearanceState& state) noexcept;

    void SetActiveLanguage(notepad_colon::Language language);

    static std::filesystem::path LanguageDirectory(bool test_mode);

    void LoadLanguageDefinitions();

    void ReloadLanguageDefinitions();

    void SetActiveRegisteredLanguage(std::string_view id);

    static std::optional<std::string> EditorUtf8(const mwfl::ScintillaEditor& editor) noexcept;

    void InitializeSyntaxTree(EditorDocument& document) noexcept;

    void StyleVisibleSyntax(EditorDocument& document) noexcept;
    void StyleVisibleSyntax(EditorDocument& document, mwfl::ScintillaEditor& view) noexcept;
    bool ConfigureViewLanguage(mwfl::ScintillaEditor& view, const EditorDocument& document, bool dark);
    void ToggleSplitView();
    bool OpenSplitView(EditorDocument& document);
    void CloseSplitView(EditorDocument& document);
    void RotateSplitView();
    void FocusOtherView();
    void ArrangeViews() noexcept;
    void DecorateVisibleLines(EditorDocument& document, mwfl::ScintillaEditor& view, int slot) noexcept;
    void ClearLineDecorations(EditorDocument& document) noexcept;
    void ApplyAutomaticLineTools(EditorDocument& document, const std::filesystem::path& path);
    void ToggleRainbowColumns();
    void ToggleHighlightLines();
    void ShowHighlightRules();
    void ShowFilterLines();
    void StartLineFilter(std::wstring query, notepad_colon::LineFilterOptions options);
    void CompleteLineFilter();
    std::wstring DelimitedFieldStatus(const EditorDocument& document) const;
    // Zero-based field of the caret's row, for the given delimiter.
    std::optional<std::size_t> CaretField(const EditorDocument& document, char delimiter) const;
    // The document's delimiter, detecting it when rainbow columns are off; 0 if none.
    char DocumentDelimiter(EditorDocument& document) const;
    void GoToFilterSource();
    void StartColumnExtract();
    void CompleteColumnExtract();
    void SortByColumn(bool descending);

    void UpdateLargeBuffer(EditorDocument& document, const SCNotification& notification) noexcept;

    void UpdateSyntaxTree(EditorDocument& document, const SCNotification& notification) noexcept;

    void LoadPreferences();

    bool SavePreferences();

    void ShowPreferences();

    bool SaveActive(bool choose_path);

    bool SaveAll();

    bool SaveLargeDocument(EditorDocument& document, const std::filesystem::path& path,
                           const mwfl::WorkspaceDocument& metadata);

    void SaveRecoverySnapshotsIfDue();

    void ShowRecoveryManager();

    void CompareWithFile();

    void CompareWithDisk();

    void ShowComparison(std::wstring right_title, std::wstring left_text,
                        std::wstring right_text);

    static bool IsMacroSafeCommand(mwfl::ControlId id) noexcept;

    void RememberCommand(mwfl::ControlId id);

    void ShowGoToLineColumn();

    void RefreshGitChangeMarkers();

    void OpenWorkspaceTerminal();

    void ShowQuickOpen();

    void ToggleActiveLineComment();

    void ShowLocalCompletion();

    void ShowDocumentSymbols();

    void ShowCommandPalette();

    void StartMacroRecording();

    void StopMacroRecording();

    void PlayMacro(const std::vector<notepad_colon::MacroAction>& actions, std::size_t repeats);

    void PlayLastMacro(std::size_t repeats) { PlayMacro(last_macro_, repeats); }

    void SaveLastMacro();

    void ShowMacroManager();

    std::vector<notepad_colon::ShortcutBinding> CaptureShortcuts() const;

    bool ApplyShortcuts(const std::vector<notepad_colon::ShortcutBinding>& bindings,
                        bool persist);

    bool SaveConfigurationFile(const std::filesystem::path& path) const;

    void ShowShortcutSettings();

    void ExportConfiguration();

    void ImportConfiguration();

    std::optional<std::wstring> ActiveSelectionText() const;

    void ShowDocumentStatistics();

    void ExportDocument(bool html);

    void ShowPageSetup();

    void ConfigurePrinter();

    std::vector<mwfl::PrintPage> PaginateForPrinter(mwfl::ScintillaEditor& editor, HDC dc,
                                                     mwfl::ScintillaPosition begin,
                                                     mwfl::ScintillaPosition end);

    void PrintActive();

    void ShowPrintPreview();

    void InsertDateTime();

    void AutoSaveIfDue();

    bool CloseActive(bool discard = false);

    bool IsPinned(mwfl::DocumentId id) const noexcept {
        return std::ranges::find(pinned_documents_, id) != pinned_documents_.end();
    }

    void TogglePinActive();

    void SortTabs();

    void CloseRelativeTabs(int direction);

    void OpenActiveInNewWindow();

    void ToggleSearchOption(mwfl::ControlId id, bool& value);

    void ToggleSearchSelection();

    mwfl::ScintillaSearchFlags SearchFlags() const noexcept;

    void IncrementalSearch();

    void RememberSearch(std::wstring_view query);

    void MarkAllSearchMatches();

    bool LoadLargeFileWindow(EditorDocument& document, std::uint64_t offset,
                             std::optional<std::pair<std::uint64_t, std::size_t>> selection = {});

    void MoveLargeFileWindow(bool forward);

    std::optional<mwfl::ScintillaTextRange> FindNext();

    void ReplaceNext();

    void ReplaceAll();

    void ShowEncodingInformation();

    void ReopenActiveWithCodePage(unsigned int code_page);
    bool ReloadDocumentFromDisk(EditorDocument& document, bool follow_tail = false,
                                 std::optional<unsigned int> force_code_page = {});

    void SetAnsiEncoding(unsigned int code_page);

    void SetEncoding(mwfl::TextEncoding encoding);

    void SetLineEnding(notepad_colon::LineEnding ending);

    void RefreshRecentCommands();

    void RememberPath(const std::filesystem::path& path);

    std::optional<std::filesystem::path> SelectedWorkspacePath() const;

    std::optional<std::wstring> PromptWorkspaceName(std::wstring_view title,
                                                     std::wstring_view initial = {},
                                                     bool validate_name = true);

    void RefreshWorkspace() {
        if (!workspace_catalog_.Roots().empty()) StartWorkspaceScan(workspace_catalog_.Roots().back());
    }

    void SaveWorkspaceCatalog();

    void ToggleFavoriteWorkspace();

    void ShowWorkspaceManager();

    void CreateWorkspaceSelection(bool directory);

    void RenameWorkspaceSelection();

    void RecycleWorkspaceSelection();

    void RevealWorkspaceSelection();

    void CopyWorkspacePath(bool relative);

    void RemoveWorkspaceRoot();

    void OpenFolderInteractive();

    void StartWorkspaceScan(std::filesystem::path root);

    void RenderLazyWorkspaceRoots();

    HTREEITEM FindTreeItem(mwfl::TreeItemId id, HTREEITEM item = nullptr) const;

    void LoadWorkspaceChildren(mwfl::TreeItemId parent);

    void CompleteWorkspaceScan();

    void RenderWorkspaceTree();

    bool PromptFolderSearch(std::wstring& query, bool& open_documents_only);

    void StartFolderSearch();

    void CancelFolderSearch();

    void CompleteSearch();
    void StartReplaceInFiles();
    bool PromptReplaceInFiles(std::wstring& query, std::wstring& replacement, bool& backups);
    void BeginReplaceInFiles(std::wstring query, std::wstring replacement, bool backups);
    void RunReplacePhase(bool apply);
    void CompleteReplace();
    bool ReviewReplacement(notepad_colon::ReplacePlan& plan, bool history = false);
    void ShowReplacementHistory();
    std::filesystem::path ReplacementHistoryPath() const;
    bool ReplacementOwnsPath(const std::filesystem::path& path) const;
    bool ValidateReplacementBuffers(notepad_colon::ReplacePlan& plan);
    void ApplyReplacementBuffers(notepad_colon::ReplacePlan& plan);
    void ShowReplaceOutcome(const notepad_colon::ReplaceResult& result);

    void CheckExternalChanges(bool force);
    // External change review (main_window_review.cpp).
    std::size_t PendingReviewCount() const noexcept;
    // Records `before` as the review baseline after a reload; returns the
    // number of changes now waiting for review.
    std::size_t NoteExternalReload(EditorDocument& document, std::wstring before);
    // Ends the review: hunks flagged in `rejected` go back to the old text.
    bool FinishReview(EditorDocument& document, const std::vector<bool>& rejected);
    void ClearReview(EditorDocument& document);
    void ReviewExternalChanges();
    void ShowChangeReview(EditorDocument& document);

    void StopWorkers();

    static void ReapCompletedWorkers(std::vector<BackgroundTask>& workers) {
        std::erase_if(workers, [](const auto& worker) { return worker.done->load(); });
    }

    void ResolveSessionPath();

    std::optional<notepad_colon::Session> CaptureSession();

    bool SaveSessionSnapshot();

    void QueueSessionSnapshotIfDue();

    void SaveNamedSession();

    void OpenNamedSession();

    bool RestoreSession();
    bool RestoreSessionEntry(const notepad_colon::SessionEntry& entry, bool preserve_existing = false);

    void SynchronizeTabs(bool focus);

    void SyncPresentation(std::wstring_view action);

    void RunSelfTest() noexcept;

    void RunLargeFileSelfTest() noexcept;
    void RunSustainedFileSelfTest(EditorDocument*& document, int& result);

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
    std::wstring presentation_title_;
    notepad_colon::SessionWriter session_writer_;
    notepad_colon::RecoveryWriter recovery_writer_;
    bool session_snapshot_pending_ = false;
    std::chrono::steady_clock::time_point session_snapshot_due_{};
    bool restoring_session_ = false;
    bool session_checkpoint_blocked_ = false;
    std::optional<std::wstring> session_restore_warning_;
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
    notepad_colon::LatestOperation<notepad_colon::SearchResult> search_operations_;
    notepad_colon::LatestOperation<notepad_colon::ReplacePlan> replace_operations_;
    PendingReplace pending_replace_;
    notepad_colon::LatestOperation<notepad_colon::LineFilterResult> filter_operations_;
    std::wstring filter_title_;
    bool filter_source_highlighted_ = false;
    mwfl::DocumentId filter_source_{};
    std::filesystem::path filter_source_path_;
    notepad_colon::LatestOperation<notepad_colon::ColumnExtractResult> extract_operations_;
    std::wstring extract_title_;
    std::size_t extract_column_ = 0;
    std::vector<notepad_colon::HighlightRule> highlight_rules_ = notepad_colon::DefaultHighlightRules();
    std::uint64_t highlight_rules_revision_ = 1;
    bool decorating_ = false;
    notepad_colon::LatestOperation<
        std::vector<std::pair<std::filesystem::path, notepad_colon::WorkspaceScan>>>
        workspace_operations_;
    mwfl::UiTimer monitor_timer_;
    std::chrono::steady_clock::time_point last_auto_save_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_recovery_snapshot_ =
        std::chrono::steady_clock::now() - std::chrono::seconds{30};
    std::shared_ptr<notepad_colon::RecoveryStore> recovery_store_;
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
    notepad_colon::UiLanguage ui_language_ = notepad_colon::UiLanguage::english;
    // English source text of every translatable command, keyed by command id.
    std::unordered_map<WORD, std::wstring> command_titles_;
    int activation_test_result_ = 4;
    notepad_colon::Preferences preferences_;
    mwfl::VersionedSettingsStore settings_{HKEY_CURRENT_USER,
                                            L"Software\\mwfl\\Notepad Colon\\Preferences", 1};
};

}  // namespace notepad_colon::app
