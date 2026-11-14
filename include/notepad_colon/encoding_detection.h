#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace notepad_colon {

// Legacy (non-Unicode) code pages offered in the Encoding menu and considered
// by automatic detection.
struct LegacyCodePage {
    unsigned int code_page = 0;
    std::wstring_view name;
};

struct CodePageGuess {
    unsigned int code_page = 0;  // 0 when no candidate decodes the sample.
    double confidence = 0.0;     // Share of non-ASCII characters that look typical.
    bool detected = false;       // True when the guess came from content evidence.
};

std::span<const LegacyCodePage> SupportedLegacyCodePages() noexcept;
std::wstring CodePageName(unsigned int code_page);

// Guesses the legacy code page of bytes that are not valid UTF-8. The scan is
// bounded to the first 64 KiB. `system_code_page` (0 = the process ANSI code
// page) wins ties and is the fallback when nothing looks typical.
CodePageGuess DetectLegacyCodePage(std::span<const std::uint8_t> bytes,
                                   unsigned int system_code_page = 0);

}  // namespace notepad_colon
