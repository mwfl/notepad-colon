#include <notepad_colon/comparison.h>

#include <algorithm>
#include <cwctype>

namespace notepad_colon {
namespace {
std::vector<std::wstring> Lines(std::wstring_view text) {
    std::vector<std::wstring> result;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find_first_of(L"\r\n", start);
        result.emplace_back(text.substr(start, end - start));
        if (end == std::wstring_view::npos) return result;
        start = end + 1;
        if (text[end] == L'\r' && start < text.size() && text[start] == L'\n') ++start;
    }
    if (text.empty()) result.emplace_back();
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
    for (const auto& line : a) normalized_a.push_back(Normalized(line, options));
    for (const auto& line : b) normalized_b.push_back(Normalized(line, options));

    const auto width = b.size() + 1;
    std::vector<std::size_t> lcs((a.size() + 1) * width);
    for (std::size_t i = a.size(); i-- > 0;)
        for (std::size_t j = b.size(); j-- > 0;)
            lcs[i * width + j] = normalized_a[i] == normalized_b[j]
                ? 1 + lcs[(i + 1) * width + j + 1]
                : (std::max)(lcs[(i + 1) * width + j], lcs[i * width + j + 1]);

    ComparisonResult result;
    std::size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        if (i < a.size() && j < b.size() && normalized_a[i] == normalized_b[j]) {
            result.lines.push_back({DifferenceKind::equal, i + 1, j + 1, a[i], b[j]});
            ++i; ++j;
        } else if (i < a.size() && j < b.size() &&
                   lcs[(i + 1) * width + j] == lcs[i * width + j + 1]) {
            const auto [left_span, right_span] = ChangedSpans(a[i], b[j]);
            result.lines.push_back({DifferenceKind::modified, i + 1, j + 1,
                                    a[i], b[j], left_span, right_span});
            ++i; ++j;
        } else if (j < b.size() &&
                   (i == a.size() || lcs[i * width + j + 1] > lcs[(i + 1) * width + j])) {
            result.lines.push_back({DifferenceKind::inserted, 0, j + 1, {}, b[j], {}, {0, b[j].size()}});
            ++j;
        } else {
            result.lines.push_back({DifferenceKind::deleted, i + 1, 0, a[i], {}, {0, a[i].size()}, {}});
            ++i;
        }
    }
    result.changed_lines = static_cast<std::size_t>(std::ranges::count_if(
        result.lines, [](const auto& line) { return line.kind != DifferenceKind::equal; }));
    result.identical = result.changed_lines == 0;
    return result;
}
}  // namespace notepad_colon
