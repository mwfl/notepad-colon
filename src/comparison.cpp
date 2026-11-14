#include <notepad_colon/comparison.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>

namespace notepad_colon {
namespace {
struct ParsedLine { std::wstring text; std::wstring ending; };

// Upper bound on the alignment table (32-bit cells): 64 MiB.
constexpr std::size_t kMaximumTableCells = 16u * 1024 * 1024;

std::vector<ParsedLine> Lines(std::wstring_view text) {
    std::vector<ParsedLine> result;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find_first_of(L"\r\n", start);
        ParsedLine line{std::wstring{text.substr(start, end - start)}, {}};
        if (end == std::wstring_view::npos) {
            result.push_back(std::move(line));
            return result;
        }
        line.ending = text[end] == L'\r' && end + 1 < text.size() && text[end + 1] == L'\n'
            ? L"\r\n" : std::wstring(1, text[end]);
        result.push_back(std::move(line));
        start = end + 1;
        if (text[end] == L'\r' && start < text.size() && text[start] == L'\n') ++start;
    }
    if (text.empty()) result.push_back({});
    return result;
}

std::wstring Normalized(std::wstring_view text, const CompareOptions& options) {
    std::wstring result;
    bool pending_space = false;
    for (auto c : text) {
        if (options.ignore_whitespace && std::iswspace(c)) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) result += L' ';
        pending_space = false;
        result += options.ignore_case ? static_cast<wchar_t>(std::towlower(c)) : c;
    }
    return result;
}

std::pair<TextSpan, TextSpan> ChangedSpans(std::wstring_view left, std::wstring_view right) {
    std::size_t prefix = 0;
    while (prefix < left.size() && prefix < right.size() && left[prefix] == right[prefix]) ++prefix;
    std::size_t left_suffix = left.size(), right_suffix = right.size();
    while (left_suffix > prefix && right_suffix > prefix &&
           left[left_suffix - 1] == right[right_suffix - 1]) {
        --left_suffix;
        --right_suffix;
    }
    return {{prefix, left_suffix - prefix}, {prefix, right_suffix - prefix}};
}
}  // namespace

ComparisonResult CompareText(std::wstring_view left, std::wstring_view right,
                             CompareOptions options) {
    const auto a = Lines(left);
    const auto b = Lines(right);
    std::vector<std::wstring> normalized_a, normalized_b;
    normalized_a.reserve(a.size()); normalized_b.reserve(b.size());
    for (const auto& line : a) normalized_a.push_back(
        Normalized(line.text, options) + (options.ignore_line_endings ? L"" : line.ending));
    for (const auto& line : b) normalized_b.push_back(
        Normalized(line.text, options) + (options.ignore_line_endings ? L"" : line.ending));

    // Most comparisons differ in a small region. Matching the common head and
    // tail first keeps the quadratic table to the part that actually changed.
    std::size_t head = 0;
    while (head < a.size() && head < b.size() && normalized_a[head] == normalized_b[head]) ++head;
    std::size_t tail = 0;
    while (tail < a.size() - head && tail < b.size() - head &&
           normalized_a[a.size() - 1 - tail] == normalized_b[b.size() - 1 - tail]) ++tail;
    const auto a_end = a.size() - tail;
    const auto b_end = b.size() - tail;

    ComparisonResult result;
    result.lines.reserve((std::max)(a.size(), b.size()));
    for (std::size_t line = 0; line < head; ++line)
        result.lines.push_back({DifferenceKind::equal, line + 1, line + 1, a[line].text, b[line].text});

    const auto rows = a_end - head;
    const auto columns = b_end - head;
    const auto width = columns + 1;
    std::size_t i = head, j = head;
    if (rows != 0 && columns != 0 && rows <= kMaximumTableCells / width) {
        std::vector<std::uint32_t> lcs((rows + 1) * width);
        const auto cell = [&](std::size_t row, std::size_t column) -> std::uint32_t& {
            return lcs[(row - head) * width + (column - head)];
        };
        for (std::size_t row = a_end; row-- > head;)
            for (std::size_t column = b_end; column-- > head;)
                cell(row, column) = normalized_a[row] == normalized_b[column]
                    ? 1 + cell(row + 1, column + 1)
                    : (std::max)(cell(row + 1, column), cell(row, column + 1));
        while (i < a_end || j < b_end) {
            if (i < a_end && j < b_end && normalized_a[i] == normalized_b[j]) {
                result.lines.push_back({DifferenceKind::equal, i + 1, j + 1, a[i].text, b[j].text});
                ++i; ++j;
            } else if (i < a_end && j < b_end && cell(i + 1, j) == cell(i, j + 1)) {
                const auto [left_span, right_span] = ChangedSpans(a[i].text, b[j].text);
                result.lines.push_back({DifferenceKind::modified, i + 1, j + 1,
                                        a[i].text, b[j].text, left_span, right_span});
                ++i; ++j;
            } else if (j < b_end && (i == a_end || cell(i, j + 1) > cell(i + 1, j))) {
                result.lines.push_back({DifferenceKind::inserted, i + 1, j + 1, {}, b[j].text, {}, {0, b[j].text.size()}});
                ++j;
            } else {
                result.lines.push_back({DifferenceKind::deleted, i + 1, j + 1, a[i].text, {}, {0, a[i].text.size()}, {}});
                ++i;
            }
        }
    }
    // Whatever the table did not cover (one side empty, or a region too large
    // to align) is reported as removed lines followed by added lines.
    for (; i < a_end; ++i)
        result.lines.push_back({DifferenceKind::deleted, i + 1, j + 1, a[i].text, {}, {0, a[i].text.size()}, {}});
    for (; j < b_end; ++j)
        result.lines.push_back({DifferenceKind::inserted, i + 1, j + 1, {}, b[j].text, {}, {0, b[j].text.size()}});
    for (; i < a.size(); ++i, ++j)
        result.lines.push_back({DifferenceKind::equal, i + 1, j + 1, a[i].text, b[j].text});
    result.changed_lines = static_cast<std::size_t>(std::ranges::count_if(
        result.lines, [](const auto& line) { return line.kind != DifferenceKind::equal; }));
    result.identical = result.changed_lines == 0;
    return result;
}

std::vector<ChangeHunk> GroupChangeHunks(const ComparisonResult& comparison) {
    std::vector<ChangeHunk> hunks;
    bool open = false;
    for (std::size_t index = 0; index < comparison.lines.size(); ++index) {
        const auto& line = comparison.lines[index];
        if (line.kind == DifferenceKind::equal) { open = false; continue; }
        if (!open) {
            hunks.push_back({index, 0, line.left_line, line.right_line, 0, 0});
            open = true;
        }
        auto& hunk = hunks.back();
        ++hunk.count;
        if (line.kind != DifferenceKind::inserted) ++hunk.removed;
        if (line.kind != DifferenceKind::deleted) ++hunk.added;
    }
    return hunks;
}

std::wstring MergeReviewedText(std::wstring_view before, std::wstring_view after,
                               const ComparisonResult& comparison,
                               const std::vector<bool>& rejected) {
    const auto a = Lines(before);
    const auto b = Lines(after);
    const auto hunks = GroupChangeHunks(comparison);
    std::vector<const ParsedLine*> pieces;
    pieces.reserve(comparison.lines.size());
    std::size_t hunk = 0;
    for (std::size_t index = 0; index < comparison.lines.size(); ++index) {
        const auto& line = comparison.lines[index];
        while (hunk < hunks.size() && index >= hunks[hunk].first + hunks[hunk].count) ++hunk;
        const bool in_hunk = hunk < hunks.size() && index >= hunks[hunk].first;
        const bool keep_before = in_hunk && hunk < rejected.size() && rejected[hunk];
        if (line.kind == DifferenceKind::equal || !keep_before) {
            if (line.kind != DifferenceKind::deleted && line.right_line - 1 < b.size())
                pieces.push_back(&b[line.right_line - 1]);
        } else if (line.kind != DifferenceKind::inserted && line.left_line - 1 < a.size()) {
            pieces.push_back(&a[line.left_line - 1]);
        }
    }
    // A line taken from the end of one side may have no line break of its own.
    std::wstring_view default_ending = L"\r\n";
    for (const auto* side : {&b, &a}) {
        const auto found = std::ranges::find_if(*side,
            [](const ParsedLine& line) { return !line.ending.empty(); });
        if (found != side->end()) { default_ending = found->ending; break; }
    }
    std::wstring merged;
    for (std::size_t index = 0; index < pieces.size(); ++index) {
        merged += pieces[index]->text;
        if (!pieces[index]->ending.empty()) merged += pieces[index]->ending;
        else if (index + 1 < pieces.size()) merged += default_ending;
    }
    return merged;
}
}  // namespace notepad_colon
