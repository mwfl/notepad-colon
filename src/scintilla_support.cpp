#include "scintilla_support.h"

#include <Scintilla.h>

#include <array>
#include <climits>
#include <limits>
#include <string>
#include <vector>

namespace notepad_colon {
namespace {
constexpr int kBookmarkMarker = 0;
// Lexilla style numbers are part of the pinned Lexilla 5.6.5 lexer contract.
constexpr int kCComment = 1, kCCommentLine = 2, kCCommentDoc = 3, kCNumber = 4,
              kCWord = 5, kCString = 6, kCCharacter = 7, kCPreprocessor = 9,
              kCStringEol = 12, kCWord2 = 16;
constexpr int kPCommentLine = 1, kPNumber = 2, kPString = 3, kPCharacter = 4,
              kPWord = 5, kPTriple = 6, kPTripleDouble = 7, kPCommentBlock = 12;
constexpr int kJsonNumber = 1, kJsonString = 2, kJsonPropertyName = 4, kJsonKeyword = 11;
constexpr int kSqlComment = 1, kSqlCommentLine = 2, kSqlNumber = 4,
              kSqlWord = 5, kSqlString = 6;

void SetProperty(mwfl::ScintillaEditor& editor, const char* name, const char* value) noexcept {
    editor.Send(SCI_SETPROPERTY, reinterpret_cast<WPARAM>(name), reinterpret_cast<LPARAM>(value));
}

void SetKeywords(mwfl::ScintillaEditor& editor, const char* words) noexcept {
    editor.Send(SCI_SETKEYWORDS, 0, reinterpret_cast<LPARAM>(words));
}

void Fore(mwfl::ScintillaEditor& editor, int style, COLORREF colour) noexcept {
    editor.Send(SCI_STYLESETFORE, style, colour);
}

void ConfigureCommonStyles(mwfl::ScintillaEditor& editor, Language language) noexcept {
    constexpr COLORREF blue = RGB(0, 92, 197);
    constexpr COLORREF green = RGB(0, 128, 0);
    constexpr COLORREF red = RGB(163, 21, 21);
    constexpr COLORREF purple = RGB(128, 0, 128);
    constexpr COLORREF teal = RGB(43, 145, 175);
    switch (language) {
    case Language::cpp: case Language::csharp: case Language::java:
    case Language::javascript: case Language::typescript:
        for (int style : {kCComment, kCCommentLine, kCCommentDoc}) Fore(editor, style, green);
        for (int style : {kCString, kCCharacter, kCStringEol}) Fore(editor, style, red);
        Fore(editor, kCNumber, teal); Fore(editor, kCWord, blue);
        Fore(editor, kCWord2, purple); Fore(editor, kCPreprocessor, purple);
        SetKeywords(editor, "alignas alignof and asm auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend goto if inline int long namespace new noexcept nullptr operator private protected public register reinterpret_cast return short signed sizeof static struct switch template this throw true try typedef typename union unsigned using virtual void volatile wchar_t while");
        break;
    case Language::python:
        for (int style : {kPCommentLine, kPCommentBlock}) Fore(editor, style, green);
        for (int style : {kPString, kPCharacter, kPTriple, kPTripleDouble}) Fore(editor, style, red);
        Fore(editor, kPNumber, teal); Fore(editor, kPWord, blue);
        SetKeywords(editor, "and as assert async await break class continue def del elif else except False finally for from global if import in is lambda None nonlocal not or pass raise return True try while with yield");
        break;
    case Language::json:
        Fore(editor, kJsonPropertyName, blue); Fore(editor, kJsonString, red);
        Fore(editor, kJsonNumber, teal); Fore(editor, kJsonKeyword, purple);
        break;
    case Language::sql:
        Fore(editor, kSqlComment, green); Fore(editor, kSqlCommentLine, green);
        Fore(editor, kSqlString, red); Fore(editor, kSqlNumber, teal); Fore(editor, kSqlWord, blue);
        SetKeywords(editor, "select from where join inner outer left right on insert update delete create alter drop table view index into values set group by order having union all distinct as and or not null is case when then else end limit offset");
        break;
    default:
        break;
    }
}
}  // namespace

LexillaRuntime::~LexillaRuntime() noexcept {
    if (module_) ::FreeLibrary(module_);
}

bool LexillaRuntime::LoadAdjacent() noexcept {
    wchar_t executable[MAX_PATH]{};
    const DWORD length = ::GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;
    std::wstring path{executable, length};
    path.resize(path.find_last_of(L"\\/") + 1);
    path += L"Lexilla.dll";
    module_ = ::LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!module_) return false;
    create_ = reinterpret_cast<CreateLexerFunction>(::GetProcAddress(module_, "CreateLexer"));
    return create_ != nullptr;
}

void* LexillaRuntime::CreateLexer(Language language) const noexcept {
    const auto name = LexerName(language);
    if (!create_ || name.empty()) return nullptr;
    return create_(name.data());
}

bool ConfigureLanguage(mwfl::ScintillaEditor& editor, const LexillaRuntime& runtime,
                       Language language) noexcept {
    void* lexer = runtime.CreateLexer(language);
    editor.Send(SCI_SETILEXER, 0, reinterpret_cast<LPARAM>(lexer));
    SetProperty(editor, "fold", "1");
    SetProperty(editor, "fold.compact", "1");
    editor.Send(SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    editor.Send(SCI_SETMARGINMASKN, 1, SC_MASK_FOLDERS);
    editor.Send(SCI_SETMARGINSENSITIVEN, 1, 1);
    editor.Send(SCI_SETMARGINWIDTHN, 1, lexer ? 14 : 0);
    editor.Send(SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED);
    ConfigureCommonStyles(editor, language);
    editor.Send(SCI_COLOURISE, 0, -1);
    return language == Language::plain_text || lexer != nullptr;
}

void ConfigureAdvancedEditing(mwfl::ScintillaEditor& editor) noexcept {
    editor.Send(SCI_SETMULTIPLESELECTION, 1);
    editor.Send(SCI_SETADDITIONALSELECTIONTYPING, 1);
    editor.Send(SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH);
    editor.Send(SCI_SETTABINDENTS, 1);
    editor.Send(SCI_SETBACKSPACEUNINDENTS, 1);
    editor.Send(SCI_SETCARETLINEVISIBLE, 1);
    editor.Send(SCI_SETCARETLINEBACK, RGB(245, 248, 252));
    editor.Send(SCI_MARKERDEFINE, kBookmarkMarker, SC_MARK_BOOKMARK);
    editor.Send(SCI_MARKERSETFORE, kBookmarkMarker, RGB(255, 255, 255));
    editor.Send(SCI_MARKERSETBACK, kBookmarkMarker, RGB(0, 120, 215));
    editor.Send(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
    editor.Send(SCI_SETMARGINMASKN, 2, 1 << kBookmarkMarker);
    editor.Send(SCI_SETMARGINSENSITIVEN, 2, 1);
    editor.Send(SCI_SETMARGINWIDTHN, 2, 14);
}

void ToggleBookmark(mwfl::ScintillaEditor& editor) noexcept {
    const auto line = editor.Send(SCI_LINEFROMPOSITION, editor.Send(SCI_GETCURRENTPOS));
    const auto markers = editor.Send(SCI_MARKERGET, line);
    if ((markers & (1 << kBookmarkMarker)) != 0) editor.Send(SCI_MARKERDELETE, line, kBookmarkMarker);
    else editor.Send(SCI_MARKERADD, line, kBookmarkMarker);
}

bool GoToNextBookmark(mwfl::ScintillaEditor& editor) noexcept {
    const auto line = editor.Send(SCI_LINEFROMPOSITION, editor.Send(SCI_GETCURRENTPOS));
    auto next = editor.Send(SCI_MARKERNEXT, line + 1, 1 << kBookmarkMarker);
    if (next < 0) next = editor.Send(SCI_MARKERNEXT, 0, 1 << kBookmarkMarker);
    if (next < 0) return false;
    editor.Send(SCI_GOTOLINE, next);
    return true;
}

void ToggleCurrentFold(mwfl::ScintillaEditor& editor) noexcept {
    const auto line = editor.Send(SCI_LINEFROMPOSITION, editor.Send(SCI_GETCURRENTPOS));
    editor.Send(SCI_TOGGLEFOLD, line);
}

void ToggleRectangularSelection(mwfl::ScintillaEditor& editor) noexcept {
    const auto mode = editor.Send(SCI_GETSELECTIONMODE);
    editor.Send(SCI_SETSELECTIONMODE, mode == SC_SEL_RECTANGLE ? SC_SEL_STREAM : SC_SEL_RECTANGLE);
}

void ToggleWhitespace(mwfl::ScintillaEditor& editor) noexcept {
    editor.Send(SCI_SETVIEWWS, editor.Send(SCI_GETVIEWWS) == SCWS_INVISIBLE ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
}

void ToggleWordWrap(mwfl::ScintillaEditor& editor) noexcept {
    editor.Send(SCI_SETWRAPMODE,
                editor.Send(SCI_GETWRAPMODE) == SC_WRAP_NONE ? SC_WRAP_WORD : SC_WRAP_NONE);
}

void MoveSelectedLines(mwfl::ScintillaEditor& editor, bool down) noexcept {
    editor.Send(down ? SCI_MOVESELECTEDLINESDOWN : SCI_MOVESELECTEDLINESUP);
}
void DuplicateLine(mwfl::ScintillaEditor& editor) noexcept { editor.Send(SCI_LINEDUPLICATE); }
void DeleteLine(mwfl::ScintillaEditor& editor) noexcept { editor.Send(SCI_LINEDELETE); }
void ChangeCase(mwfl::ScintillaEditor& editor, bool upper) noexcept { editor.Send(upper ? SCI_UPPERCASE : SCI_LOWERCASE); }
void IndentSelection(mwfl::ScintillaEditor& editor, bool indent) noexcept { editor.Send(indent ? SCI_TAB : SCI_BACKTAB); }

bool TransformSelectionOrDocument(
    mwfl::ScintillaEditor& editor,
    const std::function<std::wstring(std::wstring_view)>& transform) noexcept {
    auto range = editor.GetSelection();
    if (range.start == range.end) range = {0, editor.GetLength()};
    if (!range || range.end - range.start > static_cast<mwfl::ScintillaPosition>(INT_MAX)) return false;
    editor.Send(SCI_SETTARGETRANGE, range.start, range.end);
    std::string source(static_cast<std::size_t>(range.end - range.start) + 1, '\0');
    const auto copied = editor.Send(SCI_GETTARGETTEXT, 0, reinterpret_cast<LPARAM>(source.data()));
    if (copied < 0) return false;
    source.resize(static_cast<std::size_t>(copied));
    const auto wide = mwfl::FromUtf8(source);
    if (!wide) return false;
    const auto replacement = mwfl::ToUtf8(transform(*wide));
    if (!replacement) return false;
    editor.Send(SCI_BEGINUNDOACTION);
    editor.Send(SCI_REPLACETARGET, replacement->size(), reinterpret_cast<LPARAM>(replacement->data()));
    editor.Send(SCI_ENDUNDOACTION);
    return editor.SetSelection({range.start, range.start +
        static_cast<mwfl::ScintillaPosition>(replacement->size())});
}

bool ReplaceDocumentText(mwfl::ScintillaEditor& editor, std::wstring_view text,
                         mwfl::ScintillaTextRange restore_selection) noexcept {
    const auto replacement = mwfl::ToUtf8(text);
    if (!replacement) return false;
    editor.Send(SCI_BEGINUNDOACTION);
    editor.Send(SCI_SETTARGETRANGE, 0, editor.GetLength());
    editor.Send(SCI_REPLACETARGET, replacement->size(), reinterpret_cast<LPARAM>(replacement->data()));
    editor.Send(SCI_ENDUNDOACTION);
    const auto length = static_cast<mwfl::ScintillaPosition>(replacement->size());
    restore_selection.start = (std::min)(restore_selection.start, length);
    restore_selection.end = (std::min)(restore_selection.end, length);
    return editor.SetSelection(restore_selection);
}

bool SelectNextOccurrence(mwfl::ScintillaEditor& editor, bool all) noexcept {
    auto selection = editor.GetSelection();
    if (selection.start == selection.end) {
        const auto caret = selection.start;
        selection.start = editor.Send(SCI_WORDSTARTPOSITION, caret, 1);
        selection.end = editor.Send(SCI_WORDENDPOSITION, caret, 1);
        if (selection.start == selection.end || !editor.SetSelection(selection)) return false;
    }
    std::string needle(static_cast<std::size_t>(selection.end - selection.start) + 1, '\0');
    editor.Send(SCI_SETTARGETRANGE, selection.start, selection.end);
    const auto copied = editor.Send(SCI_GETTARGETTEXT, 0, reinterpret_cast<LPARAM>(needle.data()));
    if (copied <= 0) return false;
    needle.resize(static_cast<std::size_t>(copied));
    editor.Send(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE | SCFIND_WHOLEWORD);
    auto start = selection.end;
    bool added = false;
    do {
        editor.Send(SCI_SETTARGETRANGE, start, editor.GetLength());
        const auto found = editor.Send(SCI_SEARCHINTARGET, needle.size(),
                                       reinterpret_cast<LPARAM>(needle.data()));
        if (found < 0) break;
        const auto end = editor.Send(SCI_GETTARGETEND);
        editor.Send(SCI_ADDSELECTION, end, found);
        added = true;
        start = end;
    } while (all);
    return added;
}

void ToggleLineComment(mwfl::ScintillaEditor& editor, std::string_view prefix) noexcept {
    const std::string marker(prefix);
    auto selection = editor.GetSelection();
    auto first = editor.Send(SCI_LINEFROMPOSITION, selection.start);
    auto last = editor.Send(SCI_LINEFROMPOSITION, selection.end);
    if (selection.end == editor.Send(SCI_POSITIONFROMLINE, last) && last > first) --last;
    bool uncomment = true;
    for (auto line = first; line <= last; ++line) {
        const auto position = editor.Send(SCI_GETLINEINDENTPOSITION, line);
        for (std::size_t i = 0; i < prefix.size(); ++i)
            if (editor.Send(SCI_GETCHARAT, position + static_cast<LRESULT>(i)) != prefix[i]) {
                uncomment = false; break;
            }
        if (!uncomment) break;
    }
    editor.Send(SCI_BEGINUNDOACTION);
    for (auto line = first; line <= last; ++line) {
        const auto position = editor.Send(SCI_GETLINEINDENTPOSITION, line);
        if (uncomment) {
            editor.Send(SCI_SETTARGETRANGE, position, position + static_cast<LRESULT>(prefix.size()));
            editor.Send(SCI_REPLACETARGET, 0, reinterpret_cast<LPARAM>(""));
        } else {
            editor.Send(SCI_INSERTTEXT, position, reinterpret_cast<LPARAM>(marker.c_str()));
        }
    }
    editor.Send(SCI_ENDUNDOACTION);
}

bool WrapSelection(mwfl::ScintillaEditor& editor, std::wstring_view before,
                   std::wstring_view after) noexcept {
    const auto selection = editor.GetSelection();
    if (selection.start == selection.end) return false;
    const auto before_utf8 = mwfl::ToUtf8(before);
    const auto after_utf8 = mwfl::ToUtf8(after);
    if (!before_utf8 || !after_utf8) return false;
    editor.Send(SCI_BEGINUNDOACTION);
    editor.Send(SCI_INSERTTEXT, selection.end,
                reinterpret_cast<LPARAM>(after_utf8->c_str()));
    editor.Send(SCI_INSERTTEXT, selection.start,
                reinterpret_cast<LPARAM>(before_utf8->c_str()));
    editor.Send(SCI_ENDUNDOACTION);
    const auto before_size = static_cast<mwfl::ScintillaPosition>(before_utf8->size());
    const auto after_size = static_cast<mwfl::ScintillaPosition>(after_utf8->size());
    return editor.SetSelection({selection.start,
                                selection.end + before_size + after_size});
}

bool InsertText(mwfl::ScintillaEditor& editor, std::wstring_view text) noexcept {
    const auto utf8 = mwfl::ToUtf8(text);
    if (!utf8) return false;
    const auto selection = editor.GetSelection();
    editor.Send(SCI_BEGINUNDOACTION);
    editor.Send(SCI_SETTARGETRANGE, selection.start, selection.end);
    editor.Send(SCI_REPLACETARGET, utf8->size(), reinterpret_cast<LPARAM>(utf8->data()));
    editor.Send(SCI_ENDUNDOACTION);
    const auto end = selection.start + static_cast<mwfl::ScintillaPosition>(utf8->size());
    return editor.SetSelection({end, end});
}

void HandleCharacterAdded(mwfl::ScintillaEditor& editor, int character) noexcept {
    if (character == '\n' || character == '\r') {
        const auto position = editor.Send(SCI_GETCURRENTPOS);
        const auto line = editor.Send(SCI_LINEFROMPOSITION, position);
        if (line <= 0) return;
        auto indentation = editor.Send(SCI_GETLINEINDENTATION, line - 1);
        const auto previous_end = editor.Send(SCI_GETLINEENDPOSITION, line - 1);
        if (previous_end > 0 && editor.Send(SCI_GETCHARAT, previous_end - 1) == '{')
            indentation += editor.Send(SCI_GETTABWIDTH);
        editor.Send(SCI_SETLINEINDENTATION, line, indentation);
        editor.Send(SCI_GOTOPOS, editor.Send(SCI_GETLINEINDENTPOSITION, line));
        return;
    }
    const char* closing = nullptr;
    switch (character) {
    case '(': closing = ")"; break;
    case '[': closing = "]"; break;
    case '{': closing = "}"; break;
    case '"': closing = "\""; break;
    case '\'': closing = "'"; break;
    default: return;
    }
    const auto position = editor.Send(SCI_GETCURRENTPOS);
    editor.Send(SCI_INSERTTEXT, position, reinterpret_cast<LPARAM>(closing));
    editor.Send(SCI_GOTOPOS, position);
}

void UpdateBraceHighlight(mwfl::ScintillaEditor& editor) noexcept {
    const auto caret = editor.Send(SCI_GETCURRENTPOS);
    auto brace = caret > 0 ? caret - 1 : caret;
    auto character = editor.Send(SCI_GETCHARAT, brace);
    const auto is_brace = [](LRESULT value) {
        return value == '(' || value == ')' || value == '[' || value == ']' || value == '{' || value == '}';
    };
    if (!is_brace(character)) {
        brace = caret;
        character = editor.Send(SCI_GETCHARAT, brace);
    }
    if (!is_brace(character)) {
        editor.Send(SCI_BRACEHIGHLIGHT, (std::numeric_limits<WPARAM>::max)(), -1);
        return;
    }
    const auto match = editor.Send(SCI_BRACEMATCH, brace);
    if (match >= 0) editor.Send(SCI_BRACEHIGHLIGHT, brace, match);
    else editor.Send(SCI_BRACEBADLIGHT, brace);
}

void GoToLine(mwfl::ScintillaEditor& editor, std::size_t one_based_line) noexcept {
    editor.Send(SCI_GOTOLINE, one_based_line > 0 ? one_based_line - 1 : 0);
    editor.Focus();
}

void ApplyPreferences(mwfl::ScintillaEditor& editor,
                      const Preferences& preferences,
                      bool dark) noexcept {
    const int required = ::WideCharToMultiByte(CP_UTF8, 0, preferences.font_name.c_str(), -1,
                                                nullptr, 0, nullptr, nullptr);
    std::string font(required > 0 ? static_cast<std::size_t>(required) : 1, '\0');
    if (required > 0)
        ::WideCharToMultiByte(CP_UTF8, 0, preferences.font_name.c_str(), -1,
                              font.data(), required, nullptr, nullptr);
    editor.Send(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<LPARAM>(font.c_str()));
    editor.Send(SCI_STYLESETSIZE, STYLE_DEFAULT, preferences.font_size);
    editor.Send(SCI_STYLESETFORE, STYLE_DEFAULT, dark ? RGB(230, 230, 230) : RGB(30, 30, 30));
    editor.Send(SCI_STYLESETBACK, STYLE_DEFAULT, dark ? RGB(30, 30, 30) : RGB(255, 255, 255));
    editor.Send(SCI_STYLECLEARALL);
    editor.Send(SCI_SETTABWIDTH, preferences.tab_width);
    editor.Send(SCI_SETINDENT, preferences.tab_width);
    editor.Send(SCI_SETCARETFORE, dark ? RGB(255, 255, 255) : RGB(0, 0, 0));
    editor.Send(SCI_SETCARETLINEBACK, dark ? RGB(45, 45, 48) : RGB(245, 248, 252));
}

bool PreferencesApplied(const mwfl::ScintillaEditor& editor,
                        const Preferences& preferences) noexcept {
    auto& mutable_editor = const_cast<mwfl::ScintillaEditor&>(editor);
    return mutable_editor.Send(SCI_STYLEGETSIZE, STYLE_DEFAULT) == preferences.font_size &&
           mutable_editor.Send(SCI_GETTABWIDTH) == preferences.tab_width &&
           mutable_editor.Send(SCI_GETINDENT) == preferences.tab_width;
}

}  // namespace notepad_colon
