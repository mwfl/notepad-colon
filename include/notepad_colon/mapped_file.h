#pragma once

#include <notepad_colon/encoding_analysis.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stop_token>
#include <optional>
#include <vector>
#include <string>

namespace notepad_colon {

struct MappedTextWindow {
    std::uint64_t requested_offset = 0;
    std::uint64_t decoded_offset = 0;
    std::uint64_t byte_end = 0;
    std::wstring text;
};

class MappedFile final {
public:
    MappedFile() noexcept = default;
    ~MappedFile() noexcept;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    bool Open(const std::filesystem::path& path) noexcept;
    void Close() noexcept;
    bool IsOpen() const noexcept;
    std::uint64_t Size() const noexcept { return size_; }
    std::vector<std::uint8_t> Read(std::uint64_t offset, std::size_t length) const;
    std::optional<std::uint64_t> Find(std::span<const std::uint8_t> needle,
                                      std::uint64_t start = 0,
                                      std::stop_token stop = {}) const;
    std::optional<MappedTextWindow> ReadTextWindow(std::uint64_t offset, std::size_t length,
                                                   EncodingKind encoding,
                                                   unsigned int ansi_code_page = 0) const;

private:
    void* file_ = reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));
    void* mapping_ = nullptr;
    std::uint64_t size_ = 0;
};

}  // namespace notepad_colon
