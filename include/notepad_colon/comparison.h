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

}  // namespace notepad_colon
