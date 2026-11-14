#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace notepad_colon {

struct CompareOptions {
    bool ignore_case = false;
    bool ignore_whitespace = false;
    bool ignore_line_endings = true;
};

enum class DifferenceKind { equal, inserted, deleted, modified };

struct TextSpan {
    std::size_t start = 0;
    std::size_t length = 0;
};

struct LineDifference {
    DifferenceKind kind = DifferenceKind::equal;
    std::size_t left_line = 0;
    std::size_t right_line = 0;
    std::wstring left_text;
    std::wstring right_text;
    TextSpan left_change;
    TextSpan right_change;
};

struct ComparisonResult {
    std::vector<LineDifference> lines;
    std::size_t changed_lines = 0;
    bool identical = true;
};

ComparisonResult CompareText(std::wstring_view left, std::wstring_view right,
                             CompareOptions options = {});

// A run of adjacent changed lines: what a reviewer accepts or rejects as one.
struct ChangeHunk {
    std::size_t first = 0;       // Index of the first changed entry in ComparisonResult::lines.
    std::size_t count = 0;       // Number of consecutive changed entries.
    std::size_t left_line = 0;   // One-based line where the hunk starts on each side.
    std::size_t right_line = 0;
    std::size_t removed = 0;     // Lines the hunk takes from the left side.
    std::size_t added = 0;       // Lines the hunk adds on the right side.
};

std::vector<ChangeHunk> GroupChangeHunks(const ComparisonResult& comparison);

// Reviews a change from `before` to `after`: the result is `after`, except
// that every hunk whose entry in `rejected` is true keeps the text of
// `before`. `comparison` must be CompareText(before, after) and `rejected`
// must have one entry per hunk of GroupChangeHunks(comparison).
std::wstring MergeReviewedText(std::wstring_view before, std::wstring_view after,
                               const ComparisonResult& comparison,
                               const std::vector<bool>& rejected);

}  // namespace notepad_colon
