#include "main_window.h"

namespace notepad_colon::app {

void MainWindow::SetActiveLanguage(notepad_colon::Language language) {
    auto* document = ActiveDocument();
    if (!document || document->language == language) return;
    if (!notepad_colon::ConfigureLanguage(
            *document->editor, lexilla_, language, IsDark(),
            document->large_buffer ? notepad_colon::SyntaxPerformanceMode::lightweight
                                  : notepad_colon::SyntaxPerformanceMode::full)) {
        status_.SetText(Tr(L"Could not load the selected syntax lexer"));
        return;
    }
    document->language = language;
    for (const auto& definition : language_registry_.Languages())
        if (definition.builtin == language) { document->language_id = definition.id; break; }
    InitializeSyntaxTree(*document);
    status_.SetText(Tr(L"Language: ") + std::wstring(notepad_colon::LanguageName(language)));
}

std::filesystem::path MainWindow::LanguageDirectory(bool test_mode) {
    if (test_mode) return std::filesystem::temp_directory_path() /
        (L"notepad-colon-gui-languages-" + std::to_wstring(::GetCurrentProcessId()));
    wchar_t local_app_data[32768]{};
    const DWORD length = ::GetEnvironmentVariableW(
        L"LOCALAPPDATA", local_app_data, static_cast<DWORD>(std::size(local_app_data)));
    return length > 0 && length < std::size(local_app_data)
        ? std::filesystem::path{local_app_data} / L"mwfl" / L"Notepad Colon" / L"languages"
        : std::filesystem::path{};
}

void MainWindow::LoadLanguageDefinitions() {
    language_registry_.ResetBuiltins();
    const auto directory = LanguageDirectory(IsTestMode());
    if (!directory.empty()) static_cast<void>(language_registry_.LoadDirectory(directory));
}

void MainWindow::ReloadLanguageDefinitions() {
    LoadLanguageDefinitions();
    AddLanguageCommands();
    BuildMenu();
    mwfl::Must(accelerators_.Create(commands_), "recreate language accelerators");
    SetAccelerators(accelerators_.GetHandle());
    const auto custom = std::ranges::count_if(language_registry_.Languages(),
        [](const auto& language) { return !language.builtin.has_value(); });
    status_.SetText(Tr(L"Language definitions reloaded: ") + std::to_wstring(custom) +
        Tr(L" custom, ") + std::to_wstring(language_registry_.Errors().size()) + Tr(L" error(s)"));
}

void MainWindow::SetActiveRegisteredLanguage(std::string_view id) {
    const auto* language = language_registry_.Find(id);
    auto* document = ActiveDocument();
    if (!language || !document) return;
    if (language->builtin) { SetActiveLanguage(*language->builtin); return; }
    if (!notepad_colon::ConfigureRegisteredLanguage(
            *document->editor, lexilla_, *language, IsDark())) {
        status_.SetText(Tr(L"Could not load the custom language fallback lexer"));
        return;
    }
    document->language = notepad_colon::Language::plain_text;
    document->language_id = language->id;
    InitializeSyntaxTree(*document);
    status_.SetText(Tr(L"Language: ") + language->name);
}

std::optional<std::string> MainWindow::EditorUtf8(const mwfl::ScintillaEditor& editor) noexcept {
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

void MainWindow::InitializeSyntaxTree(EditorDocument& document) noexcept {
    document.syntax_tree.reset();
    document.wasm_syntax.reset();
    if (document.large_buffer || document.follow_tail) return;
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
                status_.SetText(Tr(L"Wasm language host rejected or timed out")); return;
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

void MainWindow::StyleVisibleSyntax(EditorDocument& document) noexcept {
    StyleVisibleSyntax(document, *document.editor);
}

// Styles are stored in the shared document, so each view only has to make
// sure the range it is showing has been coloured.
void MainWindow::StyleVisibleSyntax(EditorDocument& document, mwfl::ScintillaEditor& view) noexcept {
    if (!document.syntax_tree && !document.wasm_syntax) return;
    const auto first_visible = view.Send(SCI_GETFIRSTVISIBLELINE);
    const auto visible_count = view.Send(SCI_LINESONSCREEN);
    const auto first_line = view.Send(
        SCI_DOCLINEFROMVISIBLE, (std::max<LRESULT>)(0, first_visible - 64));
    const auto last_line = view.Send(
        SCI_DOCLINEFROMVISIBLE, first_visible + visible_count + 64);
    const auto start = view.Send(SCI_POSITIONFROMLINE, first_line);
    auto end = view.Send(SCI_GETLINEENDPOSITION, last_line);
    if (end < 0) end = view.GetLength();
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

void MainWindow::UpdateSyntaxTree(EditorDocument& document, const SCNotification& notification) noexcept {
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
        status_.SetText(Tr(L"Wasm language host stopped after a timeout or protocol failure"));
    }
}

void MainWindow::ToggleActiveLineComment() {
    auto* document = ActiveDocument();
    if (!document) return;
    const auto prefix = notepad_colon::GetLanguageProfile(document->language).line_comment;
    if (prefix.empty()) {
        status_.SetText(Tr(L"This language has no line comment"));
        return;
    }
    notepad_colon::ToggleLineComment(*document->editor, prefix);
}

void MainWindow::ShowLocalCompletion() {
    auto* document = ActiveDocument();
    if (!document || document->large_buffer) {
        status_.SetText(Tr(L"Local completion is disabled for large files")); return;
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
        status_.SetText(Tr(L"No local completion candidates")); return;
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

void MainWindow::ShowDocumentSymbols() {
    auto* document = ActiveDocument();
    const auto text = document ? EditorUtf8(*document->editor) : std::nullopt;
    if (!document || (!document->syntax_tree && !document->wasm_syntax) || !text) {
        status_.SetText(Tr(L"Document symbols require an active Tree-sitter language")); return;
    }
    const auto symbols = document->syntax_tree
        ? document->syntax_tree->Symbols(*text) : document->wasm_syntax->Symbols();
    if (symbols.empty()) { status_.SetText(Tr(L"No document symbols found")); return; }
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
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Document Symbols"),
        .initial_client_size = {520.0_dip, 420.0_dip}, .resizable = true,
        .callbacks = {.initialize = [&](HWND window) {
            mwfl::ControlHost ui{window}; ui.Add(query, {741}, L""); ui.Add(list, {742}, {});
            ui.Add(open, {IDOK}, Tr(L"Go to")); ui.Add(cancel, {IDCANCEL}, Tr(L"Cancel"));
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

}  // namespace notepad_colon::app
