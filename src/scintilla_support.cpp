#include "scintilla_support.h"

#include <Scintilla.h>

#include <array>
#include <limits>
#include <string>

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

}  // namespace notepad_colon
