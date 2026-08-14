#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace notepad_colon {

enum class EncodingKind { utf8, utf8_bom, utf16_le, utf16_be, ansi, binary };

struct EolDistribution {
    std::size_t crlf = 0;
    std::size_t lf = 0;
    std::size_t cr = 0;
    bool Mixed() const noexcept;
};

struct UnicodeRisk {
    std::size_t character_index = 0;
    wchar_t value = 0;
};

struct EncodingAnalysis {
    EncodingKind encoding = EncodingKind::utf8;
    unsigned int code_page = 65001;
    bool valid = true;
    bool has_bom = false;
    bool ascii_only = false;
    bool contains_nul = false;
    std::vector<std::size_t> invalid_byte_offsets;
    EolDistribution eol;
    std::vector<UnicodeRisk> unicode_risks;
};

struct EncodedWriteExpectation {
    std::uintmax_t size = 0;
    std::filesystem::file_time_type last_write{};
    bool exists = false;
};

EncodingAnalysis AnalyzeEncoding(std::span<const std::uint8_t> bytes,
                                 unsigned int ansi_code_page = 0);
std::optional<std::wstring> DecodeBytes(std::span<const std::uint8_t> bytes,
                                        EncodingKind encoding,
                                        unsigned int ansi_code_page = 0);
std::optional<std::vector<std::uint8_t>> EncodeText(std::wstring_view text,
                                                    EncodingKind encoding,
                                                    unsigned int ansi_code_page = 0);
bool WriteEncodedFileAtomic(const std::filesystem::path& path, std::wstring_view text,
                            EncodingKind encoding, unsigned int ansi_code_page = 0,
                            std::optional<EncodedWriteExpectation> expected = std::nullopt) noexcept;

}  // namespace notepad_colon
