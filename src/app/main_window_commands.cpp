#include "main_window.h"

namespace notepad_colon::app {

void MainWindow::BuildCommands() {
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
            ShowQuickOpen(); }).SetShortcut({FVIRTKEY | FCONTROL, 'E'}))
        .Add(mwfl::Command(kPreviousSearch, L"Use Previous Search", [this] {
            if (search_history_.empty()) status_.SetText(Tr(L"Search history is empty"));
            else { search_.SetText(search_history_.front()); search_.Focus(); }
        }))
        .Add(mwfl::Command(kMarkAll, L"Mark All Matches", [this] { MarkAllSearchMatches(); }))
        .Add(mwfl::Command(kClearSearchMarks, L"Clear Search Marks", [this] {
            if (auto* editor = ActiveEditor()) notepad_colon::ClearSearchMarks(*editor);
        }))
        .Add(mwfl::Command(kFollowTail, L"Follow Growing File", [this] {
            auto* document = ActiveDocument();
            if (document && document->follow_tail) {
                document->follow_tail = false;
                status_.SetText(Tr(L"Follow mode stopped")); return;
            }
            const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
            if (!document || !metadata || metadata->path.empty() || metadata->dirty || document->read_only ||
                (document->detected_encoding != notepad_colon::EncodingKind::utf8 &&
                 document->detected_encoding != notepad_colon::EncodingKind::utf8_bom)) {
                status_.SetText(Tr(L"Follow mode requires a saved, unmodified UTF-8 file")); return;
            }
            document->follow_tail = true;
            // A growing log is exactly where highlighted levels help most.
            if (document->follow_tail) document->highlight_lines = true;
            status_.SetText(Tr(L"Following appended data"));
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
        .Add(mwfl::Command(kReopenAutomatic, L"Reopen with Automatic Encoding", [this] { ReopenActiveWithCodePage(0); }))
        .Add(mwfl::Command(kSaveSystemAnsi, L"Convert to System ANSI", [this] { SetAnsiEncoding(::GetACP()); }));
    {
        WORD index = 0;
        for (const auto& entry : notepad_colon::SupportedLegacyCodePages()) {
            const auto code_page = entry.code_page;
            commands_
                .Add(mwfl::Command({static_cast<WORD>(kReopenCodePageBase.value + index)},
                    L"Reopen as " + std::wstring{entry.name},
                    [this, code_page] { ReopenActiveWithCodePage(code_page); }))
                .Add(mwfl::Command({static_cast<WORD>(kConvertCodePageBase.value + index)},
                    L"Convert to " + std::wstring{entry.name},
                    [this, code_page] { SetAnsiEncoding(code_page); }));
            ++index;
        }
    }
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
        .Add(mwfl::Command(kAddShellMenus, L"Add Explorer Context Menu",
            [this] { ReportShellMenus(true); }))
        .Add(mwfl::Command(kRemoveShellMenus, L"Remove Explorer Context Menu",
            [this] { ReportShellMenus(false); }))
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
                Tr(L"Notepad:: 1.3.1\nNative everyday code editing with MWFL and Scintilla.").c_str(),
                Tr(L"About Notepad::").c_str(), MB_OK | MB_ICONINFORMATION); }));
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
        .Add(mwfl::Command(kReplaceInFiles, L"Replace in Files...", [this] { StartReplaceInFiles(); })
                 .SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, 'H'}))
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
        .Add(mwfl::Command(kRainbowColumns, L"Rainbow Columns (CSV / TSV)", [this] { ToggleRainbowColumns(); }))
        .Add(mwfl::Command(kExtractColumn, L"Extract Column to New Tab", [this] { StartColumnExtract(); }))
        .Add(mwfl::Command(kSortColumnAscending, L"Sort Rows by Column (Ascending)", [this] { SortByColumn(false); }))
        .Add(mwfl::Command(kSortColumnDescending, L"Sort Rows by Column (Descending)", [this] { SortByColumn(true); }))
        .Add(mwfl::Command(kGoToSourceLine, L"Go to Source Line of Filter Result", [this] { GoToFilterSource(); })
                 .SetShortcut({FVIRTKEY, VK_F4}))
        .Add(mwfl::Command(kFilterLines, L"Filter Lines...", [this] { ShowFilterLines(); })
                 .SetShortcut({FVIRTKEY | FCONTROL | FSHIFT, 'G'}))
        .Add(mwfl::Command(kHighlightLines, L"Highlight Log Lines", [this] { ToggleHighlightLines(); }))
        .Add(mwfl::Command(kHighlightRules, L"Highlight Rules...", [this] { ShowHighlightRules(); }))
        .Add(mwfl::Command(kReviewChanges, L"Review External Changes...", [this] { ReviewExternalChanges(); })
                 .SetShortcut({FVIRTKEY, VK_F8}));
    commands_
        .Add(mwfl::Command(kSplitView, L"Split View (Clone Document)", [this] { ToggleSplitView(); })
                 .SetShortcut({FVIRTKEY | FCONTROL, VK_OEM_5}))
        .Add(mwfl::Command(kRotateSplit, L"Rotate Split (Side by Side / Stacked)", [this] { RotateSplitView(); }))
        .Add(mwfl::Command(kFocusOtherView, L"Focus Other View", [this] { FocusOtherView(); })
                 .SetShortcut({FVIRTKEY, VK_F6}));
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
    RememberCommandTitles();
}

void MainWindow::AddLanguageCommands() {
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

// Commands are created with their English titles. Remember those once so the
// interface language can be switched back and forth without losing them.
void MainWindow::RememberCommandTitles() {
    for (const auto& command : commands_.GetCommands()) {
        const auto id = static_cast<WORD>(command.GetId().value);
        const bool dynamic =
            (id >= kRecentBase.value &&
             id < kRecentBase.value + static_cast<WORD>(recent_.GetMaximumEntries())) ||
            id >= kLanguageBase.value;  // languages and code pages are composed separately
        if (!dynamic) command_titles_.try_emplace(id, CommandLabel(command, true));
    }
}

void MainWindow::ApplyUiLanguage() {
    notepad_colon::SetUiLanguage(chinese_ui_ ? notepad_colon::UiLanguage::chinese_simplified
                                             : notepad_colon::UiLanguage::english);
    for (const auto& [id, english] : command_titles_)
        if (auto* command = commands_.Find(mwfl::ControlId{id})) command->SetText(Tr(english));
    // Code-page commands are composed from a verb and the code-page name.
    WORD index = 0;
    for (const auto& entry : notepad_colon::SupportedLegacyCodePages()) {
        if (auto* command = commands_.Find({static_cast<WORD>(kReopenCodePageBase.value + index)}))
            command->SetText(Tr(L"Reopen as ") + Tr(entry.name));
        if (auto* command = commands_.Find({static_cast<WORD>(kConvertCodePageBase.value + index)}))
            command->SetText(Tr(L"Convert to ") + Tr(entry.name));
        ++index;
    }
    if (auto* command = commands_.Find({static_cast<WORD>(kLanguageBase.value)}))
        command->SetText(Tr(L"Plain Text"));
    RefreshShortcutLabels();
}

// Menus show a command's shortcut after a tab. The label is rebuilt whenever
// the titles or the bindings change.
void MainWindow::RefreshShortcutLabels() {
    std::vector<mwfl::ControlId> ids;
    for (const auto& command : commands_.GetCommands()) ids.push_back(command.GetId());
    for (const auto id : ids) {
        auto* command = commands_.Find(id);
        if (!command) continue;
        std::wstring text = CommandLabel(*command, true);
        if (const auto& shortcut = command->GetShortcut(); shortcut && shortcut->key) {
            text += L'\t';
            text += notepad_colon::FormatShortcut({static_cast<std::uint16_t>(id.value),
                static_cast<std::uint8_t>(shortcut->modifiers), static_cast<std::uint16_t>(shortcut->key)});
        }
        if (text != command->GetText()) command->SetText(std::move(text));
    }
}

// A command's title without its shortcut suffix and, unless asked to keep
// them, without menu mnemonic markers.
std::wstring MainWindow::CommandLabel(const mwfl::Command& command, bool keep_mnemonic) {
    std::wstring label{command.GetText()};
    if (const auto tab = label.find(L'\t'); tab != std::wstring::npos) label.resize(tab);
    if (!keep_mnemonic) std::erase(label, L'&');
    return label;
}

void MainWindow::SetUiLanguage(bool chinese) {
    if (chinese_ui_ == chinese) return;
    chinese_ui_ = chinese; ApplyUiLanguage(); BuildMenu();
    if (!IsTestMode()) static_cast<void>(SavePreferences());
    SyncPresentation(Tr(L"Interface language changed"));
}

void MainWindow::BuildMenu() {
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
                          kMarkAll, kClearSearchMarks, kFindInFiles, kReplaceInFiles, kCancelSearch})
        mwfl::Must(search.AppendCommand(*commands_.Find(id)), "append search command");
    for (const auto id : {kUtf8, kUtf8Bom, kUtf16Le, kUtf16Be, kSaveSystemAnsi})
        mwfl::Must(encoding.AppendCommand(*commands_.Find(id)), "append encoding command");
    for (WORD index = 0; index < notepad_colon::SupportedLegacyCodePages().size(); ++index)
        mwfl::Must(encoding.AppendCommand(*commands_.Find(
            {static_cast<WORD>(kConvertCodePageBase.value + index)})), "append convert command");
    mwfl::Must(encoding.AppendSeparator(), "append encoding separator");
    mwfl::Must(encoding.AppendCommand(*commands_.Find(kReopenAutomatic)), "append automatic reopen command");
    mwfl::Must(encoding.AppendCommand(*commands_.Find(kReopenSystemAnsi)), "append reopen command");
    for (WORD index = 0; index < notepad_colon::SupportedLegacyCodePages().size(); ++index)
        mwfl::Must(encoding.AppendCommand(*commands_.Find(
            {static_cast<WORD>(kReopenCodePageBase.value + index)})), "append reopen command");
    mwfl::Must(encoding.AppendSeparator(), "append encoding separator");
    mwfl::Must(encoding.AppendCommand(*commands_.Find(kEncodingInfo)), "append encoding information");
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
                          kSplitView, kRotateSplit, kFocusOtherView, kWhitespace, kWordWrap, kZoomIn, kZoomOut, kZoomReset,
                          kRectangular, kToggleFold, kToggleBookmark, kNextBookmark,
                          kFollowTail, kRainbowColumns, kExtractColumn, kSortColumnAscending,
                          kSortColumnDescending, kFilterLines, kGoToSourceLine, kHighlightLines,
                          kHighlightRules, kRefreshGitChanges})
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
                          kCompareWithDisk, kReviewChanges, kRegisterAssociation, kRemoveAssociation,
                          kAddShellMenus, kRemoveShellMenus,
                          kEnglishUi, kChineseUi, kOpenTerminal})
        mwfl::Must(tools.AppendCommand(*commands_.Find(id)), "append tools command");
    mwfl::Must(help.AppendCommand(*commands_.Find(kAbout)), "append about command");
    mwfl::Must(menu_.AppendSubmenu(std::move(file), Tr(L"&File")), "append file menu");
    mwfl::Must(menu_.AppendSubmenu(std::move(edit), Tr(L"&Edit")), "append edit menu");
    mwfl::Must(menu_.AppendSubmenu(std::move(search), Tr(L"&Search")), "append search menu");
    mwfl::Must(menu_.AppendSubmenu(std::move(encoding), Tr(L"En&coding")), "append encoding menu");
    mwfl::Must(menu_.AppendSubmenu(std::move(line_endings), Tr(L"&EOL")), "append line endings menu");
    mwfl::Must(menu_.AppendSubmenu(std::move(language), Tr(L"&Language")), "append language menu");
    mwfl::Must(menu_.AppendSubmenu(std::move(view), Tr(L"&View")), "append view menu");
    mwfl::Must(menu_.AppendSubmenu(std::move(code), Tr(L"&Code")), "append code menu");
    mwfl::Must(menu_.AppendSubmenu(std::move(tools), Tr(L"&Tools")), "append tools menu");
    mwfl::Must(menu_.AppendSubmenu(std::move(help), Tr(L"&Help")), "append help menu");
    mwfl::Must(menu_.AttachToWindow(GetHwnd()), "attach menu");
}

bool MainWindow::IsMacroSafeCommand(mwfl::ControlId id) noexcept {
    return (id.value >= kUndo.value && id.value <= kLf.value) ||
           (id.value >= kToggleFold.value && id.value <= kZoomReset.value) ||
           (id.value >= kSelectNext.value && id.value <= kInsertSequence.value);
}

void MainWindow::RememberCommand(mwfl::ControlId id) {
    std::erase(recent_commands_, id);
    recent_commands_.insert(recent_commands_.begin(), id);
    if (recent_commands_.size() > 20) recent_commands_.resize(20);
}

void MainWindow::ShowCommandPalette() {
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
            auto label = CommandLabel(*command, false);
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
        hint.SetText(std::to_wstring(visible.size()) + Tr(L" command(s)  •  recent first"));
    };
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Command Palette"),
        .initial_client_size = {560.0_dip, 430.0_dip}, .resizable = true,
        .callbacks = {
            .initialize = [&](HWND window) {
                mwfl::ControlHost ui{window}; ui.Add(query, {701}, L""); ui.Add(list, {702}, {});
                ui.Add(hint, {703}, L""); ui.Add(run, {IDOK}, Tr(L"Run")); ui.Add(cancel, {IDCANCEL}, Tr(L"Cancel"));
                mwfl::SetAccessibleName(query.GetHwnd(), Tr(L"Filter commands").c_str());
                mwfl::SetAccessibleName(list.GetHwnd(), Tr(L"Available commands").c_str());
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

void MainWindow::StartMacroRecording() {
    macro_recorder_.Start();
    status_.SetText(Tr(L"Macro recording started — editor commands and typed text only"));
}

void MainWindow::StopMacroRecording() {
    if (!macro_recorder_.IsRecording()) { status_.SetText(Tr(L"No macro recording is active")); return; }
    last_macro_ = macro_recorder_.Stop();
    status_.SetText(Tr(L"Macro stopped — ") + std::to_wstring(last_macro_.size()) + Tr(L" action(s)"));
}

void MainWindow::PlayMacro(const std::vector<notepad_colon::MacroAction>& actions, std::size_t repeats) {
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
    status_.SetText(Tr(L"Macro played ") + std::to_wstring(repeats) + Tr(L" time(s)"));
}

void MainWindow::SaveLastMacro() {
    if (last_macro_.empty()) { status_.SetText(Tr(L"Record a macro before saving it")); return; }
    mwfl::TextBox name; mwfl::Button save, cancel; std::wstring chosen_name;
    mwfl::Dialog* pointer = nullptr;
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Save Macro"),
        .initial_client_size = {420.0_dip, 110.0_dip}, .resizable = false,
        .callbacks = {
            .initialize = [&](HWND window) {
                mwfl::ControlHost ui{window}; ui.Add(name, {711}, L"");
                ui.Add(save, {IDOK}, Tr(L"Save")); ui.Add(cancel, {IDCANCEL}, Tr(L"Cancel"));
                mwfl::SetAccessibleName(name.GetHwnd(), Tr(L"Macro name").c_str());
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
    status_.SetText(Tr(L"Macro saved: ") + chosen_name);
}

void MainWindow::ShowMacroManager() {
    mwfl::ListBox list; mwfl::Button play, remove, close; mwfl::Dialog* pointer = nullptr;
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Macros"),
        .initial_client_size = {520.0_dip, 330.0_dip}, .resizable = true,
        .callbacks = {
            .initialize = [&](HWND window) {
                mwfl::ControlHost ui{window}; ui.Add(list, {721}, {});
                ui.Add(play, {722}, Tr(L"Play")); ui.Add(remove, {723}, Tr(L"Delete")); ui.Add(close, {IDCANCEL}, Tr(L"Close"));
                for (const auto& macro : saved_macros_)
                    list.AddItem(macro.name + L"  (" + std::to_wstring(macro.actions.size()) + Tr(L" actions)"));
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

std::vector<notepad_colon::ShortcutBinding> MainWindow::CaptureShortcuts() const {
    std::vector<notepad_colon::ShortcutBinding> result;
    for (const auto& command : commands_.GetCommands()) {
        if (!command.GetShortcut()) continue;
        result.push_back({static_cast<std::uint16_t>(command.GetId().value),
            command.GetShortcut()->modifiers, command.GetShortcut()->key});
    }
    return result;
}

bool MainWindow::ApplyShortcuts(const std::vector<notepad_colon::ShortcutBinding>& bindings,
                    bool persist) {
    if (!notepad_colon::FindShortcutConflicts(bindings).empty()) return false;
    for (const auto& command : commands_.GetCommands())
        if (auto* mutable_command = commands_.Find(command.GetId())) mutable_command->ClearShortcut();
    for (const auto& binding : bindings)
        if (auto* command = commands_.Find({binding.command_id}); binding.key && command)
            command->SetShortcut({binding.modifiers, binding.key});
    if (!accelerators_.Create(commands_)) return false;
    RefreshShortcutLabels();
    SetAccelerators(accelerators_.GetHandle()); BuildMenu();
    if (persist) static_cast<void>(SaveConfigurationFile(configuration_path_));
    return true;
}

bool MainWindow::SaveConfigurationFile(const std::filesystem::path& path) const {
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

void MainWindow::ShowShortcutSettings() {
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
            auto label = CommandLabel(*command, false);
            const auto found = std::ranges::find(bindings, static_cast<std::uint16_t>(id.value),
                                                  &notepad_colon::ShortcutBinding::command_id);
            list.AddItem(label + L"    " + (found == bindings.end() ? L"" : notepad_colon::FormatShortcut(*found)));
        }
        if (!ids.empty() && list.GetSelection() == LB_ERR) list.SetSelection(0);
        diagnostic.SetText(notepad_colon::FindShortcutConflicts(bindings).empty()
            ? Tr(L"No shortcut conflicts") : Tr(L"Resolve duplicate shortcuts before applying"));
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
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Keyboard Shortcuts"),
        .initial_client_size = {650.0_dip, 460.0_dip}, .resizable = true,
        .callbacks = {
            .initialize = [&](HWND window) {
                mwfl::ControlHost ui{window}; ui.Add(list, {731}, {}); ui.Add(shortcut, {732}, L"");
                ui.Add(diagnostic, {733}, L""); ui.Add(apply, {IDOK}, Tr(L"Apply"));
                ui.Add(defaults, {734}, Tr(L"Restore defaults")); ui.Add(close, {IDCANCEL}, Tr(L"Close"));
                mwfl::SetAccessibleName(list.GetHwnd(), Tr(L"Commands and shortcuts").c_str());
                mwfl::SetAccessibleName(shortcut.GetHwnd(), Tr(L"Shortcut such as Ctrl Shift K").c_str());
                const auto layout = pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(6.0_dip)
                    .Add(list, mwfl::Stretch()).Add(shortcut, mwfl::Fixed(30.0_dip))
                    .Add(mwfl::Row().Gap(6.0_dip).Add(diagnostic, mwfl::Stretch())
                        .Add(defaults, ButtonWidth(Tr(L"Restore defaults"))).Add(apply, mwfl::Fixed(80.0_dip))
                        .Add(close, mwfl::Fixed(80.0_dip)), mwfl::Fixed(30.0_dip)));
                refresh(); load_selected(); return layout;
            },
            .command = [&](HWND, WORD id, WORD notification) {
                if (id == 731 && notification == LBN_SELCHANGE) { load_selected(); return true; }
                if (id == 734) { bindings = default_shortcuts_; refresh(); load_selected(); return true; }
                if (id == IDOK) {
                    if (!store_selected() || !notepad_colon::FindShortcutConflicts(bindings).empty()) {
                        ::MessageBoxW(pointer->GetHwnd(), Tr(L"Enter a valid, unique shortcut (for example Ctrl+Shift+K), or leave it blank.").c_str(),
                                      Tr(L"Keyboard Shortcuts").c_str(), MB_OK | MB_ICONWARNING); return true;
                    }
                    if (ApplyShortcuts(bindings, true)) pointer->Accept(); return true;
                }
                return false;
            }}});
    pointer = &dialog; static_cast<void>(dialog.ShowModal());
}

void MainWindow::ExportConfiguration() {
    const auto selected = mwfl::ShowSaveFileDialog({.owner = GetHwnd(), .title = Tr(L"Export settings"),
        .filters = {{Tr(L"Notepad:: configuration"), L"*.npcconfig"}},
        .default_extension = L"npcconfig", .path_must_exist = false});
    if (selected.accepted)
        status_.SetText(SaveConfigurationFile(selected.path) ? Tr(L"Settings exported") : Tr(L"Settings export failed"));
}

void MainWindow::ImportConfiguration() {
    const auto selected = mwfl::ShowOpenFileDialog({.owner = GetHwnd(), .title = Tr(L"Import settings"),
        .filters = {{Tr(L"Notepad:: configuration"), L"*.npcconfig"}, {Tr(L"All files"), L"*.*"}}});
    if (!selected.accepted) return;
    std::ifstream input(selected.path, std::ios::binary); std::string encoded{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    notepad_colon::Configuration configuration;
    if ((!input && encoded.empty()) || !notepad_colon::DeserializeConfiguration(encoded, configuration) ||
        !ApplyShortcuts(configuration.shortcuts, false)) {
        status_.SetText(Tr(L"Settings import rejected: invalid or conflicting configuration")); return;
    }
    preferences_ = configuration.preferences; ApplyAppearance();
    search_history_ = configuration.search_history;
    if (!IsTestMode()) static_cast<void>(SavePreferences());
    static_cast<void>(SaveConfigurationFile(configuration_path_));
    status_.SetText(Tr(L"Settings and shortcuts imported"));
}

}  // namespace notepad_colon::app
