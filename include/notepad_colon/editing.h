#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace notepad_colon {

enum class LineOrder { ascending, descending, reverse };
enum class LetterCase { upper, lower, title, sentence };

std::wstring SortLines(std::wstring_view text, LineOrder order, bool unique = false,
                       bool ignore_case = false);
std::wstring RemoveBlankLines(std::wstring_view text, bool whitespace_only = true);
std::wstring TrimTrailingWhitespace(std::wstring_view text);
std::wstring JoinLines(std::wstring_view text, std::wstring_view separator = L" ");
std::wstring SplitLines(std::wstring_view text, std::size_t column);
std::wstring TabsToSpaces(std::wstring_view text, std::size_t tab_width);
std::wstring SpacesToTabs(std::wstring_view text, std::size_t tab_width);
std::wstring ConvertCase(std::wstring_view text, LetterCase letter_case);
std::wstring EscapeJsonString(std::wstring_view text);
std::optional<std::wstring> UnescapeJsonString(std::wstring_view text);
std::string Base64Encode(std::string_view bytes);
std::optional<std::string> Base64Decode(std::string_view encoded);
std::string UrlEncode(std::string_view bytes);
std::optional<std::string> UrlDecode(std::string_view encoded);
std::wstring GenerateSequence(long long start, std::size_t count, long long step,
                              std::wstring_view separator = L"\r\n");
std::wstring EnsureFinalNewline(std::wstring_view text, std::wstring_view newline);

}  // namespace notepad_colon
