#include <notepad_colon/encoding_analysis.h>
#include <notepad_colon/encoding_detection.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <limits>

namespace notepad_colon {
namespace {
unsigned int EffectiveCodePage(unsigned int value) noexcept {
    return value ? value : ::GetACP();
}

struct FileHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~FileHandle() { if (value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
};

bool IsContinuation(std::uint8_t value) noexcept { return (value & 0xc0u) == 0x80u; }

bool ValidUtf16(std::wstring_view text) noexcept {
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto unit = static_cast<unsigned int>(text[index]);
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++index == text.size() || text[index] < 0xdc00 || text[index] > 0xdfff) return false;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) return false;
    }
    return true;
}

std::optional<EncodedWriteExpectation> HandleState(HANDLE file) noexcept {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return std::nullopt;
    ULARGE_INTEGER size{}, write{}, identity{};
    size.LowPart = info.nFileSizeLow; size.HighPart = info.nFileSizeHigh;
    write.LowPart = info.ftLastWriteTime.dwLowDateTime; write.HighPart = info.ftLastWriteTime.dwHighDateTime;
    identity.LowPart = info.nFileIndexLow; identity.HighPart = info.nFileIndexHigh;
    // This application targets MSVC on Windows; its file_clock stores FILETIME ticks.
    const auto time = std::filesystem::file_time_type{
        std::filesystem::file_time_type::duration{static_cast<long long>(write.QuadPart)}};
    return EncodedWriteExpectation{size.QuadPart, time, true, identity.QuadPart, info.dwVolumeSerialNumber};
}

std::vector<std::size_t> InvalidUtf8(std::span<const std::uint8_t> bytes, std::size_t& total) {
    std::vector<std::size_t> invalid;
    for (std::size_t index = 0; index < bytes.size();) {
        const auto lead = bytes[index];
        if (lead < 0x80) { ++index; continue; }
        std::size_t length = 0;
        if (lead >= 0xc2 && lead <= 0xdf) length = 2;
        else if (lead >= 0xe0 && lead <= 0xef) length = 3;
        else if (lead >= 0xf0 && lead <= 0xf4) length = 4;
        if (!length || index + length > bytes.size()) {
            ++total; if (invalid.size() < 4096) invalid.push_back(index); ++index; continue;
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
        if (!valid) { ++total; if (invalid.size() < 4096) invalid.push_back(index); ++index; continue; }
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
        {
            ++result.unicode_risk_count;
            if (result.unicode_risks.size() < 4096) result.unicode_risks.push_back({index, value});
        }
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
    if (encoding == EncodingKind::utf8_bom)
        offset = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
    else if (encoding == EncodingKind::utf16_le || encoding == EncodingKind::utf16_be) {
        if (bytes.size() >= 2 &&
            ((encoding == EncodingKind::utf16_le && bytes[0] == 0xfe && bytes[1] == 0xff) ||
             (encoding == EncodingKind::utf16_be && bytes[0] == 0xff && bytes[1] == 0xfe))) return std::nullopt;
        offset = bytes.size() >= 2 && ((bytes[0] == 0xff && bytes[1] == 0xfe) ||
                                      (bytes[0] == 0xfe && bytes[1] == 0xff)) ? 2 : 0;
        if ((bytes.size() - offset) % 2) return std::nullopt;
        std::wstring text((bytes.size() - offset) / 2, L'\0');
        for (std::size_t index = 0; index < text.size(); ++index) {
            const auto a = bytes[offset + index * 2], b = bytes[offset + index * 2 + 1];
            text[index] = static_cast<wchar_t>(encoding == EncodingKind::utf16_le
                ? static_cast<unsigned>(a | (b << 8)) : static_cast<unsigned>((a << 8) | b));
        }
        return ValidUtf16(text) ? std::optional{std::move(text)} : std::nullopt;
    } else if (encoding == EncodingKind::ansi) {
        code_page = EffectiveCodePage(ansi_code_page);
    } else if (encoding == EncodingKind::binary) return std::nullopt;
    if (bytes.size() - offset > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return std::nullopt;
    const auto count = static_cast<int>(bytes.size() - offset);
    if (!count) return std::wstring{};
    const auto* input = reinterpret_cast<const char*>(bytes.data() + offset);
    const int required = ::MultiByteToWideChar(code_page, flags, input, count, nullptr, 0);
    if (required <= 0) return std::nullopt;
    std::wstring text(static_cast<std::size_t>(required), L'\0');
    if (!::MultiByteToWideChar(code_page, flags, input, count, text.data(), required)) return std::nullopt;
    if (encoding == EncodingKind::ansi) {
        const auto round_trip = EncodeText(text, encoding, code_page);
        if (!round_trip || round_trip->size() != bytes.size() ||
            !std::equal(round_trip->begin(), round_trip->end(), bytes.begin())) return std::nullopt;
    }
    return text;
}

std::optional<std::vector<std::uint8_t>> EncodeText(std::wstring_view text,
                                                    EncodingKind encoding,
                                                    unsigned int ansi_code_page, bool unicode_bom) {
    if (encoding == EncodingKind::binary || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        return std::nullopt;
    if (!ValidUtf16(text)) return std::nullopt;
    std::vector<std::uint8_t> output;
    if (encoding == EncodingKind::utf16_le || encoding == EncodingKind::utf16_be) {
        if (unicode_bom) {
            if (encoding == EncodingKind::utf16_le) output = {0xff, 0xfe}; else output = {0xfe, 0xff};
        }
        output.reserve(output.size() + text.size() * 2);
        for (const auto value : text) {
            const auto low = static_cast<std::uint8_t>(value & 0xff), high = static_cast<std::uint8_t>(value >> 8);
            if (encoding == EncodingKind::utf16_le) { output.push_back(low); output.push_back(high); }
            else { output.push_back(high); output.push_back(low); }
        }
        return output;
    }
    const UINT code_page = encoding == EncodingKind::ansi ? EffectiveCodePage(ansi_code_page) : CP_UTF8;
    // GB18030 and UTF-8 cover all of Unicode: Windows rejects the best-fit flag
    // and the substitution out-parameter for them, and neither can be lossy.
    const bool total = code_page == CP_UTF8 || code_page == 54936;
    const DWORD flags = total ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    BOOL substituted = FALSE;
    const auto count = static_cast<int>(text.size());
    const int required = count ? ::WideCharToMultiByte(code_page, flags, text.data(), count, nullptr, 0,
        nullptr, total ? nullptr : &substituted) : 0;
    if (count && (required <= 0 || substituted)) return std::nullopt;
    if (encoding == EncodingKind::utf8_bom) output = {0xef, 0xbb, 0xbf};
    const auto start = output.size(); output.resize(start + static_cast<std::size_t>(required));
    substituted = FALSE;
    if (required && !::WideCharToMultiByte(code_page, flags, text.data(), count,
        reinterpret_cast<char*>(output.data() + start), required, nullptr,
        total ? nullptr : &substituted)) return std::nullopt;
    if (substituted) return std::nullopt;
    return output;
}

EncodingAnalysis AnalyzeEncoding(std::span<const std::uint8_t> bytes, unsigned int ansi_code_page) {
    EncodingAnalysis result;
    result.ascii_only = std::ranges::all_of(bytes, [](auto value) { return value < 0x80; });
    result.contains_nul = std::ranges::find(bytes, std::uint8_t{0}) != bytes.end();
    if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
        result.encoding = EncodingKind::utf8_bom; result.has_bom = true;
        result.invalid_byte_offsets = InvalidUtf8(bytes.subspan(3), result.invalid_byte_count);
        for (auto& offset : result.invalid_byte_offsets) offset += 3;
    } else if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
        result.encoding = EncodingKind::utf16_le; result.has_bom = true;
        if ((bytes.size() - 2) % 2) { result.invalid_byte_offsets.push_back(bytes.size() - 1); ++result.invalid_byte_count; }
    } else if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) {
        result.encoding = EncodingKind::utf16_be; result.has_bom = true;
        if ((bytes.size() - 2) % 2) { result.invalid_byte_offsets.push_back(bytes.size() - 1); ++result.invalid_byte_count; }
    } else {
        result.invalid_byte_offsets = InvalidUtf8(bytes, result.invalid_byte_count);
        if (!result.invalid_byte_offsets.empty()) {
            result.encoding = result.contains_nul ? EncodingKind::binary : EncodingKind::ansi;
            // An explicit code page is a user decision; otherwise infer the
            // legacy encoding from the content before trusting the system one.
            result.code_page = ansi_code_page != 0 || result.contains_nul
                ? EffectiveCodePage(ansi_code_page)
                : DetectLegacyCodePage(bytes).code_page;
        }
    }
    result.valid = result.invalid_byte_offsets.empty() || result.encoding == EncodingKind::ansi;
    if (result.contains_nul && !result.has_bom && result.encoding == EncodingKind::utf8)
        result.encoding = EncodingKind::binary;
    if (result.encoding == EncodingKind::utf16_le) result.code_page = 1200;
    else if (result.encoding == EncodingKind::utf16_be) result.code_page = 1201;
    if (const auto text = DecodeBytes(bytes, result.encoding, result.code_page)) {
        AnalyzeText(*text, result);
    } else result.valid = false;
    return result;
}

std::optional<EncodedWriteExpectation> ReadEncodedFileState(const std::filesystem::path& path) noexcept {
    const auto file = ::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const auto error = ::GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return EncodedWriteExpectation{};
        return std::nullopt;
    }
    const auto state = HandleState(file);
    ::CloseHandle(file);
    return state;
}

std::optional<EncodedWriteExpectation> ReadEncodedFileHandleState(void* handle) noexcept {
    return HandleState(static_cast<HANDLE>(handle));
}

bool MatchesEncodedFileState(const EncodedWriteExpectation& expected,
                             const EncodedWriteExpectation& current) noexcept {
    return expected.exists == current.exists && (!expected.exists ||
        (expected.size == current.size && expected.last_write == current.last_write &&
         (!expected.file_index || (expected.file_index == current.file_index && expected.volume_serial == current.volume_serial))));
}

bool IsSameFileOrAppend(const EncodedWriteExpectation& before,
                        const EncodedWriteExpectation& after) noexcept {
    return before == after || (before.exists && after.exists && before.file_index != 0 &&
        before.file_index == after.file_index && before.volume_serial == after.volume_serial &&
        after.size > before.size);
}

EncodingAnalysis AnalyzeDecodedText(std::wstring_view text, EncodingKind encoding,
                                    unsigned int code_page, bool has_bom) {
    EncodingAnalysis analysis;
    analysis.encoding = encoding;
    analysis.code_page = code_page ? code_page : encoding == EncodingKind::utf16_le ? 1200u :
        encoding == EncodingKind::utf16_be ? 1201u : encoding == EncodingKind::ansi ? EffectiveCodePage(0) : CP_UTF8;
    analysis.has_bom = has_bom;
    analysis.ascii_only = !has_bom && (encoding == EncodingKind::utf8 || encoding == EncodingKind::ansi) &&
        std::ranges::all_of(text, [](wchar_t value) { return value < 0x80; });
    analysis.contains_nul = text.find(L'\0') != std::wstring_view::npos;
    AnalyzeText(text, analysis);
    return analysis;
}

std::optional<DecodedFileText> DecodeFileText(std::span<const std::uint8_t> bytes,
    unsigned int legacy_code_page, std::optional<EncodingKind> force_encoding,
    std::optional<EncodingKind> fallback_encoding) {
    const bool utf8_bom = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf;
    const bool le_bom = bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe;
    const bool be_bom = bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff;
    auto encoding = force_encoding.value_or(utf8_bom ? EncodingKind::utf8_bom :
        le_bom ? EncodingKind::utf16_le : be_bom ? EncodingKind::utf16_be : EncodingKind::utf8);
    const bool nul = std::ranges::find(bytes, std::uint8_t{0}) != bytes.end();
    if (!force_encoding && !le_bom && !be_bom && !utf8_bom && nul) {
        if (fallback_encoding == EncodingKind::utf16_le || fallback_encoding == EncodingKind::utf16_be)
            encoding = *fallback_encoding;
        else return std::nullopt; // Reject binary data before allocating a decoded copy.
    }
    unsigned int page = legacy_code_page;
    if (!force_encoding && !nul && !utf8_bom && !le_bom && !be_bom && legacy_code_page &&
        std::ranges::all_of(bytes, [](auto value) { return value < 0x80; })) encoding = EncodingKind::ansi;
    auto text = DecodeBytes(bytes, encoding, page);
    if (!text && !force_encoding && !utf8_bom && !le_bom && !be_bom && !nul && encoding == EncodingKind::utf8) {
        encoding = EncodingKind::ansi;
        page = legacy_code_page ? legacy_code_page : DetectLegacyCodePage(bytes).code_page;
        text = DecodeBytes(bytes, encoding, page);
    }
    // Scintilla SetText accepts a terminated string; an embedded NUL must
    // never truncate a file silently. Decode just once on the Unicode path.
    if (!text || text->find(L'\0') != std::wstring::npos) return std::nullopt;
    const bool bom = (encoding == EncodingKind::utf8_bom && utf8_bom) ||
        (encoding == EncodingKind::utf16_le && le_bom) || (encoding == EncodingKind::utf16_be && be_bom);
    auto analysis = AnalyzeDecodedText(*text, encoding, encoding == EncodingKind::ansi ? EffectiveCodePage(page) : 0, bom);
    return DecodedFileText{std::move(*text), std::move(analysis)};
}

std::optional<EncodedFileText> ReadEncodedFileText(const std::filesystem::path& path,
    std::uintmax_t maximum_size, unsigned int legacy_code_page,
    std::optional<EncodingKind> force_encoding, std::optional<EncodingKind> fallback_encoding,
    bool allow_live_writes) {
    HANDLE file = INVALID_HANDLE_VALUE;
    try {
        const DWORD sharing = FILE_SHARE_READ | FILE_SHARE_DELETE | (allow_live_writes ? FILE_SHARE_WRITE : 0);
        file = ::CreateFileW(path.c_str(), GENERIC_READ, sharing,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) return std::nullopt;
        const auto before = HandleState(file);
        if (!before || before->size > maximum_size || before->size > (std::numeric_limits<std::size_t>::max)()) {
            ::CloseHandle(file); return std::nullopt;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(before->size));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto count = static_cast<DWORD>((std::min<std::size_t>)(1024 * 1024, bytes.size() - offset));
            DWORD read = 0;
            if (!::ReadFile(file, bytes.data() + offset, count, &read, nullptr) || read != count) {
                ::CloseHandle(file); return std::nullopt;
            }
            offset += read;
        }
        const auto after = HandleState(file);
        const auto matches = [&](const auto& current) {
            return current && (allow_live_writes ? IsSameFileOrAppend(*before, *current) : *current == *before);
        };
        if (!matches(after)) { ::CloseHandle(file); return std::nullopt; }
        auto decoded = DecodeFileText(bytes, legacy_code_page, force_encoding, fallback_encoding);
        const auto current = ReadEncodedFileState(path);
        ::CloseHandle(file); file = INVALID_HANDLE_VALUE;
        if (!decoded || !matches(current)) return std::nullopt;
        EncodedFileText result;
        result.text = std::move(decoded->text);
        result.analysis = std::move(decoded->analysis);
        result.state = *before;
        return result;
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) ::CloseHandle(file);
        return std::nullopt;
    }
}

bool WriteEncodedFileAtomic(const std::filesystem::path& path, std::wstring_view text,
                            EncodingKind encoding, unsigned int ansi_code_page,
                            std::optional<EncodedWriteExpectation> expected,
                            bool unicode_bom, EncodedWriteExpectation* written_state) noexcept {
    HANDLE file = INVALID_HANDLE_VALUE;
    std::filesystem::path temporary;
    try {
        const auto bytes = EncodeText(text, encoding, ansi_code_page, unicode_bom);
        if (!bytes) return false;
        static std::atomic_uint64_t sequence{0};
        temporary = path.wstring() + L".npc-tmp-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
                     std::to_wstring(::GetTickCount64()) + L"-" + std::to_wstring(++sequence);
        file = ::CreateFileW(temporary.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        std::size_t offset = 0;
        bool success = true;
        while (offset < bytes->size() && success) {
            const auto count = static_cast<DWORD>((std::min<std::size_t>)(1024 * 1024, bytes->size() - offset));
            DWORD written = 0;
            success = ::WriteFile(file, bytes->data() + offset, count, &written, nullptr) && written == count;
            offset += written;
        }
        if (success) success = ::FlushFileBuffers(file) != FALSE;
        ::CloseHandle(file); file = INVALID_HANDLE_VALUE;
        const auto replacement_state = success ? ReadEncodedFileState(temporary) : std::nullopt;
        const auto current = ReadEncodedFileState(path);
        if (!replacement_state || !current || (expected && !MatchesEncodedFileState(*expected, *current))) {
            ::DeleteFileW(temporary.c_str()); return false;
        }
        FileHandle guard;
        if (current->exists) {
            guard.value = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (guard.value == INVALID_HANDLE_VALUE || HandleState(guard.value) != current || ReadEncodedFileState(path) != current) {
                ::DeleteFileW(temporary.c_str()); return false;
            }
        }
        success = current->exists
            ? ::ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr) != FALSE
            : ::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!success) { ::DeleteFileW(temporary.c_str()); return false; }
        if (written_state) {
            const auto saved = ReadEncodedFileState(path);
            // A competing writer after the commit must not become our baseline.
            if (saved != replacement_state) return false;
            *written_state = *saved;
        }
        return true;
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) ::CloseHandle(file);
        if (!temporary.empty()) ::DeleteFileW(temporary.c_str());
        return false;
    }
}

}  // namespace notepad_colon
