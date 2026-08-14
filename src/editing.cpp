#include <notepad_colon/editing.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <vector>

namespace notepad_colon {
namespace {
struct Lines {
    std::vector<std::wstring> values;
    std::wstring ending = L"\r\n";
    bool terminated = false;
};

Lines ParseLines(std::wstring_view text) {
    Lines result;
    if (text.find(L"\r\n") == std::wstring_view::npos) result.ending = L"\n";
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find_first_of(L"\r\n", start);
        result.values.emplace_back(text.substr(start, end - start));
        if (end == std::wstring_view::npos) {
            result.terminated = false;
            return result;
        }
        result.terminated = true;
        start = end + 1;
        if (text[end] == L'\r' && start < text.size() && text[start] == L'\n') ++start;
    }
    if (text.empty()) result.values.emplace_back();
    return result;
}

std::wstring Compose(const Lines& lines) {
    std::wstring result;
    for (std::size_t index = 0; index < lines.values.size(); ++index) {
        if (index) result += lines.ending;
        result += lines.values[index];
    }
    if (lines.terminated && !lines.values.empty()) result += lines.ending;
    return result;
}

std::wstring Fold(std::wstring_view value) {
    std::wstring result(value);
    std::ranges::transform(result, result.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return result;
}
}  // namespace

std::wstring SortLines(std::wstring_view text, LineOrder order, bool unique, bool ignore_case) {
    auto lines = ParseLines(text);
    if (order == LineOrder::reverse) {
        std::ranges::reverse(lines.values);
    } else {
        std::ranges::stable_sort(lines.values, [=](const auto& left, const auto& right) {
            const auto a = ignore_case ? Fold(left) : left;
            const auto b = ignore_case ? Fold(right) : right;
            return order == LineOrder::ascending ? a < b : a > b;
        });
    }
    if (unique) {
        const auto same = [=](const auto& a, const auto& b) {
            return ignore_case ? Fold(a) == Fold(b) : a == b;
        };
        lines.values.erase(std::unique(lines.values.begin(), lines.values.end(), same),
                           lines.values.end());
    }
    return Compose(lines);
}

std::wstring RemoveBlankLines(std::wstring_view text, bool whitespace_only) {
    auto lines = ParseLines(text);
    std::erase_if(lines.values, [=](const auto& line) {
        return line.empty() || (whitespace_only && std::ranges::all_of(
            line, [](wchar_t c) { return std::iswspace(c) != 0; }));
    });
    if (lines.values.empty()) lines.terminated = false;
    return Compose(lines);
}

std::wstring TrimTrailingWhitespace(std::wstring_view text) {
    auto lines = ParseLines(text);
    for (auto& line : lines.values)
        while (!line.empty() && (line.back() == L' ' || line.back() == L'\t')) line.pop_back();
    return Compose(lines);
}

std::wstring JoinLines(std::wstring_view text, std::wstring_view separator) {
    auto lines = ParseLines(text);
    std::wstring result;
    for (const auto& line : lines.values) {
        const auto first = line.find_first_not_of(L" \t");
        const auto last = line.find_last_not_of(L" \t");
        if (first == std::wstring::npos) continue;
        if (!result.empty()) result += separator;
        result.append(line, first, last - first + 1);
    }
    return result;
}

std::wstring SplitLines(std::wstring_view text, std::size_t column) {
    if (column == 0) return std::wstring(text);
    auto lines = ParseLines(text);
    std::vector<std::wstring> wrapped;
    for (auto line : lines.values) {
        while (line.size() > column) {
            auto split = line.rfind(L' ', column);
            if (split == std::wstring::npos || split == 0) split = column;
            wrapped.push_back(line.substr(0, split));
            line.erase(0, split);
            line.erase(0, line.find_first_not_of(L" \t"));
        }
        wrapped.push_back(std::move(line));
    }
    lines.values = std::move(wrapped);
    return Compose(lines);
}

std::wstring TabsToSpaces(std::wstring_view text, std::size_t tab_width) {
    if (tab_width == 0) return std::wstring(text);
    std::wstring result;
    std::size_t column = 0;
    for (const auto c : text) {
        if (c == L'\t') {
            const auto count = tab_width - column % tab_width;
            result.append(count, L' ');
            column += count;
        } else {
            result += c;
            column = c == L'\r' || c == L'\n' ? 0 : column + 1;
        }
    }
    return result;
}

std::wstring SpacesToTabs(std::wstring_view text, std::size_t tab_width) {
    if (tab_width == 0) return std::wstring(text);
    std::wstring result;
    std::size_t column = 0, spaces = 0;
    const auto flush = [&] {
        while (spaces) {
            const auto to_stop = tab_width - column % tab_width;
            if (spaces >= to_stop && to_stop > 1) {
                result += L'\t'; spaces -= to_stop; column += to_stop;
            } else {
                result += L' '; --spaces; ++column;
            }
        }
    };
    for (const auto c : text) {
        if (c == L' ') { ++spaces; continue; }
        flush();
        result += c;
        column = c == L'\r' || c == L'\n' ? 0 : column + 1;
    }
    flush();
    return result;
}

std::wstring ConvertCase(std::wstring_view text, LetterCase letter_case) {
    std::wstring result(text);
    bool new_word = true, new_sentence = true;
    for (auto& c : result) {
        if (std::iswalpha(c)) {
            const bool upper = letter_case == LetterCase::upper ||
                (letter_case == LetterCase::title && new_word) ||
                (letter_case == LetterCase::sentence && new_sentence);
            c = static_cast<wchar_t>(upper ? std::towupper(c) : std::towlower(c));
            new_word = false;
            new_sentence = false;
        } else {
            new_word = std::iswspace(c) != 0 || std::iswpunct(c) != 0;
            if (c == L'.' || c == L'!' || c == L'?') new_sentence = true;
        }
    }
    return result;
}

std::wstring EscapeJsonString(std::wstring_view text) {
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring result;
    for (const auto c : text) {
        switch (c) {
        case L'\\': result += L"\\\\"; break; case L'\"': result += L"\\\""; break;
        case L'\b': result += L"\\b"; break; case L'\f': result += L"\\f"; break;
        case L'\n': result += L"\\n"; break; case L'\r': result += L"\\r"; break;
        case L'\t': result += L"\\t"; break;
        default:
            if (c < 0x20) {
                result += L"\\u00"; result += hex[(c >> 4) & 15]; result += hex[c & 15];
            } else result += c;
        }
    }
    return result;
}

std::optional<std::wstring> UnescapeJsonString(std::wstring_view text) {
    std::wstring result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'\\') { result += text[i]; continue; }
        if (++i == text.size()) return std::nullopt;
        switch (text[i]) {
        case L'\\': result += L'\\'; break; case L'\"': result += L'\"'; break;
        case L'/': result += L'/'; break; case L'b': result += L'\b'; break;
        case L'f': result += L'\f'; break; case L'n': result += L'\n'; break;
        case L'r': result += L'\r'; break; case L't': result += L'\t'; break;
        default: return std::nullopt;
        }
    }
    return result;
}

std::string Base64Encode(std::string_view bytes) {
    constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
        const auto c = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
        result += table[a >> 2]; result += table[((a & 3) << 4) | (b >> 4)];
        result += i + 1 < bytes.size() ? table[((b & 15) << 2) | (c >> 6)] : '=';
        result += i + 2 < bytes.size() ? table[c & 63] : '=';
    }
    return result;
}

std::optional<std::string> Base64Decode(std::string_view encoded) {
    if (encoded.size() % 4) return std::nullopt;
    const std::string table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        unsigned value = 0;
        int padding = 0;
        for (std::size_t j = 0; j < 4; ++j) {
            if (encoded[i + j] == '=') { ++padding; value <<= 6; continue; }
            const auto found = table.find(encoded[i + j]);
            if (found == std::string::npos || padding) return std::nullopt;
            value = (value << 6) | static_cast<unsigned>(found);
        }
        if (padding > 2 || (padding && i + 4 != encoded.size())) return std::nullopt;
        result += static_cast<char>((value >> 16) & 0xff);
        if (padding < 2) result += static_cast<char>((value >> 8) & 0xff);
        if (padding < 1) result += static_cast<char>(value & 0xff);
    }
    return result;
}

std::string UrlEncode(std::string_view bytes) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const auto byte : bytes) {
        const auto value = static_cast<unsigned char>(byte);
        if (std::isalnum(value) || value == '-' || value == '_' || value == '.' || value == '~')
            result += static_cast<char>(value);
        else {
            result += '%'; result += hex[value >> 4]; result += hex[value & 15];
        }
    }
    return result;
}

std::optional<std::string> UrlDecode(std::string_view encoded) {
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string result;
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '+') { result += ' '; continue; }
        if (encoded[i] != '%') { result += encoded[i]; continue; }
        if (i + 2 >= encoded.size()) return std::nullopt;
        const auto high = digit(encoded[i + 1]), low = digit(encoded[i + 2]);
        if (high < 0 || low < 0) return std::nullopt;
        result += static_cast<char>((high << 4) | low);
        i += 2;
    }
    return result;
}

std::wstring GenerateSequence(long long start, std::size_t count, long long step,
                              std::wstring_view separator) {
    std::wstring result;
    auto value = start;
    for (std::size_t index = 0; index < count; ++index) {
        if (index) result += separator;
        result += std::to_wstring(value);
        value += step;
    }
    return result;
}

std::wstring EnsureFinalNewline(std::wstring_view text, std::wstring_view newline) {
    if (text.empty() || text.ends_with(L"\n") || text.ends_with(L"\r")) return std::wstring(text);
    return std::wstring(text) + std::wstring(newline);
}
}  // namespace notepad_colon
