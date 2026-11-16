#include "main_window.h"

namespace notepad_colon::app {

void MainWindow::RunSelfTest() noexcept {
    if (large_file_self_test_) {
        RunLargeFileSelfTest();
        return;
    }
    int result = 0;
    std::vector<std::filesystem::path> cleanup;
    try {
        if (::SendMessageW(GetHwnd(), WM_GETICON, ICON_BIG, 0) == 0 ||
            ::SendMessageW(GetHwnd(), WM_GETICON, ICON_SMALL, 0) == 0)
            result = 42;
        std::function<bool(HMENU)> validate_menu;
        validate_menu = [&](HMENU menu) -> bool {
            const int count = ::GetMenuItemCount(menu);
            if (count < 0) return false;
            for (int index = 0; index < count; ++index) {
                if (const auto submenu = ::GetSubMenu(menu, index)) {
                    if (!validate_menu(submenu)) return false;
                    continue;
                }
                const UINT id = ::GetMenuItemID(menu, index);
                if (id == 0 || id == static_cast<UINT>(-1)) continue;
                if (id > (std::numeric_limits<WORD>::max)() ||
                    !commands_.Find(mwfl::ControlId{static_cast<WORD>(id)}))
                    return false;
            }
            return true;
        };
        if (::GetMenuItemCount(menu_.GetHandle()) != 10 ||
            !validate_menu(menu_.GetHandle()))
            result = 43;
        const auto toolbar_count = static_cast<int>(
            ::SendMessageW(toolbar_.GetHwnd(), TB_BUTTONCOUNT, 0, 0));
        if (toolbar_count != 9) result = 44;
        SIZE toolbar_image_size{};
        if (!toolbar_images_.GetIconSize(toolbar_image_size) ||
            toolbar_image_size.cx != 24 || toolbar_image_size.cy != 24)
            result = 44;
        for (int index = 0; index < toolbar_count; ++index) {
            TBBUTTON button{};
            if (::SendMessageW(toolbar_.GetHwnd(), TB_GETBUTTON, index,
                               reinterpret_cast<LPARAM>(&button)) == FALSE ||
                ((button.fsStyle & BTNS_SEP) == 0 &&
                 !commands_.Find(mwfl::ControlId{static_cast<WORD>(button.idCommand)})))
                result = 44;
        }
        {
            // Every command and every top-level menu must have a Chinese title,
            // and switching back must restore the English one exactly.
            const auto menu_title = [&](int index) {
                wchar_t buffer[128]{};
                ::GetMenuStringW(menu_.GetHandle(), static_cast<UINT>(index), buffer, 128, MF_BYPOSITION);
                return std::wstring{buffer};
            };
            const auto english_file_menu = menu_title(0);
            SetUiLanguage(true);
            if (notepad_colon::GetUiLanguage() != notepad_colon::UiLanguage::chinese_simplified ||
                menu_title(0) != L"文件(&F)" || menu_title(9) != L"帮助(&H)" ||
                ::GetMenuItemCount(menu_.GetHandle()) != 10 || !validate_menu(menu_.GetHandle()))
                result = 80;
            for (const auto& [id, english] : command_titles_) {
                const auto* command = commands_.Find(mwfl::ControlId{id});
                if (!command || !notepad_colon::HasTranslation(
                        notepad_colon::UiLanguage::chinese_simplified, english) ||
                    CommandLabel(*command, true) != Tr(english)) { result = 81; break; }
            }
            if (const auto* reopen = commands_.Find(kReopenCodePageBase);
                !reopen || reopen->GetText() != L"重新打开为 简体中文 (GB18030)") result = 82;
            if (Tr(L"Saved") != L"已保存" || Tr(L"not in the catalog") != L"not in the catalog")
                result = 83;
            SetUiLanguage(false);
            if (menu_title(0) != english_file_menu || Tr(L"Saved") != L"Saved") result = 84;
            for (const auto& [id, english] : command_titles_) {
                const auto* command = commands_.Find(mwfl::ControlId{id});
                if (!command || CommandLabel(*command, true) != english) { result = 85; break; }
            }
        }
        {
            // Two built-in commands on one key would make one unreachable and
            // block every change in the shortcut editor. Menus must show the key.
            if (!notepad_colon::FindShortcutConflicts(CaptureShortcuts()).empty()) result = 87;
            const auto* print = commands_.Find(kPrint);
            const auto* quick_open = commands_.Find(kQuickOpen);
            if (!print || !print->GetText().ends_with(L"	Ctrl+P") ||
                !quick_open || !quick_open->GetText().ends_with(L"	Ctrl+E")) result = 88;
        }
        const auto command_processor = CommandProcessorPath();
        auto command_line = L"\"" + command_processor.wstring() + L"\" /D /C exit";
        STARTUPINFOW terminal_startup{sizeof(terminal_startup)};
        PROCESS_INFORMATION terminal_process{};
        if (command_processor.empty() ||
            ::CreateProcessW(command_processor.c_str(), command_line.data(), nullptr,
                             nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                             std::filesystem::temp_directory_path().c_str(),
                             &terminal_startup, &terminal_process) == FALSE) {
            result = 45;
        } else {
            ::CloseHandle(terminal_process.hThread);
            const DWORD waited = ::WaitForSingleObject(terminal_process.hProcess, 5000);
            DWORD exit_code = 1;
            if (waited != WAIT_OBJECT_0 ||
                ::GetExitCodeProcess(terminal_process.hProcess, &exit_code) == FALSE ||
                exit_code != 0)
                result = 45;
            ::CloseHandle(terminal_process.hProcess);
        }
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
        if (result == 0) {
            // Split view: two views, one document.
            const auto text_of = [](mwfl::ScintillaEditor& view) {
                return view.GetText().value_or(std::wstring{L"<unreadable>"});
            };
            RECT full{};
            ::GetWindowRect(first->editor->GetHwnd(), &full);
            ToggleSplitView();
            if (!first->split_editor) result = 50;
            else {
                auto& clone = *first->split_editor;
                if (clone.Send(SCI_GETDOCPOINTER) != first->editor->Send(SCI_GETDOCPOINTER) ||
                    text_of(clone) != text_of(*first->editor)) result = 51;
                // An edit made through either view is visible in the other,
                // and is reported exactly once (dirty state, not duplicated text).
                const auto before_length = first->editor->GetLength();
                clone.Send(SCI_INSERTTEXT, 0, reinterpret_cast<LPARAM>("// clone\n"));
                if (first->editor->GetLength() != before_length + 9 ||
                    !text_of(*first->editor).starts_with(L"// clone\n")) result = 52;
                first->editor->Send(SCI_APPENDTEXT, 7, reinterpret_cast<LPARAM>("\n// own"));
                if (!text_of(clone).ends_with(L"\n// own")) result = 53;
                clone.Undo(); clone.Undo();
                if (first->editor->GetLength() != before_length) result = 54;
                RECT left{}, right{};
                ::GetWindowRect(first->editor->GetHwnd(), &left);
                ::GetWindowRect(clone.GetHwnd(), &right);
                if (result == 0 && (left.right > right.left || left.right - left.left <= 0 ||
                    right.right - right.left <= 0 || left.top != right.top ||
                    right.right != full.right || left.left != full.left)) result = 55;
                RotateSplitView();
                ::GetWindowRect(first->editor->GetHwnd(), &left);
                ::GetWindowRect(clone.GetHwnd(), &right);
                if (result == 0 && (left.bottom > right.top || left.left != right.left ||
                    right.bottom != full.bottom || left.top != full.top)) result = 56;
                if (result == 0 && FindByHwnd(clone.GetHwnd()) != first) result = 57;
                // The split belongs to its document: another tab hides it.
                NewDocument(L"other", L"Other");
                if (result == 0 && ::IsWindowVisible(clone.GetHwnd())) result = 58;
                if (!CloseActive(true)) result = 59;
                first = ActiveDocument();
                if (result == 0 && (!first || !first->split_editor)) result = 59;
                if (result == 0) {
                    ToggleSplitView();
                    RECT restored{};
                    ::GetWindowRect(first->editor->GetHwnd(), &restored);
                    if (first->split_editor || ::EqualRect(&restored, &full) == FALSE) result = 60;
                }
            }
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
        if (result == 0) {
            // Legacy CJK encodings: detect on open, keep on save, convert on demand.
            const std::wstring traditional = L"這是一個繁體中文檔案，我們需要打開資料夾並且儲存。\r\n";
            const auto big5 = notepad_colon::EncodeText(traditional, notepad_colon::EncodingKind::ansi, 950);
            const auto legacy_path = std::filesystem::temp_directory_path() /
                (L"notepad-colon-gui-big5-" + std::to_wstring(::GetCurrentProcessId()) + L".txt");
            cleanup.push_back(legacy_path);
            auto legacy_backup = legacy_path; legacy_backup += L".bak"; cleanup.push_back(legacy_backup);
            if (!big5) result = 61;
            else {
                std::ofstream(legacy_path, std::ios::binary).write(
                    reinterpret_cast<const char*>(big5->data()), static_cast<std::streamsize>(big5->size()));
                if (!OpenPath(legacy_path)) result = 61;
            }
            auto* legacy = result == 0 ? ActiveDocument() : nullptr;
            if (result == 0 && (!legacy ||
                legacy->detected_encoding != notepad_colon::EncodingKind::ansi ||
                legacy->ansi_code_page != 950 ||
                legacy->editor->GetText() != std::optional<std::wstring>{traditional} ||
                DocumentEncodingName(*legacy).find(L"Big5") == std::wstring::npos)) result = 62;
            if (result == 0) {
                // Appending and saving must keep Big5 on disk.
                legacy->editor->Send(SCI_APPENDTEXT, 5, reinterpret_cast<LPARAM>("ASCII"));
                const auto expected = notepad_colon::EncodeText(
                    traditional + L"ASCII", notepad_colon::EncodingKind::ansi, 950);
                if (!SaveDocument(*legacy, false) || !expected ||
                    ReadFileBytes(legacy_path) != std::optional{*expected}) result = 63;
            }
            if (result == 0) {
                // Shift-JIS cannot represent this text; the conversion must refuse.
                SetAnsiEncoding(932);
                if (legacy->ansi_code_page != 950) result = 64;
                // GB18030 covers all of Unicode, so the conversion is lossless.
                SetAnsiEncoding(54936);
                const auto expected = notepad_colon::EncodeText(
                    traditional + L"ASCII", notepad_colon::EncodingKind::ansi, 54936);
                if (legacy->ansi_code_page != 54936 || !SaveDocument(*legacy, false) || !expected ||
                    ReadFileBytes(legacy_path) != std::optional{*expected}) result = 65;
            }
            if (result == 0) {
                // Reading GB18030 bytes as Big5 is a wrong guess the user can correct.
                ReopenActiveWithCodePage(54936);
                if (legacy->editor->GetText() != std::optional<std::wstring>{traditional + L"ASCII"})
                    result = 66;
            }
            if (legacy && !CloseActive(true)) result = 67;
        }
        if (result == 0) {
            // Rainbow columns: one indicator per field, quoted delimiters respected.
            NewDocument(L"id,name,value\r\n1,alpha,3\r\n2,\"b,c\",4\r\n", L"table.csv");
            auto* table = ActiveDocument();
            auto& view = *table->editor;
            const auto indicator_at = [&](int indicator, LRESULT position) {
                return view.Send(SCI_INDICATORVALUEAT, indicator, position) != 0;
            };
            const bool clean_before = !workspace_.Find(table->id)->dirty;
            ToggleRainbowColumns();
            const auto third_line = view.Send(SCI_POSITIONFROMLINE, 2);
            if (table->delimiter != ',' || !indicator_at(24, 0) || indicator_at(25, 0) ||
                !indicator_at(25, 3) || !indicator_at(26, 8) ||
                // "b,c" is one field: both letters and the comma inside the quotes.
                !indicator_at(25, third_line + 3) || !indicator_at(25, third_line + 4) ||
                !indicator_at(25, third_line + 5) || !indicator_at(26, third_line + 8)) result = 90;
            // Decorating is not an edit: the document must still be clean.
            if (result == 0 && (!clean_before || workspace_.Find(table->id)->dirty ||
                view.IsModified())) result = 91;
            view.Send(SCI_GOTOPOS, third_line + 4);
            if (result == 0 && DelimitedFieldStatus(*table) != L"Field 2 | ") result = 92;
            ToggleRainbowColumns();
            if (result == 0 && (table->delimiter != 0 || indicator_at(24, 0) ||
                indicator_at(25, third_line + 4))) result = 93;
            // Prose has no consistent delimiter and must be refused.
            view.SetText(L"Just a sentence, with one comma.\r\nAnd a second line.\r\n");
            ToggleRainbowColumns();
            if (result == 0 && table->delimiter != 0) result = 94;

            // Highlighted log lines follow the rules and track edits.
            view.SetText(L"INFO ok\r\nERROR bad\r\nWARN slow\r\nDEBUG noise\r\n");
            ToggleHighlightLines();
            const auto marker = [&](LRESULT line) { return view.Send(SCI_MARKERGET, line) & 0x1fc00; };
            if (result == 0 && (!table->highlight_lines || marker(0) != 0 ||
                marker(1) != (1 << 10) || marker(2) != (1 << 11) || marker(3) != (1 << 16))) result = 95;
            view.Send(SCI_INSERTTEXT, 0, reinterpret_cast<LPARAM>("FATAL "));
            DecorateVisibleLines(*table, view, 0);
            if (result == 0 && (marker(0) != (1 << 10) || marker(1) != (1 << 10))) result = 96;
            // Custom rules replace the defaults.
            if (auto rules = notepad_colon::ParseHighlightRules("blue=~info; green=ok")) {
                highlight_rules_ = std::move(*rules);
                ++highlight_rules_revision_;
                DecorateVisibleLines(*table, view, 0);
                if (result == 0 && (marker(0) != (1 << 14) || marker(1) != 0)) result = 97;
                highlight_rules_ = notepad_colon::DefaultHighlightRules();
                ++highlight_rules_revision_;
            } else result = 97;

            // Line filter: matching lines open in a new tab with their line numbers.
            const auto documents_before = workspace_.GetCount();
            StartLineFilter(L"error", {});
            for (auto& worker : search_workers_)
                if (worker.thread.joinable()) worker.thread.join();
            CompleteLineFilter();
            auto* filtered = ActiveDocument();
            const auto* filtered_metadata = filtered ? workspace_.Find(filtered->id) : nullptr;
            if (result == 0 && (workspace_.GetCount() != documents_before + 1 || !filtered_metadata ||
                !filtered_metadata->title.starts_with(L"Filter: error") ||
                filtered->editor->GetText() != std::optional<std::wstring>{L"2: ERROR bad\r\n"} ||
                !filtered->highlight_lines)) result = 98;
            if (result == 0) {
                // A result row leads back to the line it came from.
                const auto filtered_id = filtered->id;
                GoToFilterSource();
                const auto* source = ActiveDocument();
                // Opening the result tab may have moved the documents: compare editors.
                if (!source || source->editor.get() != &view ||
                    view.Send(SCI_LINEFROMPOSITION, view.Send(SCI_GETCURRENTPOS)) != 1) result = 120;
                // A tab that is not a filter result refuses politely.
                GoToFilterSource();
                if (result == 0 && (!ActiveDocument() || ActiveDocument()->editor.get() != &view)) result = 125;
                workspace_.Activate(filtered_id);
                SynchronizeTabs(false);
            }
            if (workspace_.GetCount() == documents_before + 1 && !CloseActive(true)) result = 99;
            // A filter with no matches opens nothing.
            workspace_.Activate(table->id);
            StartLineFilter(L"no such text", {});
            for (auto& worker : search_workers_)
                if (worker.thread.joinable()) worker.thread.join();
            CompleteLineFilter();
            if (result == 0 && workspace_.GetCount() != documents_before) result = 99;
            workspace_.Activate(table->id);
            SynchronizeTabs(false);
            if (result == 0) {
                // Column tools work on the column under the caret, with or
                // without rainbow columns switched on.
                view.SetText(L"name,score\r\nbeta,10\r\nalpha,9\r\ngamma,100\r\n");
                view.Send(SCI_GOTOPOS, 6);  // inside "score"
                SortByColumn(false);
                if (view.GetText() != std::optional<std::wstring>{
                        L"name,score\r\nalpha,9\r\nbeta,10\r\ngamma,100\r\n"}) result = 121;
                view.Send(SCI_GOTOPOS, 6);
                SortByColumn(true);
                if (result == 0 && view.GetText() != std::optional<std::wstring>{
                        L"name,score\r\ngamma,100\r\nbeta,10\r\nalpha,9\r\n"}) result = 122;
                view.Send(SCI_GOTOPOS, 1);  // inside "name"
                StartColumnExtract();
                for (auto& worker : search_workers_)
                    if (worker.thread.joinable()) worker.thread.join();
                CompleteColumnExtract();
                const auto* column = ActiveDocument();
                const auto* column_metadata = column ? workspace_.Find(column->id) : nullptr;
                if (result == 0 && (workspace_.GetCount() != documents_before + 1 || !column_metadata ||
                    !column_metadata->title.starts_with(L"Column 1") ||
                    column->editor->GetText() != std::optional<std::wstring>{
                        L"name\r\ngamma\r\nbeta\r\nalpha\r\n"})) result = 123;
                if (workspace_.GetCount() == documents_before + 1 && !CloseActive(true)) result = 99;
                if (const auto* owner = FindByHwnd(view.GetHwnd())) workspace_.Activate(owner->id);
                SynchronizeTabs(false);
                // Prose has no columns: nothing is changed and nothing opens.
                view.SetText(L"Just a sentence, with one comma.\r\nAnd a second line.\r\n");
                SortByColumn(false);
                StartColumnExtract();
                if (result == 0 && (workspace_.GetCount() != documents_before ||
                    view.GetText() != std::optional<std::wstring>{
                        L"Just a sentence, with one comma.\r\nAnd a second line.\r\n"})) result = 124;
            }
            if (!CloseActive(true)) result = 99;
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
            workspace_operations_.Publish(workspace_operations_.CurrentGeneration(),
                std::vector<std::pair<std::filesystem::path, notepad_colon::WorkspaceScan>>{
                    {test_workspace, notepad_colon::ScanWorkspace(test_workspace)}});
        }
        CompleteWorkspaceScan();
        if (result == 0 && TreeView_GetCount(tree_.GetHwnd()) < 3) result = 17;
        {
            search_operations_.Publish(search_operations_.CurrentGeneration(),
                notepad_colon::SearchWorkspace(test_workspace, L"needle"));
        }
        CompleteSearch();
        if (result == 0 && ListView_GetItemCount(results_.GetHwnd()) != 1) result = 18;
        if (result == 0) {
            // A folder handed to the application (Explorer verb, command line,
            // drag and drop) becomes a workspace root instead of a failed open.
            const auto folder = test_workspace / L"src";
            const auto documents_before = workspace_.GetCount();
            if (!OpenPathOrFolder(folder) || workspace_.GetCount() != documents_before ||
                !notepad_colon::IsWithinWorkspaceRoots(folder / L"x.txt", workspace_catalog_.Roots()) ||
                OpenPathOrFolder(test_workspace / L"does-not-exist.txt")) result = 86;
            static_cast<void>(workspace_catalog_.RemoveRoot(folder));
        }
        if (result == 0) {
            // Another program rewrites an open, unmodified file: the document
            // reloads, remembers the old text, and each change can be rejected.
            const auto review_file = test_workspace / L"review.txt";
            const std::wstring before = L"one\r\ntwo\r\nthree\r\n";
            const std::wstring after = L"one\r\nTWO\r\nthree\r\nfour\r\n";
            if (!mwfl::WriteTextFileAtomic(review_file, before, mwfl::TextEncoding::utf8).Succeeded() ||
                !OpenPath(review_file)) result = 101;
            const auto review_id = workspace_.GetActiveId();
            const auto reload = [&](const std::wstring& text) {
                return mwfl::WriteTextFileAtomic(review_file, text, mwfl::TextEncoding::utf8).Succeeded() &&
                       (CheckExternalChanges(true), true);
            };
            auto* review = result == 0 && review_id ? FindDocument(*review_id) : nullptr;
            if (result == 0) {
                if (!review) result = 110;
                else if (!reload(after)) result = 111;
                else if (review->editor->GetText() != after) result = 112;
                else if (review->review_base != before) result = 113;
                else if (PendingReviewCount() == 0) result = 114;
                else if ((review->editor->Send(SCI_MARKERGET, 1) & (1 << kReviewMarker)) == 0) result = 115;
                else if ((review->editor->Send(SCI_MARKERGET, 0) & (1 << kReviewMarker)) != 0) result = 116;
            }
            // A second external edit is reviewed against the same original text.
            const std::wstring later = L"one\r\nTWO\r\nthree\r\nfour\r\nfive\r\n";
            if (result == 0 && (!reload(later) || review->review_base != before)) result = 103;
            // Reject the first change, keep the appended lines.
            if (result == 0 && (!FinishReview(*review, {true, false}) ||
                review->editor->GetText() != L"one\r\ntwo\r\nthree\r\nfour\r\nfive\r\n" ||
                review->review_base ||
                review->editor->Send(SCI_MARKERNEXT, 0, 1 << kReviewMarker) != -1)) result = 104;
            const auto* review_metadata = review_id ? workspace_.Find(*review_id) : nullptr;
            if (result == 0 && (!review_metadata || !review_metadata->dirty)) result = 105;
            // Once the tool undoes its own edit there is nothing left to review.
            if (result == 0) {
                review->editor->SetSavePoint();
                workspace_.SetDirty(*review_id, false);
                review->file_state = notepad_colon::CaptureFileState(review_file);
                const auto current = review->editor->GetText().value_or(L"");
                if (!reload(before) || !review->review_base || !reload(current) ||
                    review->review_base) result = 106;
            }
            if (review_id) { workspace_.Activate(*review_id); static_cast<void>(CloseActive(true)); }
        }
        if (result == 0) {
            // Replace in Files: disk files are rewritten, open documents are
            // changed in the editor and stay unsaved.
            const auto open_file = test_workspace / L"src" / L"open.txt";
            const auto skipped_file = test_workspace / L"src" / L"skip.log";
            if (!mwfl::WriteTextFileAtomic(open_file, L"open needle document\n",
                                           mwfl::TextEncoding::utf8).Succeeded() ||
                !mwfl::WriteTextFileAtomic(skipped_file, L"needle in a log\n",
                                           mwfl::TextEncoding::utf8).Succeeded() ||
                !OpenPath(open_file)) result = 70;
            auto* open_document = result == 0 ? ActiveDocument() : nullptr;
            const auto finish_phase = [&] {
                for (auto& worker : search_workers_)
                    if (worker.thread.joinable()) worker.thread.join();
                CompleteReplace();
            };
            if (result == 0) {
                search_exclude_globs_ = L"*.log";
                BeginReplaceInFiles(L"needle", L"thread", true);
                finish_phase();  // preview, auto-confirmed in test mode
                if (!pending_replace_.applying) result = 71;
                finish_phase();  // rewrite
                const auto rewritten = mwfl::ReadTextFile(workspace_file);
                const auto on_disk = mwfl::ReadTextFile(open_file);
                const auto skipped = mwfl::ReadTextFile(skipped_file);
                const auto backup = mwfl::ReadTextFile(workspace_file.wstring() + L".bak");
                if (result == 0 && (!rewritten.Succeeded() ||
                    rewritten.value->text != L"folder thread result\n")) result = 72;
                if (result == 0 && (!backup.Succeeded() ||
                    backup.value->text != L"folder needle result\n")) result = 73;
                if (result == 0 && (!on_disk.Succeeded() ||
                    on_disk.value->text != L"open needle document\n" ||
                    open_document->editor->GetText() !=
                        std::optional<std::wstring>{L"open thread document\n"} ||
                    !workspace_.Find(open_document->id)->dirty)) result = 74;
                if (result == 0 && (!skipped.Succeeded() ||
                    skipped.value->text != L"needle in a log\n")) result = 75;
                if (result == 0 && (ListView_GetItemCount(results_.GetHwnd()) != 2 ||
                    pending_replace_.open_documents_changed != 1 ||
                    pending_replace_.open_replacements != 1)) result = 76;
                // The editor change is a single undoable step.
                open_document->editor->Undo();
                if (result == 0 && open_document->editor->GetText() !=
                        std::optional<std::wstring>{L"open needle document\n"}) result = 77;
                // A disk change after the worker captured its preview aborts
                // the whole batch, including the open-buffer portion.
                const auto stale_file = test_workspace / L"src" / L"stale.txt";
                if (!notepad_colon::WriteEncodedFileAtomic(stale_file, L"needle before\n",
                        notepad_colon::EncodingKind::utf8)) result = 170;
                BeginReplaceInFiles(L"needle", L"thread", false);
                for (auto& worker : search_workers_)
                    if (worker.thread.joinable()) worker.thread.join();
                if (!notepad_colon::WriteEncodedFileAtomic(stale_file, L"newer changes\n",
                        notepad_colon::EncodingKind::utf8)) result = 171;
                CompleteReplace();
                finish_phase();
                if (result == 0 && (!pending_replace_.plan.preflight_failed ||
                    open_document->editor->GetText() != L"open needle document\n" ||
                    mwfl::ReadTextFile(stale_file).value->text != L"newer changes\n")) result = 172;
                // Editing an open buffer while the disk phase runs keeps the
                // user's new text; disk successes remain visible in history.
                if (!notepad_colon::WriteEncodedFileAtomic(stale_file, L"needle before\n",
                        notepad_colon::EncodingKind::utf8)) result = 173;
                BeginReplaceInFiles(L"needle", L"thread", false);
                finish_phase();
                open_document->editor->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("!"));
                finish_phase();
                bool buffer_conflict = false;
                for (const auto& entry : pending_replace_.plan.files)
                    if (entry.document == open_document->id.value)
                        buffer_conflict = entry.progress == notepad_colon::ReplaceProgress::conflict;
                if (result == 0 && (!buffer_conflict ||
                    open_document->editor->GetText() != L"open needle document\n!" ||
                    mwfl::ReadTextFile(stale_file).value->text != L"thread before\n")) result = 174;
                open_document->editor->Undo();
                // Cancellation delivered after worker completion must still
                // prevent the pending buffer edits on the UI thread.
                BeginReplaceInFiles(L"needle", L"thread", false);
                finish_phase();
                for (auto& worker : search_workers_)
                    if (worker.thread.joinable()) worker.thread.join();
                CancelFolderSearch();
                CompleteReplace();
                if (result == 0 && (!pending_replace_.plan.cancelled ||
                    open_document->editor->GetText() != L"open needle document\n")) result = 175;
                // A query without matches never reaches the rewrite phase.
                BeginReplaceInFiles(L"no-such-text-anywhere", L"x", false);
                finish_phase();
                if (result == 0 && pending_replace_.applying) result = 78;
            }
            if (open_document && !CloseActive(true)) result = 79;
        }
        if (result == 0) {
            // Exercise the actual open -> external reload -> edit -> save path
            // for every supported encoding, rather than only the decoder model.
            const auto encoding_root = test_workspace / L"encoding-cases";
            std::filesystem::create_directories(encoding_root);
            std::ofstream(encoding_root / L".editorconfig") <<
                "root=true\n[*.txt]\ntrim_trailing_whitespace=false\ninsert_final_newline=false\n";
            struct Sample { notepad_colon::EncodingKind kind; unsigned int page; std::wstring_view text; };
            using Kind = notepad_colon::EncodingKind;
            const Sample samples[]{
                {Kind::utf8, 0, L"Unicode Ω text\n"}, {Kind::utf8_bom, 0, L"Unicode 中文 text\n"},
                {Kind::utf16_le, 0, L"UTF-16 LE 😀\n"}, {Kind::utf16_be, 0, L"UTF-16 BE 😀\n"},
                {Kind::ansi, 54936, L"这是简体中文文件，我们需要打开并保存。\n"},
                {Kind::ansi, 950, L"這是繁體中文檔案，我們需要打開並儲存。\n"},
                {Kind::ansi, 932, L"これは日本語のテキストファイルです。保存します。\n"},
                {Kind::ansi, 51932, L"これは日本語のテキストファイルです。保存します。\n"},
                {Kind::ansi, 949, L"이것은 한국어 텍스트 파일입니다. 저장합니다.\n"},
                {Kind::ansi, 1252, L"Café déjà vu, Straße.\n"},
            };
            std::size_t index = 0;
            for (const auto& sample : samples) {
                if (result != 0) break;
                if (sample.page && !::IsValidCodePage(sample.page)) continue;
                const auto path = encoding_root / (std::to_wstring(index++) + L".txt");
                if (!notepad_colon::WriteEncodedFileAtomic(path, sample.text, sample.kind, sample.page) ||
                    !OpenPath(path, sample.page, false)) { result = 150; break; }
                auto* current = ActiveDocument();
                const auto after = L"changed\n" + std::wstring{sample.text};
                if (!notepad_colon::WriteEncodedFileAtomic(path, after, sample.kind, sample.page)) result = 151;
                CheckExternalChanges(true);
                if (!current || current->editor->GetText() != after || current->detected_encoding != sample.kind ||
                    (sample.page && current->ansi_code_page != sample.page) || workspace_.Find(current->id)->dirty ||
                    current->editor->Send(SCI_GETEOLMODE) != SC_EOL_LF) result = 152;
                if (result == 0) {
                    current->editor->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("!"));
                    const auto expected = notepad_colon::EncodeText(after + L"!", sample.kind, sample.page);
                    if (!SaveDocument(*current, false) || !expected || ReadFileBytes(path) != expected) result = 153;
                }
                if (!CloseActive(true)) result = 154;
            }
            if (result == 0) {
                const auto path = encoding_root / L"pinned.txt";
                if (!notepad_colon::WriteEncodedFileAtomic(path, L"initial\n", Kind::ansi, 1252) ||
                    !OpenPath(path, 1252)) result = 155;
                auto* current = result == 0 ? ActiveDocument() : nullptr;
                if (current) {
                    if (!notepad_colon::WriteEncodedFileAtomic(path, L"é\n", Kind::utf8)) result = 156;
                    CheckExternalChanges(true);
                    if (current->editor->GetText() != L"\u00c3\u00a9\n" || !current->legacy_encoding_override) result = 157;
                    ReopenActiveWithCodePage(0);
                    if (current->editor->GetText() != L"é\n" || current->legacy_encoding_override ||
                        current->detected_encoding != Kind::utf8) result = 158;
                    const std::vector<std::uint8_t> binary{0xef, 0xbb, 0xbf, 'x', 0, 'y'};
                    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(binary.data()),
                                                               static_cast<std::streamsize>(binary.size()));
                    CheckExternalChanges(true);
                    if (current->editor->GetText() != L"é\n" || !current->external_changed ||
                        SaveDocument(*current, false) || ReadFileBytes(path) != std::optional{binary}) result = 159;
                    if (!notepad_colon::WriteEncodedFileAtomic(path, L"repaired\n", Kind::utf8)) result = 160;
                    CheckExternalChanges(true);
                    if (current->editor->GetText() != L"repaired\n" || current->external_changed) result = 161;
                    std::ofstream(path, std::ios::binary).put(static_cast<char>(0x82));
                    ReopenActiveWithCodePage(932);
                    if (current->editor->GetText() != L"repaired\n" || current->legacy_encoding_override || current->read_only)
                        result = 162;
                    if (!CloseActive(true)) result = 163;
                }
            }
        }
        if (result == 0) {
            // EditorConfig is a fallback for decoding BOMless UTF-16, never
            // permission to drop a prefix or reinterpret a BOM-tagged file.
            using Kind = notepad_colon::EncodingKind;
            const auto root = test_workspace / L"format-config";
            std::filesystem::create_directories(root);
            const auto config = root / L".editorconfig";
            std::ofstream(config) << "root=true\n[*.txt]\ncharset=utf-8-bom\nend_of_line=lf\ninsert_final_newline=true\n";
            const auto file = root / L"plain.txt";
            if (!notepad_colon::WriteEncodedFileAtomic(file, L"abcdef", Kind::utf8) || !OpenPath(file)) result = 164;
            auto* plain = result == 0 ? ActiveDocument() : nullptr;
            if (plain) {
                if (plain->editor->GetText() != L"abcdef" || plain->detected_encoding != Kind::utf8 ||
                    plain->editor->Send(SCI_GETEOLMODE) != SC_EOL_LF || !SaveDocument(*plain, false) ||
                    ReadFileBytes(file) != notepad_colon::EncodeText(L"abcdef\n", Kind::utf8)) result = 165;
                if (!CloseActive(true)) result = 166;
            }
            if (result == 0 && (!notepad_colon::WriteEncodedFileAtomic(file, L"hello Ω\n", Kind::utf16_be) ||
                !OpenPath(file))) result = 167;
            auto* unicode = result == 0 ? ActiveDocument() : nullptr;
            if (unicode) {
                if (unicode->editor->GetText() != L"hello Ω\n" || unicode->detected_encoding != Kind::utf16_be ||
                    !SaveDocument(*unicode, false) ||
                    ReadFileBytes(file) != notepad_colon::EncodeText(L"hello Ω\n", Kind::utf16_be)) result = 168;
                if (!CloseActive(true)) result = 169;
            }
            std::ofstream(config) << "root=true\n[*.txt]\ncharset=utf-16le\nend_of_line=lf\n";
            if (result == 0 && (!notepad_colon::WriteEncodedFileAtomic(file, L"hello Ω\n", Kind::utf16_le, 0, {}, false) ||
                !OpenPath(file))) result = 170;
            auto* bomless = result == 0 ? ActiveDocument() : nullptr;
            std::optional<notepad_colon::SessionEntry> saved;
            if (bomless) {
                if (bomless->editor->GetText() != L"hello Ω\n" || bomless->unicode_bom || !SaveDocument(*bomless, false) ||
                    ReadFileBytes(file) != notepad_colon::EncodeText(L"hello Ω\n", Kind::utf16_le, 0, false)) result = 171;
                if (const auto captured = CaptureSession())
                    for (const auto& entry : captured->documents) if (entry.path == file) saved = entry;
                if (!CloseActive(true)) result = 172;
            }
            std::filesystem::remove(config);
            if (result == 0) {
                if (!saved || !RestoreSessionEntry(*saved)) result = 173;
                else {
                    auto* restored = ActiveDocument();
                    if (restored->unicode_bom || restored->detected_encoding != Kind::utf16_le ||
                        !SaveDocument(*restored, false) ||
                        ReadFileBytes(file) != notepad_colon::EncodeText(L"hello Ω\n", Kind::utf16_le, 0, false)) result = 174;
                    if (!CloseActive(true)) result = 175;
                }
            }
        }
        if (result == 0) {
            // Empty text is an edit, not a missing recovery payload. Exercise
            // the real capture/write/restore path with a non-default code page.
            const auto empty_file = test_workspace / L"empty-recovery.txt";
            if (!notepad_colon::WriteEncodedFileAtomic(empty_file, L"original text",
                    notepad_colon::EncodingKind::ansi, 932) || !OpenPath(empty_file)) result = 120;
            auto* emptied = result == 0 ? ActiveDocument() : nullptr;
            if (emptied) {
                emptied->detected_encoding = notepad_colon::EncodingKind::ansi;
                emptied->ansi_code_page = 932;
                emptied->editor->SetText(L"");
                workspace_.SetDirty(emptied->id, true);
                const auto captured = CaptureSession();
                notepad_colon::Session recovery;
                if (captured) {
                    for (const auto& entry : captured->documents)
                        if (entry.path == empty_file) recovery.documents.push_back(entry);
                }
                if (!session_writer_.Flush() || recovery.documents.size() != 1 ||
                    !notepad_colon::SaveSessionAtomic(session_path_, recovery)) result = 121;
                emptied->editor->SetText(L"stale disk content");
                emptied->ansi_code_page = 1252;
                if (!RestoreSession() || emptied->editor->GetText() != L"" ||
                    emptied->ansi_code_page != 932 || !workspace_.Find(emptied->id)->dirty)
                    result = 122;
                if (!notepad_colon::WriteEncodedFileAtomic(empty_file, L"later external edit",
                        notepad_colon::EncodingKind::ansi, 932) || !RestoreSession() ||
                    !emptied->external_changed || SaveDocument(*emptied, false)) result = 128;
                workspace_.Activate(emptied->id);
                if (!CloseActive(true)) result = 123;
            }
        }
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
    RemoveTestState();
    adapter_.Detach();
    ::PostQuitMessage(result);
}

void MainWindow::RunLargeFileSelfTest() noexcept {
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
    if (result == 0) {
        const auto original_id = document->id;
        const auto middle = document->large_buffer->Size() / 2;
        const auto tail = document->large_buffer->Size() - marker.size();
        const auto middle_bytes = document->large_buffer->Read(middle, marker.size());
        const auto tail_bytes = document->large_buffer->Read(tail, marker.size());
        const auto marker_bytes = std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(marker.data()), marker.size()};
        if (!document->large_buffer->Replace(middle, marker.size(), marker_bytes) ||
            !document->large_buffer->Replace(tail, marker.size(), marker_bytes)) result = 124;
        const auto captured = CaptureSession();
        const auto checkpoint_path = session_path_.wstring() + L".large-checkpoint";
        notepad_colon::Session recovered;
        if (!captured || !notepad_colon::SaveSessionAtomic(checkpoint_path, *captured) ||
            !notepad_colon::LoadSession(checkpoint_path, recovered) || recovered.documents.size() != 1 ||
            !RestoreSessionEntry(recovered.documents[0])) result = 125;
        auto* restored = ActiveDocument();
        if (result == 0 && (!restored || restored->id == original_id || !restored->large_buffer ||
            restored->large_buffer->Read(0, marker.size()) != std::vector<std::uint8_t>(marker_bytes.begin(), marker_bytes.end()) ||
            restored->large_buffer->Read(middle, marker.size()) != std::vector<std::uint8_t>(marker_bytes.begin(), marker_bytes.end()) ||
            restored->large_buffer->Read(tail, marker.size()) != std::vector<std::uint8_t>(marker_bytes.begin(), marker_bytes.end()) ||
            !workspace_.Find(restored->id)->dirty || !workspace_.Find(restored->id)->path.empty())) result = 126;
        if (restored && restored->id != original_id) static_cast<void>(CloseActive(true));
        workspace_.Activate(original_id);
        document = FindDocument(original_id);
        if (!document || !document->large_buffer->Replace(middle, marker.size(), middle_bytes) ||
            !document->large_buffer->Replace(tail, marker.size(), tail_bytes)) result = 127;
    }
    if (result == 0 && !SaveDocument(*document, false)) {
        result = 42;
        const auto* failed = workspace_.Find(document->id);
        if (failed) {
            const auto length = static_cast<std::size_t>(::SendMessageW(status_.GetHwnd(), SB_GETTEXTLENGTHW, 0, 0) & 0xffff);
            std::wstring message(length + 1, L'\0');
            ::SendMessageW(status_.GetHwnd(), SB_GETTEXTW, 0, reinterpret_cast<LPARAM>(message.data()));
            std::ofstream details(failed->path.wstring() + L".selftest-error", std::ios::binary);
            details << mwfl::ToUtf8(message).value_or("unreadable status") << "\nread_only=" << document->read_only
                << " external_changed=" << document->external_changed
                << " disk_state_equal=" << (document->file_state == notepad_colon::CaptureFileState(failed->path))
                << " encoding=" << static_cast<int>(document->detected_encoding)
                << " win32_error=" << ::GetLastError() << '\n';
        }
    }
    if (result == 0) {
        const auto prefix = document->large_buffer->Read(0, marker.size());
        if (std::string(prefix.begin(), prefix.end()) != marker ||
            document->large_buffer->IsModified() || workspace_.Find(document->id)->dirty)
            result = 43;
    }
    if (result == 0) {
        // Whole-file conversions must never mutate only the visible window.
        const auto size = document->large_buffer->Size();
        SetEncoding(mwfl::TextEncoding::utf16_le);
        SetAnsiEncoding(932);
        SetLineEnding(notepad_colon::LineEnding::lf);
        if (document->detected_encoding != notepad_colon::EncodingKind::utf8 ||
            document->large_buffer->Size() != size || workspace_.Find(document->id)->dirty ||
            commands_.Find(kUtf16Le)->IsEnabled()) result = 176;
        const auto path = workspace_.Find(document->id)->path;
        // Two scalars cross both the 8 MiB view boundary and its lookahead
        // boundary. Neither detection nor scrolling may turn them into ANSI.
        static_cast<void>(session_writer_.Flush());
        constexpr std::uint64_t boundary = 8u * 1024 * 1024;
        constexpr std::string_view scalars = "\xf0\x9f\x98\x80\xf0\x9f\x98\x80";
        const auto original = document->large_buffer->Read(boundary - 2, scalars.size());
        {
            std::fstream patch(path, std::ios::binary | std::ios::in | std::ios::out);
            patch.seekp(static_cast<std::streamoff>(boundary - 2));
            patch.write(scalars.data(), static_cast<std::streamsize>(scalars.size()));
            if (!patch) result = 185;
        }
        CheckExternalChanges(true);
        if (document->detected_encoding != notepad_colon::EncodingKind::utf8 || document->read_only) result = 186;
        MoveLargeFileWindow(true);
        if (!document->editor->GetText().value_or(L"").starts_with(L"😀😀") ||
            workspace_.Find(document->id)->dirty) result = 187;
        {
            std::fstream patch(path, std::ios::binary | std::ios::in | std::ios::out);
            patch.seekp(static_cast<std::streamoff>(boundary - 2));
            patch.write(reinterpret_cast<const char*>(original.data()), static_cast<std::streamsize>(original.size()));
            if (!patch) result = 188;
        }
        CheckExternalChanges(true);
        document->follow_tail = true;
        constexpr std::string_view appended = "extra-tail\n";
        std::ofstream(path, std::ios::binary | std::ios::app) << appended;
        CheckExternalChanges(true);
        const auto followed = document->editor->GetText();
        if (!document->large_buffer || document->large_buffer->Size() != size + appended.size() ||
            !followed || !followed->ends_with(L"extra-tail\n") || !document->follow_tail ||
            workspace_.Find(document->id)->dirty) result = 177;
        std::filesystem::resize_file(path, size);
        CheckExternalChanges(true);
        if (!document->large_buffer || document->large_buffer->Size() != size || !document->follow_tail) result = 178;
        const auto old_identity = document->file_state.file_index;
        const auto rotation = path.wstring() + L".rotation";
        if (!::CopyFileW(path.c_str(), rotation.c_str(), FALSE) ||
            !::ReplaceFileW(path.c_str(), rotation.c_str(), nullptr, 0, nullptr, nullptr)) result = 179;
        CheckExternalChanges(true);
        if (document->file_state.file_index == old_identity || !document->follow_tail) result = 180;
        const auto regrow = path.wstring() + L".regrow";
        if (!::CopyFileW(path.c_str(), regrow.c_str(), FALSE)) result = 181;
        std::filesystem::resize_file(path, 0);
        CheckExternalChanges(true);
        if (document->large_buffer || document->editor->GetText() != L"" || !document->follow_tail) result = 182;
        if (!::ReplaceFileW(path.c_str(), regrow.c_str(), nullptr, 0, nullptr, nullptr)) result = 183;
        CheckExternalChanges(true);
        if (!document->large_buffer || document->large_buffer->Size() != size || !document->follow_tail ||
            workspace_.Find(document->id)->dirty) result = 184;
    }
    StopWorkers();
    RemoveTestState();
    ::PostQuitMessage(result);
}

// Test modes keep their session, recovery and settings files in the temporary
// directory under the process id; leave nothing behind when the run ends.
void MainWindow::RemoveTestState() noexcept {
    if (!IsTestMode()) return;
    static_cast<void>(session_writer_.Flush());
    std::error_code ignored;
    for (const auto* path : {&session_path_, &macros_path_, &configuration_path_,
                             &workspace_catalog_path_})
        if (!path->empty()) {
            std::filesystem::remove(*path, ignored);
            std::filesystem::remove(path->wstring() + L".previous", ignored);
        }
    // All paths below are generated by this test under its process-specific prefix.
    std::filesystem::remove_all(session_path_.wstring() + L".data", ignored);
    std::filesystem::remove_all(ReplacementHistoryPath(), ignored);
    std::filesystem::remove(session_path_.wstring() + L".large-checkpoint", ignored);
    std::filesystem::remove(session_path_.wstring() + L".large-checkpoint.previous", ignored);
    std::filesystem::remove_all(session_path_.wstring() + L".large-checkpoint.data", ignored);
    std::filesystem::remove_all(std::filesystem::temp_directory_path() /
        (L"notepad-colon-gui-recovery-" + std::to_wstring(::GetCurrentProcessId())), ignored);
}

}  // namespace notepad_colon::app
