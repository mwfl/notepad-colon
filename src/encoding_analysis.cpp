#include <notepad_colon/encoding_analysis.h>

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <limits>

namespace notepad_colon {
namespace {
unsigned int EffectiveCodePage(unsigned int value) noexcept {
    return value ? value : ::GetACP();
}

bool IsContinuation(std::uint8_t value) noexcept { return (value & 0xc0u) == 0x80u; }

std::vector<std::size_t> InvalidUtf8(std::span<const std::uint8_t> bytes) {
    std::vector<std::size_t> invalid;
    for (std::size_t index = 0; index < bytes.size();) {
        const auto lead = bytes[index];
        if (lead < 0x80) { ++index; continue; }
        std::size_t length = 0;
        if (lead >= 0xc2 && lead <= 0xdf) length = 2;
        else if (lead >= 0xe0 && lead <= 0xef) length = 3;
        else if (lead >= 0xf0 && lead <= 0xf4) length = 4;
        if (!length || index + length > bytes.size()) {
            invalid.push_back(index++); continue;
        }
        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset)
            valid = valid && IsContinuation(bytes[index + offset]);
        if (valid && length == 3)
            valid = !(lead == 0xe0 && bytes[index + 1] < 0xa0) &&
                    !(lead == 0xed && bytes[index + 1] >= 0xa0);
        if (valid && length == 4)
            valid = !(lead == 0xf0 && bytes[index + 1] < 0x90) &&
                    !(lead == 0xf4 && bytes[index + 1] >= 0x90);
        if (!valid) { invalid.push_back(index++); continue; }
        index += length;
    }
    return invalid;
}

void AnalyzeText(std::wstring_view text, EncodingAnalysis& result) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto value = text[index];
        if (value == L'\r') {
            if (index + 1 < text.size() && text[index + 1] == L'\n') {
                ++result.eol.crlf; ++index;
            } else ++result.eol.cr;
        } else if (value == L'\n') ++result.eol.lf;
        const auto code = static_cast<unsigned int>(value);
        if ((code >= 0x202a && code <= 0x202e) || (code >= 0x2066 && code <= 0x2069) ||
            code == 0x200b || code == 0x200c || code == 0x200d || code == 0x2060 || code == 0xfeff)
            result.unicode_risks.push_back({index, value});
    }
}
}  // namespace

bool EolDistribution::Mixed() const noexcept {
    return static_cast<unsigned>(crlf != 0) + static_cast<unsigned>(lf != 0) +
           static_cast<unsigned>(cr != 0) > 1;
}

std::optional<std::wstring> DecodeBytes(std::span<const std::uint8_t> bytes,
                                        EncodingKind encoding, unsigned int ansi_code_page) {
    std::size_t offset = 0;
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (encoding == EncodingKind::utf8_bom) offset = bytes.size() >= 3 ? 3 : 0;
    else if (encoding == EncodingKind::utf16_le || encoding == EncodingKind::utf16_be) {
        offset = bytes.size() >= 2 && ((bytes[0] == 0xff && bytes[1] == 0xfe) ||
                                      (bytes[0] == 0xfe && bytes[1] == 0xff)) ? 2 : 0;
        if ((bytes.size() - offset) % 2) return std::nullopt;
        std::wstring text((bytes.size() - offset) / 2, L'\0');
        for (std::size_t index = 0; index < text.size(); ++index) {
            const auto a = bytes[offset + index * 2], b = bytes[offset + index * 2 + 1];
            text[index] = static_cast<wchar_t>(encoding == EncodingKind::utf16_le
                ? static_cast<unsigned>(a | (b << 8)) : static_cast<unsigned>((a << 8) | b));
        }
        return text;
    } else if (encoding == EncodingKind::ansi) {
        code_page = EffectiveCodePage(ansi_code_page); flags = 0;
    } else if (encoding == EncodingKind::binary) return std::nullopt;
    if (bytes.size() - offset > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return std::nullopt;
    const auto* input = reinterpret_cast<const char*>(bytes.data() + offset);
    const auto count = static_cast<int>(bytes.size() - offset);
    if (!count) return std::wstring{};
    const int required = ::MultiByteToWideChar(code_page, flags, input, count, nullptr, 0);
    if (required <= 0) return std::nullopt;
    std::wstring text(static_cast<std::size_t>(required), L'\0');
    if (!::MultiByteToWideChar(code_page, flags, input, count, text.data(), required)) return std::nullopt;
    return text;
}

std::optional<std::vector<std::uint8_t>> EncodeText(std::wstring_view text,
                                                    EncodingKind encoding,
                                                    unsigned int ansi_code_page) {
    if (encoding == EncodingKind::binary || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        return std::nullopt;
    std::vector<std::uint8_t> output;
    if (encoding == EncodingKind::utf16_le || encoding == EncodingKind::utf16_be) {
        if (encoding == EncodingKind::utf16_le) output = {0xff, 0xfe}; else output = {0xfe, 0xff};
        output.reserve(output.size() + text.size() * 2);
        for (const auto value : text) {
            const auto low = static_cast<std::uint8_t>(value & 0xff), high = static_cast<std::uint8_t>(value >> 8);
            if (encoding == EncodingKind::utf16_le) { output.push_back(low); output.push_back(high); }
            else { output.push_back(high); output.push_back(low); }
        }
        return output;
    }
    const UINT code_page = encoding == EncodingKind::ansi ? EffectiveCodePage(ansi_code_page) : CP_UTF8;
    const DWORD flags = encoding == EncodingKind::ansi ? WC_NO_BEST_FIT_CHARS : WC_ERR_INVALID_CHARS;
    BOOL substituted = FALSE;
    const auto count = static_cast<int>(text.size());
    const int required = count ? ::WideCharToMultiByte(code_page, flags, text.data(), count, nullptr, 0,
        nullptr, encoding == EncodingKind::ansi ? &substituted : nullptr) : 0;
    if (count && (required <= 0 || substituted)) return std::nullopt;
    if (encoding == EncodingKind::utf8_bom) output = {0xef, 0xbb, 0xbf};
    const auto start = output.size(); output.resize(start + static_cast<std::size_t>(required));
    substituted = FALSE;
    if (required && !::WideCharToMultiByte(code_page, flags, text.data(), count,
        reinterpret_cast<char*>(output.data() + start), required, nullptr,
        encoding == EncodingKind::ansi ? &substituted : nullptr)) return std::nullopt;
    if (substituted) return std::nullopt;
    return output;
}

EncodingAnalysis AnalyzeEncoding(std::span<const std::uint8_t> bytes, unsigned int ansi_code_page) {
    EncodingAnalysis result;
    result.ascii_only = std::ranges::all_of(bytes, [](auto value) { return value < 0x80; });
    result.contains_nul = std::ranges::find(bytes, std::uint8_t{0}) != bytes.end();
    if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
        result.encoding = EncodingKind::utf8_bom; result.has_bom = true;
        result.invalid_byte_offsets = InvalidUtf8(bytes.subspan(3));
        for (auto& offset : result.invalid_byte_offsets) offset += 3;
    } else if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
        result.encoding = EncodingKind::utf16_le; result.has_bom = true;
        if ((bytes.size() - 2) % 2) result.invalid_byte_offsets.push_back(bytes.size() - 1);
    } else if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) {
        result.encoding = EncodingKind::utf16_be; result.has_bom = true;
        if ((bytes.size() - 2) % 2) result.invalid_byte_offsets.push_back(bytes.size() - 1);
    } else {
        result.invalid_byte_offsets = InvalidUtf8(bytes);
        if (!result.invalid_byte_offsets.empty()) {
            result.encoding = result.contains_nul ? EncodingKind::binary : EncodingKind::ansi;
            result.code_page = EffectiveCodePage(ansi_code_page);
        }
    }
    result.valid = result.invalid_byte_offsets.empty() || result.encoding == EncodingKind::ansi;
    if (result.contains_nul && !result.has_bom && result.encoding == EncodingKind::utf8)
        result.encoding = EncodingKind::binary;
    if (const auto text = DecodeBytes(bytes, result.encoding, result.code_page)) AnalyzeText(*text, result);
    return result;
}

bool WriteEncodedFileAtomic(const std::filesystem::path& path, std::wstring_view text,
                            EncodingKind encoding, unsigned int ansi_code_page,
                            std::optional<EncodedWriteExpectation> expected) noexcept {
    try {
        const auto bytes = EncodeText(text, encoding, ansi_code_page);
        if (!bytes) return false;
        auto temporary = path;
        temporary += L".npc-tmp-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
                     std::to_wstring(::GetTickCount64());
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output || (!bytes->empty() && !output.write(
                reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size())))) {
                std::error_code ignored; std::filesystem::remove(temporary, ignored); return false;
            }
        }
        if (expected) {
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            const auto size = exists ? std::filesystem::file_size(path, error) : 0;
            const auto write = exists ? std::filesystem::last_write_time(path, error)
                                      : std::filesystem::file_time_type{};
            if (error || exists != expected->exists ||
                (exists && (size != expected->size || write != expected->last_write))) {
                std::filesystem::remove(temporary, error); return false;
            }
        }
        if (!::MoveFileExW(temporary.c_str(), path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::error_code ignored; std::filesystem::remove(temporary, ignored); return false;
        }
        return true;
    } catch (...) { return false; }
}

}  // namespace notepad_colon
