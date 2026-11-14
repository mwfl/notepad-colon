#include "main_window.h"

// Line tools for logs and delimited data: rainbow columns, highlighted log
// lines and a line filter. Decorations are applied only to the lines a view
// is showing, so they cost the same in a 4 GiB file as in a small one.
namespace notepad_colon::app {

namespace {
constexpr int kFirstColumnIndicator = 24;  // 24..31
constexpr int kColumnColors = 8;
constexpr int kFirstLineMarker = 10;       // 10..16, one per HighlightColor
constexpr int kLineColors = 7;
constexpr LRESULT kMaximumDecoratedLineBytes = 16 * 1024;

constexpr COLORREF kColumnLight[kColumnColors]{
    RGB(0, 92, 197), RGB(170, 60, 0), RGB(0, 120, 60), RGB(140, 0, 140),
    RGB(150, 110, 0), RGB(0, 120, 135), RGB(190, 30, 60), RGB(90, 90, 90)};
constexpr COLORREF kColumnDark[kColumnColors]{
    RGB(110, 180, 255), RGB(255, 160, 100), RGB(120, 220, 150), RGB(230, 140, 230),
    RGB(235, 210, 110), RGB(110, 220, 230), RGB(255, 130, 150), RGB(190, 190, 190)};
// Indexed by HighlightColor: red, orange, yellow, green, blue, purple, gray.
constexpr COLORREF kLineLight[kLineColors]{
    RGB(255, 214, 214), RGB(255, 228, 196), RGB(255, 247, 190), RGB(214, 245, 214),
    RGB(214, 232, 255), RGB(236, 220, 255), RGB(232, 232, 232)};
constexpr COLORREF kLineDark[kLineColors]{
    RGB(96, 32, 32), RGB(96, 60, 20), RGB(84, 78, 20), RGB(28, 78, 40),
    RGB(28, 56, 100), RGB(70, 40, 100), RGB(60, 60, 60)};

bool HasExtension(const std::filesystem::path& path, std::initializer_list<const wchar_t*> extensions) {
    const auto extension = path.extension().wstring();
    for (const auto* candidate : extensions)
        if (_wcsicmp(extension.c_str(), candidate) == 0) return true;
    return false;
}

// The first bytes of the document as UTF-8, for delimiter detection.
std::string ViewSample(mwfl::ScintillaEditor& view, LRESULT maximum) {
    const auto length = (std::min<LRESULT>)(view.GetLength(), maximum);
    if (length <= 0) return {};
    std::string sample(static_cast<std::size_t>(length) + 1, '\0');
    Sci_TextRangeFull range{{0, length}, sample.data()};
    view.Send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&range));
    sample.resize(static_cast<std::size_t>(length));
    return sample;
}
}  // namespace

void MainWindow::ClearLineDecorations(EditorDocument& document) noexcept {
    decorating_ = true;
    auto& view = *document.editor;
    const auto length = view.GetLength();
    for (int index = 0; index < kColumnColors; ++index) {
        view.Send(SCI_SETINDICATORCURRENT, kFirstColumnIndicator + index);
        view.Send(SCI_INDICATORCLEARRANGE, 0, length);
    }
    for (int index = 0; index < kLineColors; ++index)
        view.Send(SCI_MARKERDELETEALL, kFirstLineMarker + index);
    decorating_ = false;
    for (auto& decoration : document.decorations) decoration = {};
}

void MainWindow::DecorateVisibleLines(EditorDocument& document, mwfl::ScintillaEditor& view,
                                      int slot) noexcept {
    auto& cache = document.decorations[slot];
    if (document.delimiter == 0 && !document.highlight_lines) {
        if (cache.first >= 0) ClearLineDecorations(document);
        return;
    }
    try {
        const int palette = IsDark() ? 1 : 0;
        const auto first_visible = view.Send(SCI_GETFIRSTVISIBLELINE);
        const auto first = view.Send(SCI_DOCLINEFROMVISIBLE, first_visible);
        const auto last = (std::min)(view.Send(SCI_GETLINECOUNT) - 1,
            view.Send(SCI_DOCLINEFROMVISIBLE, first_visible + view.Send(SCI_LINESONSCREEN) + 1));
        if (cache.first == first && cache.last == last && cache.revision == document.revision &&
            cache.delimiter == document.delimiter && cache.highlight == document.highlight_lines &&
            cache.palette == palette && cache.rules == highlight_rules_revision_)
            return;
        decorating_ = true;
        if (cache.palette != palette) {
            // Colours belong to the view, so each view is configured once per theme.
            for (int index = 0; index < kColumnColors; ++index) {
                view.Send(SCI_INDICSETSTYLE, kFirstColumnIndicator + index, INDIC_TEXTFORE);
                view.Send(SCI_INDICSETFORE, kFirstColumnIndicator + index,
                          palette ? kColumnDark[index] : kColumnLight[index]);
            }
            for (int index = 0; index < kLineColors; ++index) {
                view.Send(SCI_MARKERDEFINE, kFirstLineMarker + index, SC_MARK_BACKGROUND);
                view.Send(SCI_MARKERSETBACK, kFirstLineMarker + index,
                          palette ? kLineDark[index] : kLineLight[index]);
            }
        }
        std::string text;
        std::vector<notepad_colon::DelimitedField> fields;
        for (auto line = first; line <= last; ++line) {
            const auto start = view.Send(SCI_POSITIONFROMLINE, line);
            const auto length = view.Send(SCI_LINELENGTH, line);
            for (int index = 0; index < kColumnColors; ++index) {
                view.Send(SCI_SETINDICATORCURRENT, kFirstColumnIndicator + index);
                view.Send(SCI_INDICATORCLEARRANGE, start, length);
            }
            const auto markers = view.Send(SCI_MARKERGET, line);
            for (int index = 0; index < kLineColors; ++index)
                if ((markers & (LRESULT{1} << (kFirstLineMarker + index))) != 0)
                    view.Send(SCI_MARKERDELETE, line, kFirstLineMarker + index);
            if (length <= 0 || length > kMaximumDecoratedLineBytes) continue;
            text.assign(static_cast<std::size_t>(length) + 1, '\0');
            view.Send(SCI_GETLINE, line, reinterpret_cast<LPARAM>(text.data()));
            text.resize(static_cast<std::size_t>(length));
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            if (document.delimiter != 0) {
                notepad_colon::SplitDelimitedLine(text, document.delimiter, fields);
                for (std::size_t index = 0; index < fields.size(); ++index) {
                    if (fields[index].length == 0) continue;
                    view.Send(SCI_SETINDICATORCURRENT,
                              kFirstColumnIndicator + static_cast<int>(index % kColumnColors));
                    view.Send(SCI_INDICATORFILLRANGE, start + static_cast<LRESULT>(fields[index].start),
                              static_cast<LPARAM>(fields[index].length));
                }
            }
            if (document.highlight_lines)
                if (const auto rule = notepad_colon::MatchHighlightRule(text, highlight_rules_))
                    view.Send(SCI_MARKERADD, line,
                              kFirstLineMarker + static_cast<int>(highlight_rules_[*rule].color));
        }
        decorating_ = false;
        cache = {first, last, document.revision, document.delimiter, document.highlight_lines,
                 palette, highlight_rules_revision_};
    } catch (...) { decorating_ = false; }
}

void MainWindow::ApplyAutomaticLineTools(EditorDocument& document, const std::filesystem::path& path) {
    if (HasExtension(path, {L".csv", L".tsv", L".tab", L".psv"})) {
        document.delimiter = notepad_colon::DetectDelimiter(ViewSample(*document.editor, 64 * 1024));
        if (document.delimiter == 0 && HasExtension(path, {L".csv"})) document.delimiter = ',';
        if (document.delimiter == 0 && HasExtension(path, {L".tsv", L".tab"})) document.delimiter = '\t';
    }
    if (HasExtension(path, {L".log"})) document.highlight_lines = true;
    if (document.delimiter != 0 || document.highlight_lines)
        DecorateVisibleLines(document, *document.editor, 0);
}

void MainWindow::ToggleRainbowColumns() {
    auto* document = ActiveDocument();
    if (!document) return;
    if (document->delimiter != 0) {
        document->delimiter = 0;
        if (!document->highlight_lines) ClearLineDecorations(*document);
        else for (auto& decoration : document->decorations) decoration.first = -2;
        DecorateVisibleLines(*document, *document->editor, 0);
        SyncPresentation(Tr(L"Rainbow columns off"));
        return;
    }
    const char delimiter = notepad_colon::DetectDelimiter(ViewSample(*document->editor, 64 * 1024));
    if (delimiter == 0) {
        status_.SetText(Tr(L"No consistent comma, tab, semicolon or pipe delimiter was found"));
        return;
    }
    document->delimiter = delimiter;
    DecorateVisibleLines(*document, *document->editor, 0);
    if (document->split_editor) DecorateVisibleLines(*document, *document->split_editor, 1);
    SyncPresentation(Tr(L"Rainbow columns on"));
}

void MainWindow::ToggleHighlightLines() {
    auto* document = ActiveDocument();
    if (!document) return;
    document->highlight_lines = !document->highlight_lines;
    if (!document->highlight_lines && document->delimiter == 0) ClearLineDecorations(*document);
    DecorateVisibleLines(*document, *document->editor, 0);
    if (document->split_editor) DecorateVisibleLines(*document, *document->split_editor, 1);
    status_.SetText(document->highlight_lines ? Tr(L"Highlighting lines that match the highlight rules")
                                              : Tr(L"Line highlighting off"));
}

std::wstring MainWindow::DelimitedFieldStatus(const EditorDocument& document) const {
    if (document.delimiter == 0) return {};
    auto& view = *document.editor;
    const auto caret = view.Send(SCI_GETCURRENTPOS);
    const auto line = view.Send(SCI_LINEFROMPOSITION, caret);
    const auto length = view.Send(SCI_LINELENGTH, line);
    if (length <= 0 || length > kMaximumDecoratedLineBytes) return {};
    std::string text(static_cast<std::size_t>(length) + 1, '\0');
    view.Send(SCI_GETLINE, line, reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<std::size_t>(length));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    const auto field = notepad_colon::DelimitedFieldAt(
        text, document.delimiter, static_cast<std::size_t>(caret - view.Send(SCI_POSITIONFROMLINE, line)));
    return field ? Tr(L"Field ") + std::to_wstring(*field + 1) + L" | " : std::wstring{};
}

void MainWindow::ShowHighlightRules() {
    mwfl::Label caption, help;
    mwfl::TextBox rules_box;
    mwfl::Button accept, defaults, cancel;
    std::optional<std::vector<notepad_colon::HighlightRule>> accepted;
    mwfl::Dialog* pointer = nullptr;
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Highlight Rules"),
        .initial_client_size = {620.0_dip, 210.0_dip}, .resizable = false,
        .callbacks = {.initialize = [&](HWND window) {
            mwfl::ControlHost ui{window};
            ui.Add(caption, {780}, Tr(L"Rules (first match wins)"));
            ui.Add(rules_box, {781}, mwfl::FromUtf8(
                notepad_colon::FormatHighlightRules(highlight_rules_)).value_or(std::wstring{}));
            ui.Add(help, {782}, Tr(L"colour=text|text; ...   Colours: red orange yellow green blue purple gray.   ~text ignores case."));
            ui.Add(defaults, {783}, Tr(L"Restore defaults"));
            ui.Add(accept, {IDOK}, Tr(L"Apply")); ui.Add(cancel, {IDCANCEL}, Tr(L"Cancel"));
            mwfl::SetAccessibleName(rules_box.GetHwnd(), Tr(L"Highlight rules").c_str());
            rules_box.Focus();
            return pointer->SetLayout(mwfl::Column().Margin(10.0_dip).Gap(6.0_dip)
                .Add(caption, mwfl::Fixed(20.0_dip)).Add(rules_box, mwfl::Fixed(30.0_dip))
                .Add(help, mwfl::Fixed(40.0_dip))
                .Add(mwfl::Row().Gap(6.0_dip).Add(defaults, ButtonWidth(Tr(L"Restore defaults")))
                    .Add(mwfl::Column(), mwfl::Stretch())
                    .Add(accept, mwfl::Fixed(90.0_dip)).Add(cancel, mwfl::Fixed(90.0_dip)),
                    mwfl::Fixed(30.0_dip)));
        }, .command = [&](HWND, WORD id, WORD) {
            if (id == 783) {
                rules_box.SetText(mwfl::FromUtf8(notepad_colon::FormatHighlightRules(
                    notepad_colon::DefaultHighlightRules())).value_or(std::wstring{}));
                return true;
            }
            if (id != IDOK) return false;
            std::string error;
            const auto utf8 = mwfl::ToUtf8(rules_box.GetText());
            accepted = utf8 ? notepad_colon::ParseHighlightRules(*utf8, &error) : std::nullopt;
            if (!accepted) {
                ::MessageBoxW(pointer->GetHwnd(),
                    (Tr(L"The rules could not be understood:") + L"\n\n" +
                     mwfl::FromUtf8(error).value_or(std::wstring{})).c_str(),
                    Tr(L"Highlight Rules").c_str(), MB_OK | MB_ICONWARNING);
                return true;
            }
            pointer->Accept();
            return true;
        }}});
    pointer = &dialog;
    static_cast<void>(dialog.ShowModal());
    if (!accepted) return;
    highlight_rules_ = std::move(*accepted);
    ++highlight_rules_revision_;
    if (!IsTestMode()) static_cast<void>(SavePreferences());
    if (auto* document = ActiveDocument()) {
        DecorateVisibleLines(*document, *document->editor, 0);
        if (document->split_editor) DecorateVisibleLines(*document, *document->split_editor, 1);
    }
    status_.SetText(Tr(L"Highlight rules updated"));
}

void MainWindow::ShowFilterLines() {
    if (!ActiveDocument()) return;
    mwfl::Label caption, hint;
    mwfl::TextBox query_box;
    mwfl::CheckBox regex_box, case_box, invert_box;
    mwfl::Button accept, cancel;
    std::wstring query = search_.GetText();
    notepad_colon::LineFilterOptions options;
    options.match_case = search_match_case_;
    options.regular_expression = search_regex_;
    bool accepted = false;
    mwfl::Dialog* pointer = nullptr;
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Filter Lines"),
        .initial_client_size = {520.0_dip, 250.0_dip}, .resizable = false,
        .callbacks = {.initialize = [&](HWND window) {
            mwfl::ControlHost ui{window};
            ui.Add(caption, {790}, Tr(L"Keep lines containing"));
            ui.Add(query_box, {791}, query);
            ui.Add(case_box, {792}, Tr(L"Match Case"));
            ui.Add(regex_box, {793}, Tr(L"Regular Expression"));
            ui.Add(invert_box, {794}, Tr(L"Keep lines that do not match instead"));
            ui.Add(hint, {795}, Tr(L"The result opens in a new tab with the original line numbers."));
            ui.Add(accept, {IDOK}, Tr(L"Filter")); ui.Add(cancel, {IDCANCEL}, Tr(L"Cancel"));
            case_box.SetChecked(options.match_case);
            regex_box.SetChecked(options.regular_expression);
            mwfl::SetAccessibleName(query_box.GetHwnd(), Tr(L"Text that kept lines contain").c_str());
            query_box.Focus();
            return pointer->SetLayout(mwfl::Column().Margin(10.0_dip).Gap(5.0_dip)
                .Add(caption, mwfl::Fixed(20.0_dip)).Add(query_box, mwfl::Fixed(30.0_dip))
                .Add(case_box, mwfl::Fixed(26.0_dip)).Add(regex_box, mwfl::Fixed(26.0_dip))
                .Add(invert_box, mwfl::Fixed(26.0_dip)).Add(hint, mwfl::Fixed(22.0_dip))
                .Add(mwfl::Row().Gap(6.0_dip).Add(mwfl::Column(), mwfl::Stretch())
                    .Add(accept, mwfl::Fixed(90.0_dip)).Add(cancel, mwfl::Fixed(90.0_dip)),
                    mwfl::Fixed(30.0_dip)));
        }, .command = [&](HWND, WORD id, WORD) {
            if (id != IDOK) return false;
            query = query_box.GetText();
            options.match_case = case_box.IsChecked();
            options.regular_expression = regex_box.IsChecked();
            options.invert = invert_box.IsChecked();
            if (query.empty()) return true;
            accepted = true; pointer->Accept(); return true;
        }}});
    pointer = &dialog;
    static_cast<void>(dialog.ShowModal());
    if (accepted) StartLineFilter(std::move(query), options);
}

void MainWindow::StartLineFilter(std::wstring query, notepad_colon::LineFilterOptions options) {
    auto* document = ActiveDocument();
    const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
    const auto needle = mwfl::ToUtf8(query);
    if (!document || !metadata || !needle) return;
    // A large file is scanned from disk in fixed-size chunks; a pending edit
    // exists only in the piece table, which the background scan must not read.
    std::filesystem::path source;
    std::uint64_t source_size = 0;
    std::optional<std::string> text;
    if (document->large_buffer) {
        if (document->large_buffer->IsModified()) {
            status_.SetText(Tr(L"Save the large file before filtering its lines"));
            return;
        }
        source = metadata->path;
        source_size = document->large_buffer->Size();
    } else {
        text = EditorUtf8(*document->editor);
        if (!text) return;
    }
    for (auto& worker : search_workers_) worker.thread.request_stop();
    ReapCompletedWorkers(search_workers_);
    const auto generation = filter_operations_.Begin();
    filter_title_ = Tr(L"Filter: ") + query + L" — " + metadata->title;
    filter_source_highlighted_ = document->highlight_lines;
    if (auto* command = commands_.Find(kCancelSearch)) {
        command->SetEnabled(true);
        menu_.UpdateCommand(*command);
    }
    status_.SetText(Tr(L"Filtering lines..."));
    const HWND window = GetHwnd();
    auto done = std::make_shared<std::atomic_bool>(false);
    search_workers_.push_back({done, std::jthread(
        [this, window, generation, options, done, needle = *needle, source, source_size,
         text = std::move(text)](std::stop_token stop) {
            notepad_colon::LineFilterResult result;
            if (text) {
                result = notepad_colon::FilterLines(*text, needle, options, stop);
            } else {
                std::ifstream input(source, std::ios::binary);
                const notepad_colon::ChunkReader read = [&](std::uint64_t offset, std::size_t length) {
                    std::vector<std::uint8_t> bytes(length);
                    input.clear();
                    input.seekg(static_cast<std::streamoff>(offset));
                    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length));
                    bytes.resize(static_cast<std::size_t>((std::max<std::streamsize>)(0, input.gcount())));
                    return bytes;
                };
                result = notepad_colon::FilterLines(read, source_size, needle, options, stop);
            }
            if (filter_operations_.Publish(generation, std::move(result)))
                ::PostMessageW(window, kFilterCompleteMessage, static_cast<WPARAM>(generation), 0);
            done->store(true);
        })});
}

void MainWindow::CompleteLineFilter() {
    auto result = filter_operations_.TakeCurrent();
    if (!result) return;
    if (auto* command = commands_.Find(kCancelSearch)) {
        command->SetEnabled(false);
        menu_.UpdateCommand(*command);
    }
    if (!result->error.empty()) {
        status_.SetText(Tr(L"Invalid regular expression"));
        return;
    }
    if (result->cancelled) { status_.SetText(Tr(L"Line filter cancelled")); return; }
    if (result->lines.empty()) {
        status_.SetText(Tr(L"Filter: no lines kept out of ") + std::to_wstring(result->lines_scanned));
        return;
    }
    const auto text = mwfl::FromUtf8(notepad_colon::FormatFilteredLines(*result));
    if (!text) { status_.SetText(Tr(L"The filtered lines are not valid UTF-8")); return; }
    NewDocument(*text, filter_title_);
    if (auto* created = ActiveDocument(); created && filter_source_highlighted_) {
        created->highlight_lines = true;
        DecorateVisibleLines(*created, *created->editor, 0);
    }
    status_.SetText(Tr(L"Filter kept ") + std::to_wstring(result->lines.size()) + Tr(L" of ") +
        std::to_wstring(result->lines_scanned) + Tr(L" line(s)") +
        (result->truncated ? Tr(L" (limit reached)") : L""));
}

}  // namespace notepad_colon::app
