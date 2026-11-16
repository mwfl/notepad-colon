#include "main_window.h"

namespace notepad_colon::app {

void MainWindow::CompareWithFile() {
    const auto* document = ActiveDocument();
    if (!document) return;
    if (document->large_buffer) {
        status_.SetText(Tr(L"A complete comparison is unavailable for this file; local edits were retained. Use Save As")); return;
    }
    const auto left = document->editor->GetText();
    if (!left) return;
    const auto selected = mwfl::ShowOpenFileDialog({
        .owner = GetHwnd(), .title = Tr(L"Choose file to compare"),
        .filters = {{Tr(L"Text and source files"), L"*.txt;*.md;*.cpp;*.h;*.json;*.xml;*.ini;*.yaml;*.yml;*.ps1"},
                    {Tr(L"All files"), L"*.*"}}});
    if (!selected.accepted) return;
    const auto loaded = notepad_colon::ReadEncodedFileText(selected.path, notepad_colon::LargeFilePolicy{}.editable_limit,
        0, {}, notepad_colon::ResolveEditorConfig(selected.path).encoding);
    if (!loaded) { status_.SetText(Tr(L"Comparison file could not be opened")); return; }
    ShowComparison(selected.path.filename().wstring(), *left, loaded->text);
}

void MainWindow::CompareWithDisk() {
    const auto* document = ActiveDocument();
    const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
    if (!document || !metadata || metadata->path.empty()) {
        status_.SetText(Tr(L"Save the document before comparing with its saved version"));
        return;
    }
    if (document->large_buffer) {
        status_.SetText(Tr(L"A complete comparison is unavailable for this file; local edits were retained. Use Save As")); return;
    }
    const auto left = document->editor->GetText();
    const auto loaded = notepad_colon::ReadEncodedFileText(metadata->path, notepad_colon::LargeFilePolicy{}.editable_limit,
        document->detected_encoding == notepad_colon::EncodingKind::ansi ? document->ansi_code_page : 0,
        document->legacy_encoding_override ? std::optional{notepad_colon::EncodingKind::ansi} : std::nullopt,
        document->detected_encoding);
    if (!left || !loaded) { status_.SetText(Tr(L"Saved version could not be read")); return; }
    ShowComparison(Tr(L"Saved — ") + metadata->title, *left, loaded->text);
}

void MainWindow::ShowComparison(std::wstring right_title, std::wstring left_text,
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
        summary.SetText(comparison.identical ? Tr(L"Files are identical") :
            std::to_wstring(comparison.changed_lines) + Tr(L" changed line(s)  •  Difference ") +
            std::to_wstring(current + 1) + Tr(L" of ") + std::to_wstring(changes.size()));
    };
    const auto go_to_current = [&] {
        if (changes.empty()) return;
        const auto& difference = comparison.lines[changes[current]];
        if (difference.left_line) left.Send(SCI_GOTOLINE, difference.left_line - 1);
        if (difference.right_line) right.Send(SCI_GOTOLINE, difference.right_line - 1);
        summary.SetText(std::to_wstring(comparison.changed_lines) + Tr(L" changed line(s)  •  Difference ") +
            std::to_wstring(current + 1) + Tr(L" of ") + std::to_wstring(changes.size()));
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
        .owner = GetHwnd(), .title = Tr(L"Compare Text"),
        .initial_client_size = {980.0_dip, 620.0_dip}, .resizable = true,
        .callbacks = {
            .initialize = [&](HWND window) {
                mwfl::ControlHost ui{window};
                ui.Add(left_label, {601}, Tr(L"Current document"));
                ui.Add(right_label, {602}, right_title);
                ui.Add(summary, {603}, L"");
                ui.Add(ignore_case, {604}, Tr(L"Ignore case"));
                ui.Add(ignore_whitespace, {605}, Tr(L"Ignore whitespace"));
                ui.Add(ignore_eol, {612}, Tr(L"Ignore EOL"));
                ignore_eol.SetChecked(true);
                ui.Add(previous, {606}, Tr(L"Previous")); ui.Add(next, {607}, Tr(L"Next"));
                ui.Add(copy_left, {608}, Tr(L"← Use right"));
                ui.Add(copy_right, {609}, Tr(L"Use left →"));
                ui.Add(apply, {IDOK}, Tr(L"Apply current side")); ui.Add(close, {IDCANCEL}, Tr(L"Close"));
                if (!left.Create(window, {610}, {}, runtime_) ||
                    !right.Create(window, {611}, {}, runtime_)) return false;
                left.ConfigureCodeEditing(); right.ConfigureCodeEditing();
                notepad_colon::ApplyPreferences(left, preferences_, IsDark());
                notepad_colon::ApplyPreferences(right, preferences_, IsDark());
                notepad_colon::SetEditorTextBaseline(left, left_text);
                notepad_colon::SetEditorTextBaseline(right, right_text);
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
                mwfl::SetAccessibleName(left.GetHwnd(), Tr(L"Current document comparison side").c_str());
                mwfl::SetAccessibleName(right.GetHwnd(), Tr(L"Comparison file side").c_str());
                const auto layout = pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(5.0_dip)
                    .Add(mwfl::Row().Gap(6.0_dip).Add(left_label, mwfl::Stretch())
                        .Add(right_label, mwfl::Stretch()), mwfl::Fixed(24.0_dip))
                    .Add(mwfl::Row().Gap(5.0_dip).Add(left, mwfl::Stretch())
                        .Add(right, mwfl::Stretch()), mwfl::Stretch())
                    .Add(mwfl::Row().Gap(6.0_dip).Add(summary, mwfl::Stretch())
                        .Add(ignore_case, mwfl::Auto()).Add(ignore_whitespace, mwfl::Auto())
                        .Add(ignore_eol, mwfl::Auto()),
                        mwfl::Fixed(28.0_dip))
                    .Add(mwfl::Row().Gap(6.0_dip).Add(previous, ButtonWidth(Tr(L"Previous"))).Add(next, ButtonWidth(Tr(L"Next")))
                        .Add(copy_left, ButtonWidth(Tr(L"← Use right"))).Add(copy_right, ButtonWidth(Tr(L"Use left →")))
                        .Add(mwfl::Column(), mwfl::Stretch()).Add(apply, ButtonWidth(Tr(L"Apply current side")))
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
        SyncPresentation(Tr(L"Comparison changes applied"));
    }
}

std::optional<std::wstring> MainWindow::ActiveSelectionText() const {
    const auto* document = const_cast<MainWindow*>(this)->ActiveDocument();
    if (!document) return std::nullopt;
    const auto selection = document->editor->GetSelection();
    if (selection.start == selection.end) return std::wstring{};
    std::string utf8(static_cast<std::size_t>(selection.end - selection.start) + 1, '\0');
    document->editor->Send(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(utf8.data()));
    utf8.resize(static_cast<std::size_t>(selection.end - selection.start));
    return mwfl::FromUtf8(utf8);
}

void MainWindow::ShowDocumentStatistics() {
    const auto* document = ActiveDocument(); if (!document) return;
    const auto text = document->editor->GetText(); if (!text) return;
    const auto stats = notepad_colon::CalculateStatistics(*text);
    const auto selection = ActiveSelectionText();
    const auto selection_stats = selection ? notepad_colon::CalculateStatistics(*selection)
                                           : notepad_colon::DocumentStatistics{};
    mwfl::Label values; mwfl::Button close; mwfl::Dialog* pointer = nullptr;
    const auto row = [](const std::wstring& label, std::size_t value) {
        return label + L":  " + std::to_wstring(value);
    };
    const auto content = row(Tr(L"Characters"), stats.characters) +
        L"\n" + row(Tr(L"Characters (no spaces)"), stats.characters_without_whitespace) +
        L"\n" + row(Tr(L"Words"), stats.words) +
        L"\n" + row(Tr(L"Lines"), stats.lines) +
        L"\n" + row(Tr(L"Non-blank lines"), stats.non_blank_lines) +
        L"\n" + row(Tr(L"UTF-8 bytes"), stats.utf8_bytes) +
        L"\n\n" + row(Tr(L"Selected characters"), selection_stats.characters) +
        L"\n" + row(Tr(L"Selected words"), selection_stats.words);
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Document Statistics"),
        .initial_client_size = {410.0_dip, 300.0_dip}, .resizable = false,
        .callbacks = {.initialize = [&](HWND window) {
            mwfl::ControlHost ui{window}; ui.Add(values, {741}, content); ui.Add(close, {IDOK}, Tr(L"Close"));
            return pointer->SetLayout(mwfl::Column().Margin(14.0_dip).Gap(8.0_dip)
                .Add(values, mwfl::Stretch())
                .Add(mwfl::Row().Add(mwfl::Column(), mwfl::Stretch()).Add(close, mwfl::Fixed(86.0_dip)),
                     mwfl::Fixed(30.0_dip)));
        }}});
    pointer = &dialog; static_cast<void>(dialog.ShowModal());
}

void MainWindow::ExportDocument(bool html) {
    const auto* document = ActiveDocument();
    const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
    const auto text = document ? document->editor->GetText() : std::nullopt;
    if (!document || !metadata || !text) return;
    const auto selected = mwfl::ShowSaveFileDialog({.owner = GetHwnd(),
        .title = html ? Tr(L"Export HTML") : Tr(L"Export plain text"),
        .filters = html ? std::vector<mwfl::FileDialogOptions::Filter>{{Tr(L"HTML document"), L"*.html;*.htm"}}
                        : std::vector<mwfl::FileDialogOptions::Filter>{{Tr(L"Text file"), L"*.txt"}},
        .default_extension = html ? L"html" : L"txt", .path_must_exist = false});
    if (!selected.accepted) return;
    const auto output = html ? notepad_colon::ExportHtmlDocument(metadata->title, *text, IsDark()) : *text;
    const auto written = mwfl::WriteTextFileAtomic(selected.path, output, mwfl::TextEncoding::utf8);
    status_.SetText(written.Succeeded() ? (html ? Tr(L"HTML exported") : Tr(L"Plain text exported"))
                                       : Tr(L"Export failed"));
}

void MainWindow::ShowPageSetup() {
    auto candidate = print_options_;
    mwfl::Label margin_label; mwfl::TextBox margin;
    mwfl::CheckBox header, footer, line_numbers, colours;
    mwfl::Button save, cancel; mwfl::Dialog* pointer = nullptr;
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Page Setup"),
        .initial_client_size = {470.0_dip, 285.0_dip}, .resizable = false,
        .callbacks = {
            .initialize = [&](HWND window) {
                mwfl::ControlHost ui{window}; ui.Add(margin_label, {761}, Tr(L"Margins (inches)"));
                ui.Add(margin, {762}, std::to_wstring(candidate.margin_inches));
                ui.Add(header, {763}, Tr(L"Print document title in header"));
                ui.Add(footer, {764}, Tr(L"Print page number in footer"));
                ui.Add(line_numbers, {765}, Tr(L"Print line numbers"));
                ui.Add(colours, {766}, Tr(L"Print syntax colours on white paper"));
                ui.Add(save, {IDOK}, Tr(L"Save")); ui.Add(cancel, {IDCANCEL}, Tr(L"Cancel"));
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
                        ::MessageBoxW(pointer->GetHwnd(), Tr(L"Margins must be between 0.1 and 2.0 inches.").c_str(),
                                      Tr(L"Page Setup").c_str(), MB_OK | MB_ICONWARNING); return true;
                    }
                    candidate = {value, header.IsChecked(), footer.IsChecked(),
                                 line_numbers.IsChecked(), colours.IsChecked()};
                }
                return false;
            }}});
    pointer = &dialog;
    if (dialog.ShowModal()) { print_options_ = candidate; status_.SetText(Tr(L"Page setup updated")); }
}

void MainWindow::ConfigurePrinter() {
    mwfl::PrinterSettings initial;
    if (printer_settings_.IsValid()) initial = printer_settings_.Clone();
    auto result = mwfl::ShowPrintDialog(GetHwnd(), std::move(initial), PD_NOSELECTION);
    if (result.action == mwfl::PrintDialogAction::accepted) {
        printer_settings_ = std::move(result.settings);
        status_.SetText(Tr(L"Printer settings updated: ") + printer_settings_.PrinterName());
    } else if (result.action == mwfl::PrintDialogAction::cancelled)
        status_.SetText(Tr(L"Printer settings cancelled"));
    else status_.SetText(Tr(L"No printer is available or the printer dialog failed"));
}

std::vector<mwfl::PrintPage> MainWindow::PaginateForPrinter(mwfl::ScintillaEditor& editor, HDC dc,
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

void MainWindow::PrintActive() {
    auto* document = ActiveDocument(); const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
    if (!document || !metadata) return;
    mwfl::PrinterSettings initial;
    if (printer_settings_.IsValid()) initial = printer_settings_.Clone();
    const auto selection = document->editor->GetSelection();
    auto dialog = mwfl::ShowPrintDialog(GetHwnd(), std::move(initial),
        selection.start == selection.end ? PD_NOSELECTION : PD_SELECTION);
    if (dialog.action == mwfl::PrintDialogAction::cancelled) { status_.SetText(Tr(L"Printing cancelled")); return; }
    if (dialog.action != mwfl::PrintDialogAction::accepted) { status_.SetText(Tr(L"No printer is available")); return; }
    printer_settings_ = std::move(dialog.settings);
    auto backend = mwfl::CreatePrinterBackend(printer_settings_);
    if (!backend) { status_.SetText(Tr(L"Printer device could not be opened")); return; }
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
        status_.SetText(Tr(L"Nothing to print")); return;
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
                const auto footer_text = Tr(L"Page ") + std::to_wstring(page.index + 1) + Tr(L" of ") + std::to_wstring(pages.size());
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
    status_.SetText(result == mwfl::PrintOperationStatus::success ? Tr(L"Document printed") :
        result == mwfl::PrintOperationStatus::cancelled ? Tr(L"Printing cancelled safely") : Tr(L"Printing failed"));
}

void MainWindow::ShowPrintPreview() {
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
    const auto refresh = [&] { preview.SetReadOnly(false); notepad_colon::SetEditorTextBaseline(preview, pages[current]); preview.SetReadOnly(true);
        page_label.SetText(metadata->title + Tr(L"  •  Page ") + std::to_wstring(current + 1) + Tr(L" of ") + std::to_wstring(pages.size())); };
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Print Preview"),
        .initial_client_size = {760.0_dip, 700.0_dip}, .resizable = true,
        .callbacks = {
            .initialize = [&](HWND window) {
                mwfl::ControlHost ui{window}; ui.Add(page_label, {751}, L"");
                ui.Add(previous, {752}, Tr(L"Previous")); ui.Add(next, {753}, Tr(L"Next"));
                ui.Add(print, {754}, Tr(L"Print...")); ui.Add(close, {IDCANCEL}, Tr(L"Close"));
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

void MainWindow::InsertDateTime() {
    SYSTEMTIME time{};
    ::GetLocalTime(&time);
    wchar_t value[64]{};
    swprintf_s(value, L"%04u-%02u-%02u %02u:%02u:%02u",
               time.wYear, time.wMonth, time.wDay,
               time.wHour, time.wMinute, time.wSecond);
    if (auto* editor = ActiveEditor()) static_cast<void>(notepad_colon::InsertText(*editor, value));
}

}  // namespace notepad_colon::app
