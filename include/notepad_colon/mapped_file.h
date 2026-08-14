#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stop_token>
#include <optional>
#include <vector>

namespace notepad_colon {

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

private:
    void* file_ = reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));
    void* mapping_ = nullptr;
    std::uint64_t size_ = 0;
};

}  // namespace notepad_colon
