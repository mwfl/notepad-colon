#pragma once

#include <notepad_colon/document.h>

#include <string>
#include <string_view>

namespace notepad_colon {

LineEnding DetectLineEnding(std::wstring_view text) noexcept;
std::wstring NormalizeLineEndings(std::wstring_view text, LineEnding ending);
std::size_t CountLines(std::wstring_view text) noexcept;

}  // namespace notepad_colon

