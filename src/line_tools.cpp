#include <notepad_colon/line_tools.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <regex>

namespace notepad_colon {
namespace {
constexpr std::size_t kChunkBytes = 4u * 1024 * 1024;
constexpr std::array kDelimiters{'\t', ',', ';', '|'};

char FoldAscii(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

// Substring search; when `fold` is set ASCII letters compare case-insensitively
// and `needle` must already be folded. Non-ASCII bytes always compare exactly.
bool Contains(std::string_view haystack, std::string_view needle, bool fold) noexcept {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    if (!fold) return haystack.find(needle) != std::string_view::npos;
    const auto last = haystack.size() - needle.size();
    for (std::size_t offset = 0; offset <= last; ++offset) {
        std::size_t index = 0;
        while (index < needle.size() && FoldAscii(haystack[offset + index]) == needle[index]) ++index;
        if (index == needle.size()) return true;
    }
    return false;
}

std::string Fold(std::string_view value) {
    std::string folded{value};
    std::ranges::transform(folded, folded.begin(), FoldAscii);
    return folded;
}

std::string_view Trim(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' ||
                              value.back() == '\n')) value.remove_suffix(1);
    return value;
}

std::size_t CountDelimiters(std::string_view line, char delimiter) noexcept {
    std::size_t count = 0;
    bool quoted = false;
    for (const char value : line) {
        if (value == '"') quoted = !quoted;
        else if (value == delimiter && !quoted) ++count;
    }
    return count;
}

constexpr std::array kColorNames{"red", "orange", "yellow", "green", "blue", "purple", "gray"};
}  // namespace

char DetectDelimiter(std::string_view sample) noexcept {
    std::array<std::vector<std::size_t>, kDelimiters.size()> counts;
    std::size_t lines = 0;
    for (std::size_t start = 0; start < sample.size() && lines < 64;) {
        auto end = sample.find('\n', start);
        const bool complete = end != std::string_view::npos;
        if (!complete) end = sample.size();
        auto line = sample.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        start = end + 1;
        // A sample cut mid-line would report a short last row; ignore it
        // unless it is the only row.
        if (!complete && lines != 0) break;
        if (line.empty()) continue;
        for (std::size_t index = 0; index < kDelimiters.size(); ++index)
            counts[index].push_back(CountDelimiters(line, kDelimiters[index]));
        ++lines;
    }
    if (lines < 2) return 0;
    char best = 0;
    std::size_t best_columns = 0;
    for (std::size_t index = 0; index < kDelimiters.size(); ++index) {
        const auto& values = counts[index];
        const auto first = values.front();
        if (first == 0) continue;
        // Real tables repeat the header's column count on almost every row.
        const auto same = static_cast<std::size_t>(std::ranges::count(values, first));
        if (same * 10 < values.size() * 9) continue;
        if (first > best_columns) { best_columns = first; best = kDelimiters[index]; }
    }
    return best;
}

void SplitDelimitedLine(std::string_view line, char delimiter, std::vector<DelimitedField>& fields) {
    fields.clear();
    std::size_t start = 0;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char value = line[index];
        if (value == '"') quoted = !quoted;  // "" toggles twice: still inside the field.
        else if (value == delimiter && !quoted) {
            fields.push_back({start, index - start});
            start = index + 1;
        }
    }
    fields.push_back({start, line.size() - start});
}

std::optional<std::size_t> DelimitedFieldAt(std::string_view line, char delimiter, std::size_t offset) {
    std::vector<DelimitedField> fields;
    SplitDelimitedLine(line, delimiter, fields);
    for (std::size_t index = 0; index < fields.size(); ++index)
        if (offset <= fields[index].start + fields[index].length) return index;
    return fields.empty() ? std::nullopt : std::optional{fields.size() - 1};
}

std::string UnquoteDelimitedField(std::string_view field) {
    field = Trim(field);
    if (field.size() < 2 || field.front() != '"' || field.back() != '"') return std::string{field};
    std::string value;
    value.reserve(field.size() - 2);
    for (std::size_t index = 1; index + 1 < field.size(); ++index) {
        value += field[index];
        if (field[index] == '"' && index + 2 < field.size() && field[index + 1] == '"') ++index;
    }
    return value;
}

namespace {
// Calls `row(line)` for every line of a chunked source; `line` has no
// line ending. A line longer than `maximum_line_bytes` is reported truncated
// to that length plus one byte, so the callee can tell it was too long.
template <typename Row>
bool ForEachLine(const ChunkReader& read, std::uint64_t size, std::size_t maximum_line_bytes,
                 std::stop_token stop, Row&& row) {
    std::string carry;
    bool carrying = false;
    const auto room = [&] { return maximum_line_bytes + 1 - (std::min)(carry.size(), maximum_line_bytes + 1); };
    const auto emit = [&](std::string_view line) {
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        return row(line);
    };
    for (std::uint64_t offset = 0; offset < size;) {
        if (stop.stop_requested()) return false;
        const auto wanted = static_cast<std::size_t>((std::min<std::uint64_t>)(kChunkBytes, size - offset));
        const auto chunk = read(offset, wanted);
        if (chunk.empty()) break;
        const std::string_view bytes{reinterpret_cast<const char*>(chunk.data()), chunk.size()};
        std::size_t start = 0;
        while (start < bytes.size()) {
            const auto end = bytes.find('\n', start);
            if (end == std::string_view::npos) break;
            if (!carrying) {
                if (!emit(bytes.substr(start, (std::min)(end - start, maximum_line_bytes + 1)))) return true;
            } else {
                carry.append(bytes.substr(start, (std::min)(end - start, room())));
                const bool keep_going = emit(carry);
                carry.clear();
                carrying = false;
                if (!keep_going) return true;
            }
            start = end + 1;
        }
        if (start < bytes.size()) {
            const auto rest = bytes.substr(start);
            carry.append(rest.substr(0, (std::min)(rest.size(), room())));
            carrying = true;
        }
        offset += chunk.size();
        if (chunk.size() < wanted) break;
    }
    if (carrying) emit(carry);
    return true;
}

std::optional<double> NumberValue(std::string_view value) noexcept {
    value = Trim(value);
    if (value.empty()) return std::nullopt;
    if (value.front() == '+') value.remove_prefix(1);
    double number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return std::nullopt;
    return number;
}

std::string FieldValue(std::string_view line, char delimiter, std::size_t column,
                       std::vector<DelimitedField>& fields) {
    SplitDelimitedLine(line, delimiter, fields);
    if (column >= fields.size()) return {};
    return UnquoteDelimitedField(line.substr(fields[column].start, fields[column].length));
}
}  // namespace

ColumnExtractResult ExtractColumn(const ChunkReader& read, std::uint64_t size, char delimiter,
                                  std::size_t column, const ColumnExtractOptions& options,
                                  std::stop_token stop) {
    ColumnExtractResult result;
    std::vector<DelimitedField> fields;
    const bool finished = ForEachLine(read, size, options.maximum_line_bytes, stop,
        [&](std::string_view line) {
            if (line.size() > options.maximum_line_bytes) { ++result.missing; return true; }
            SplitDelimitedLine(line, delimiter, fields);
            if (column >= fields.size()) {
                // A blank line is not a row; a short row is reported.
                if (!line.empty()) ++result.missing;
                return true;
            }
            const auto raw = line.substr(fields[column].start, fields[column].length);
            const auto value = options.unquote ? UnquoteDelimitedField(raw) : std::string{raw};
            if (result.rows >= options.maximum_rows ||
                result.text.size() + value.size() + 2 > options.maximum_bytes) {
                result.truncated = true;
                return false;
            }
            result.text += value;
            result.text += "\r\n";
            ++result.rows;
            return true;
        });
    result.cancelled = !finished;
    return result;
}

ColumnExtractResult ExtractColumn(std::string_view text, char delimiter, std::size_t column,
                                  const ColumnExtractOptions& options, std::stop_token stop) {
    const ChunkReader read = [text](std::uint64_t offset, std::size_t length) {
        const auto start = static_cast<std::size_t>((std::min<std::uint64_t>)(offset, text.size()));
        const auto part = text.substr(start, length);
        return std::vector<std::uint8_t>{part.begin(), part.end()};
    };
    return ExtractColumn(read, text.size(), delimiter, column, options, stop);
}

namespace {
struct Row { std::string_view text; std::string_view ending; };

std::vector<Row> SplitRows(std::string_view text) {
    std::vector<Row> rows;
    for (std::size_t start = 0; start < text.size();) {
        const auto end = text.find('\n', start);
        if (end == std::string_view::npos) { rows.push_back({text.substr(start), {}}); break; }
        const bool crlf = end > start && text[end - 1] == '\r';
        rows.push_back({text.substr(start, end - start - (crlf ? 1 : 0)),
                        text.substr(end - (crlf ? 1 : 0), crlf ? 2 : 1)});
        start = end + 1;
    }
    return rows;
}

bool AnyNumber(std::string_view line, char delimiter, std::vector<DelimitedField>& fields) {
    SplitDelimitedLine(line, delimiter, fields);
    for (const auto& field : fields)
        if (NumberValue(UnquoteDelimitedField(line.substr(field.start, field.length)))) return true;
    return false;
}

bool HasHeader(const std::vector<Row>& rows, char delimiter) {
    std::vector<DelimitedField> fields;
    if (rows.size() < 2 || rows.front().text.empty() || AnyNumber(rows.front().text, delimiter, fields))
        return false;
    for (std::size_t index = 1; index < rows.size() && index <= 64; ++index)
        if (AnyNumber(rows[index].text, delimiter, fields)) return true;
    return false;
}
}  // namespace

bool HasHeaderRow(std::string_view text, char delimiter) {
    return HasHeader(SplitRows(text), delimiter);
}

std::string SortDelimitedRows(std::string_view text, char delimiter, std::size_t column,
                              bool descending, HeaderRow header) {
    auto rows = SplitRows(text);
    if (rows.size() < 2) return std::string{text};
    const bool unterminated = rows.back().ending.empty();
    std::string_view ending = "\r\n";
    for (const auto& row : rows) if (!row.ending.empty()) { ending = row.ending; break; }
    const bool keep_first = header == HeaderRow::present ||
        (header == HeaderRow::detect && HasHeader(rows, delimiter));

    struct Key { std::optional<double> number; std::string value; std::size_t row; };
    std::vector<Key> keys;
    std::vector<DelimitedField> fields;
    keys.reserve(rows.size());
    for (std::size_t index = keep_first ? 1 : 0; index < rows.size(); ++index) {
        auto value = FieldValue(rows[index].text, delimiter, column, fields);
        const auto number = NumberValue(value);
        keys.push_back({number, std::move(value), index});
    }
    const auto less = [](const Key& left, const Key& right) {
        if (left.number && right.number) return *left.number < *right.number;
        if (left.number.has_value() != right.number.has_value()) return left.number.has_value();
        return left.value < right.value;
    };
    std::ranges::stable_sort(keys, [&](const Key& left, const Key& right) {
        return descending ? less(right, left) : less(left, right);
    });

    std::string sorted;
    sorted.reserve(text.size() + ending.size());
    const auto append = [&](const Row& row, bool last) {
        sorted += row.text;
        // The row that had no line break may no longer be last: every row gets
        // one except the new last row of a text that did not end with one.
        if (!last || !unterminated) sorted += row.ending.empty() ? ending : row.ending;
    };
    if (keep_first) append(rows.front(), keys.empty());
    for (std::size_t index = 0; index < keys.size(); ++index)
        append(rows[keys[index].row], index + 1 == keys.size());
    return sorted;
}

LineFilterResult FilterLines(const ChunkReader& read, std::uint64_t size, std::string_view query,
                             const LineFilterOptions& options, std::stop_token stop) {
    LineFilterResult result;
    std::optional<std::regex> expression;
    if (options.regular_expression) {
        try {
            auto flags = std::regex_constants::ECMAScript | std::regex_constants::optimize;
            if (!options.match_case) flags |= std::regex_constants::icase;
            expression.emplace(std::string{query}, flags);
        } catch (const std::regex_error&) {
            result.error = "Invalid regular expression";
            return result;
        }
    }
    const bool fold = !options.match_case && !options.regular_expression;
    const std::string needle = fold ? Fold(query) : std::string{query};
    std::size_t kept_bytes = 0;
    std::string carry;                 // Unterminated tail of the previous chunk.
    std::uint64_t carry_offset = 0;    // Source offset of carry[0].
    std::uint64_t line_number = 0;
    bool carry_overflow = false;       // The current line already exceeded the cap.

    const auto consider = [&](std::string_view line, std::uint64_t offset) {
        ++line_number;
        ++result.lines_scanned;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        bool matches = false;
        try {
            matches = expression ? std::regex_search(line.begin(), line.end(), *expression)
                                 : Contains(line, needle, fold);
        } catch (const std::regex_error&) {
            matches = false;  // Pathological backtracking on one line: treat as no match.
        }
        if (matches == options.invert) return true;
        const auto kept = line.substr(0, (std::min)(line.size(), options.maximum_line_bytes));
        if (result.lines.size() >= options.maximum_lines ||
            kept_bytes + kept.size() > options.maximum_bytes) {
            result.truncated = true;
            return false;
        }
        kept_bytes += kept.size();
        result.lines.push_back({line_number, offset, std::string{kept}});
        return true;
    };

    for (std::uint64_t offset = 0; offset < size;) {
        if (stop.stop_requested()) { result.cancelled = true; return result; }
        const auto wanted = static_cast<std::size_t>((std::min<std::uint64_t>)(kChunkBytes, size - offset));
        const auto chunk = read(offset, wanted);
        if (chunk.empty()) break;
        const std::string_view bytes{reinterpret_cast<const char*>(chunk.data()), chunk.size()};
        std::size_t start = 0;
        while (start < bytes.size()) {
            const auto end = bytes.find('\n', start);
            if (end == std::string_view::npos) break;
            if (carry.empty() && !carry_overflow) {
                if (!consider(bytes.substr(start, end - start), offset + start)) return result;
            } else {
                // Only the capped head of an over-long line is kept in memory.
                if (!carry_overflow)
                    carry.append(bytes.substr(start, (std::min)(end - start,
                        options.maximum_line_bytes + 1 - (std::min)(carry.size(), options.maximum_line_bytes + 1))));
                const bool keep_going = consider(carry, carry_offset);
                carry.clear();
                carry_overflow = false;
                if (!keep_going) return result;
            }
            start = end + 1;
        }
        if (start < bytes.size()) {
            if (carry.empty() && !carry_overflow) carry_offset = offset + start;
            const auto rest = bytes.substr(start);
            const auto room = options.maximum_line_bytes + 1 > carry.size()
                ? options.maximum_line_bytes + 1 - carry.size() : 0;
            carry.append(rest.substr(0, (std::min)(rest.size(), room)));
            if (rest.size() > room) carry_overflow = true;
        }
        offset += chunk.size();
        result.bytes_scanned = offset;
        if (chunk.size() < wanted) break;
    }
    if (!carry.empty() || carry_overflow) consider(carry, carry_offset);
    return result;
}

LineFilterResult FilterLines(std::string_view text, std::string_view query,
                             const LineFilterOptions& options, std::stop_token stop) {
    const ChunkReader read = [text](std::uint64_t offset, std::size_t length) {
        const auto start = static_cast<std::size_t>((std::min<std::uint64_t>)(offset, text.size()));
        const auto part = text.substr(start, length);
        return std::vector<std::uint8_t>{part.begin(), part.end()};
    };
    return FilterLines(read, text.size(), query, options, stop);
}

std::string FormatFilteredLines(const LineFilterResult& result) {
    std::size_t width = 1;
    if (!result.lines.empty()) width = std::to_string(result.lines.back().line_number).size();
    std::string output;
    for (const auto& line : result.lines) {
        const auto number = std::to_string(line.line_number);
        output.append(width - (std::min)(width, number.size()), ' ');
        output += number;
        output += ": ";
        output += line.text;
        output += "\r\n";
    }
    return output;
}

std::optional<std::uint64_t> FilteredLineNumber(std::string_view row) noexcept {
    while (!row.empty() && row.front() == ' ') row.remove_prefix(1);
    std::uint64_t number = 0;
    const auto parsed = std::from_chars(row.data(), row.data() + row.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr == row.data() || number == 0) return std::nullopt;
    if (parsed.ptr == row.data() + row.size() || *parsed.ptr != ':') return std::nullopt;
    return number;
}

std::string_view HighlightColorName(HighlightColor color) noexcept {
    const auto index = static_cast<std::size_t>(color);
    return index < kColorNames.size() ? kColorNames[index] : kColorNames.front();
}

std::vector<HighlightRule> DefaultHighlightRules() {
    return {{HighlightColor::red, {"ERROR", "FATAL", "CRITICAL", "Exception"}, true},
            {HighlightColor::orange, {"WARN"}, true},
            {HighlightColor::gray, {"DEBUG", "TRACE"}, true}};
}

std::optional<std::vector<HighlightRule>> ParseHighlightRules(std::string_view text, std::string* error) {
    const auto fail = [&](std::string message) -> std::optional<std::vector<HighlightRule>> {
        if (error) *error = std::move(message);
        return std::nullopt;
    };
    std::vector<HighlightRule> rules;
    for (std::size_t start = 0; start <= text.size();) {
        const auto end = (std::min)(text.find(';', start), text.size());
        const auto item = Trim(text.substr(start, end - start));
        start = end + 1;
        if (item.empty()) continue;
        const auto equals = item.find('=');
        if (equals == std::string_view::npos) return fail("Expected colour=text in \"" + std::string{item} + "\"");
        const auto name = Fold(Trim(item.substr(0, equals)));
        const auto found = std::ranges::find(kColorNames, name);
        if (found == kColorNames.end()) return fail("Unknown colour \"" + name + "\"");
        HighlightRule rule;
        rule.color = static_cast<HighlightColor>(found - kColorNames.begin());
        auto terms = Trim(item.substr(equals + 1));
        if (!terms.empty() && terms.front() == '~') { rule.match_case = false; terms.remove_prefix(1); }
        for (std::size_t cursor = 0; cursor <= terms.size();) {
            const auto stop = (std::min)(terms.find('|', cursor), terms.size());
            const auto term = Trim(terms.substr(cursor, stop - cursor));
            if (!term.empty()) rule.terms.push_back(rule.match_case ? std::string{term} : Fold(term));
            cursor = stop + 1;
        }
        if (rule.terms.empty()) return fail("No text after \"" + name + "=\"");
        rules.push_back(std::move(rule));
        if (rules.size() > 32) return fail("At most 32 rules are supported");
    }
    return rules;
}

std::string FormatHighlightRules(std::span<const HighlightRule> rules) {
    std::string output;
    for (const auto& rule : rules) {
        if (!output.empty()) output += "; ";
        output += HighlightColorName(rule.color);
        output += '=';
        if (!rule.match_case) output += '~';
        for (std::size_t index = 0; index < rule.terms.size(); ++index) {
            if (index != 0) output += '|';
            output += rule.terms[index];
        }
    }
    return output;
}

std::optional<std::size_t> MatchHighlightRule(std::string_view line,
                                              std::span<const HighlightRule> rules) noexcept {
    for (std::size_t index = 0; index < rules.size(); ++index)
        for (const auto& term : rules[index].terms)
            if (Contains(line, term, !rules[index].match_case)) return index;
    return std::nullopt;
}

}  // namespace notepad_colon
