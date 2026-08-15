#pragma once

#include <mwfl/scintilla.h>
#include <notepad_colon/language.h>
#include <notepad_colon/language_registry.h>
#include <notepad_colon/preferences.h>
#include <notepad_colon/tree_sitter_document.h>

#include <windows.h>

#include <functional>
#include <span>

namespace notepad_colon {

enum class SyntaxPerformanceMode { full, lightweight };

class LexillaRuntime final {
public:
    LexillaRuntime() noexcept = default;
    ~LexillaRuntime() noexcept;
    LexillaRuntime(const LexillaRuntime&) = delete;
    LexillaRuntime& operator=(const LexillaRuntime&) = delete;
    bool LoadAdjacent() noexcept;
    void* CreateLexer(Language language) const noexcept;
    void* CreateLexer(std::string_view name) const noexcept;

private:
    using CreateLexerFunction = void*(__cdecl*)(const char*);
    HMODULE module_ = nullptr;
    CreateLexerFunction create_ = nullptr;
};

bool ConfigureLanguage(mwfl::ScintillaEditor& editor, const LexillaRuntime& runtime,
                       Language language, bool dark = false,
                       SyntaxPerformanceMode mode = SyntaxPerformanceMode::full) noexcept;
bool ConfigureRegisteredLanguage(mwfl::ScintillaEditor& editor,
                                 const LexillaRuntime& runtime,
                                 const RegisteredLanguage& language,
                                 bool dark = false) noexcept;
void ConfigureTreeSitterStyles(mwfl::ScintillaEditor& editor, bool dark) noexcept;
void ApplyTreeSitterHighlights(mwfl::ScintillaEditor& editor,
                               const TreeSitterDocument& document,
                               std::uint32_t start_byte,
                               std::uint32_t end_byte) noexcept;
void ApplySyntaxSpans(mwfl::ScintillaEditor& editor,
                      std::span<const SyntaxSpan> spans,
                      std::uint32_t start_byte,
                      std::uint32_t end_byte) noexcept;
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
bool TransformSelectionOrDocument(
    mwfl::ScintillaEditor& editor,
    const std::function<std::wstring(std::wstring_view)>& transform) noexcept;
bool ReplaceDocumentText(mwfl::ScintillaEditor& editor, std::wstring_view text,
                         mwfl::ScintillaTextRange restore_selection) noexcept;
bool SelectNextOccurrence(mwfl::ScintillaEditor& editor, bool all) noexcept;
void ToggleLineComment(mwfl::ScintillaEditor& editor, std::string_view prefix) noexcept;
bool WrapSelection(mwfl::ScintillaEditor& editor, std::wstring_view before,
                   std::wstring_view after) noexcept;
bool InsertText(mwfl::ScintillaEditor& editor, std::wstring_view text) noexcept;
void HandleCharacterAdded(mwfl::ScintillaEditor& editor, int character) noexcept;
void UpdateBraceHighlight(mwfl::ScintillaEditor& editor) noexcept;
std::size_t MarkAllMatches(mwfl::ScintillaEditor& editor, std::wstring_view query,
                           mwfl::ScintillaSearchFlags flags,
                           mwfl::ScintillaTextRange scope) noexcept;
void ClearSearchMarks(mwfl::ScintillaEditor& editor) noexcept;
void GoToLine(mwfl::ScintillaEditor& editor, std::size_t one_based_line) noexcept;
void ApplyPreferences(mwfl::ScintillaEditor& editor,
                      const Preferences& preferences,
                      bool dark) noexcept;
bool PreferencesApplied(const mwfl::ScintillaEditor& editor,
                        const Preferences& preferences) noexcept;

}  // namespace notepad_colon
