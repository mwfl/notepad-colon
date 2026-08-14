#pragma once

#include <mwfl/scintilla.h>
#include <notepad_colon/language.h>

#include <windows.h>

namespace notepad_colon {

class LexillaRuntime final {
public:
    LexillaRuntime() noexcept = default;
    ~LexillaRuntime() noexcept;
    LexillaRuntime(const LexillaRuntime&) = delete;
    LexillaRuntime& operator=(const LexillaRuntime&) = delete;
    bool LoadAdjacent() noexcept;
    void* CreateLexer(Language language) const noexcept;

private:
    using CreateLexerFunction = void*(__cdecl*)(const char*);
    HMODULE module_ = nullptr;
    CreateLexerFunction create_ = nullptr;
};

bool ConfigureLanguage(mwfl::ScintillaEditor& editor, const LexillaRuntime& runtime,
                       Language language) noexcept;
void ConfigureAdvancedEditing(mwfl::ScintillaEditor& editor) noexcept;
void ToggleBookmark(mwfl::ScintillaEditor& editor) noexcept;
bool GoToNextBookmark(mwfl::ScintillaEditor& editor) noexcept;
void ToggleCurrentFold(mwfl::ScintillaEditor& editor) noexcept;
void ToggleRectangularSelection(mwfl::ScintillaEditor& editor) noexcept;
void ToggleWhitespace(mwfl::ScintillaEditor& editor) noexcept;
void ToggleWordWrap(mwfl::ScintillaEditor& editor) noexcept;
void MoveSelectedLines(mwfl::ScintillaEditor& editor, bool down) noexcept;
void DuplicateLine(mwfl::ScintillaEditor& editor) noexcept;
void DeleteLine(mwfl::ScintillaEditor& editor) noexcept;
void ChangeCase(mwfl::ScintillaEditor& editor, bool upper) noexcept;
void IndentSelection(mwfl::ScintillaEditor& editor, bool indent) noexcept;
void HandleCharacterAdded(mwfl::ScintillaEditor& editor, int character) noexcept;
void UpdateBraceHighlight(mwfl::ScintillaEditor& editor) noexcept;

}  // namespace notepad_colon
