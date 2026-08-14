#include <notepad_colon/text.h>

namespace notepad_colon {

LineEnding DetectLineEnding(std::wstring_view text) noexcept {
    if (text.find(L"\r\n") != std::wstring_view::npos) return LineEnding::crlf;
    if (text.find(L'\n') != std::wstring_view::npos) return LineEnding::lf;
    if (text.find(L'\r') != std::wstring_view::npos) return LineEnding::cr;
    return LineEnding::crlf;
}

std::wstring NormalizeLineEndings(std::wstring_view text, LineEnding ending) {
    const std::wstring_view delimiter = ending == LineEnding::crlf ? L"\r\n"
                                      : ending == LineEnding::lf ? L"\n" : L"\r";
    std::wstring result;
    result.reserve(text.size() + (ending == LineEnding::crlf ? text.size() / 16 : 0));
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == L'\r') {
            if (index + 1 < text.size() && text[index + 1] == L'\n') ++index;
            result.append(delimiter);
        } else if (text[index] == L'\n') {
            result.append(delimiter);
        } else {
            result.push_back(text[index]);
        }
    }
    return result;
}

std::size_t CountLines(std::wstring_view text) noexcept {
    if (text.empty()) return 1;
    std::size_t lines = 1;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == L'\r') {
            ++lines;
            if (index + 1 < text.size() && text[index + 1] == L'\n') ++index;
        } else if (text[index] == L'\n') {
            ++lines;
        }
    }
    return lines;
}

}  // namespace notepad_colon

