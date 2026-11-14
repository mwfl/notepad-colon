#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

// Line-oriented tools for logs and delimited data. Everything here works on
// UTF-8 bytes and bounded memory so it can run over multi-gigabyte files.
namespace notepad_colon {

// ---- Delimited (CSV / TSV) text -------------------------------------------

struct DelimitedField {
    std::size_t start = 0;   // Byte offset within the line.
    std::size_t length = 0;  // Includes surrounding quotes, excludes the delimiter.
};

// Returns ',', '\t', ';' or '|' when the sample looks like delimited rows with
// a consistent column count, otherwise 0. At most the first 64 lines are read.
char DetectDelimiter(std::string_view sample) noexcept;

// Splits one line (without its line ending) into fields. Delimiters inside
// double-quoted fields do not split; "" is an escaped quote.
void SplitDelimitedLine(std::string_view line, char delimiter, std::vector<DelimitedField>& fields);

// Zero-based index of the field containing `offset`, if the line has fields.
std::optional<std::size_t> DelimitedFieldAt(std::string_view line, char delimiter,
                                            std::size_t offset);

// ---- Line filter ----------------------------------------------------------

struct LineFilterOptions {
    bool match_case = false;
    bool regular_expression = false;
    bool invert = false;                              // Keep lines that do NOT match.
    std::size_t maximum_lines = 100000;
    std::size_t maximum_bytes = 16u * 1024 * 1024;    // Of kept line text.
    std::size_t maximum_line_bytes = 64u * 1024;      // Longer kept lines are cut here.
};

struct FilteredLine {
    std::uint64_t line_number = 0;  // One-based, in the source.
    std::uint64_t offset = 0;       // Byte offset of the line start in the source.
    std::string text;               // Without the line ending.
};

struct LineFilterResult {
    std::vector<FilteredLine> lines;
    std::uint64_t lines_scanned = 0;
    std::uint64_t bytes_scanned = 0;
    bool truncated = false;   // A limit stopped the scan early.
    bool cancelled = false;
    std::string error;        // Invalid regular expression.
};

// Reads up to `length` bytes at `offset`; a short or empty result ends the scan.
using ChunkReader = std::function<std::vector<std::uint8_t>(std::uint64_t offset, std::size_t length)>;

// Streams the source in fixed-size chunks; memory use does not grow with the
// source size, only with the (bounded) number of kept lines.
LineFilterResult FilterLines(const ChunkReader& read, std::uint64_t size, std::string_view query,
                             const LineFilterOptions& options = {}, std::stop_token stop = {});
LineFilterResult FilterLines(std::string_view text, std::string_view query,
                             const LineFilterOptions& options = {}, std::stop_token stop = {});

// "    12: text" rows, the line number right-aligned to the widest one.
std::string FormatFilteredLines(const LineFilterResult& result);

// ---- Highlight rules ------------------------------------------------------

enum class HighlightColor { red, orange, yellow, green, blue, purple, gray };

struct HighlightRule {
    HighlightColor color = HighlightColor::red;
    // Alternatives; a line matches when it contains any of them.
    std::vector<std::string> terms;
    bool match_case = true;
};

// ERROR/FATAL red, WARN orange, DEBUG/TRACE gray.
std::vector<HighlightRule> DefaultHighlightRules();

// Parses "red=ERROR|FATAL; orange=WARN; gray=debug|trace". A leading '~' on
// the term list makes the rule case-insensitive: "red=~error".
std::optional<std::vector<HighlightRule>> ParseHighlightRules(std::string_view text,
                                                              std::string* error = nullptr);
std::string FormatHighlightRules(std::span<const HighlightRule> rules);
std::string_view HighlightColorName(HighlightColor color) noexcept;

// Index of the first rule that matches the line; earlier rules win.
std::optional<std::size_t> MatchHighlightRule(std::string_view line,
                                              std::span<const HighlightRule> rules) noexcept;

}  // namespace notepad_colon
