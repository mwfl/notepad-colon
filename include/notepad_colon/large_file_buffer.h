#pragma once

#include <notepad_colon/mapped_file.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace notepad_colon {

class LargeFileBuffer final {
public:
    bool Open(const std::filesystem::path& path) noexcept;
    void Close() noexcept;
    bool IsOpen() const noexcept { return source_.IsOpen(); }
    bool IsModified() const noexcept { return modified_; }
    std::uint64_t Size() const noexcept { return size_; }
    const std::filesystem::path& SourcePath() const noexcept { return source_path_; }

    std::vector<std::uint8_t> Read(std::uint64_t offset, std::size_t length) const;
    std::optional<std::uint64_t> Find(std::span<const std::uint8_t> needle,
                                      std::uint64_t start = 0,
                                      std::uint64_t end = UINT64_MAX,
                                      bool match_case = true) const;
    std::optional<MappedTextWindow> ReadTextWindow(std::uint64_t offset, std::size_t length,
                                                   EncodingKind encoding,
                                                   unsigned int ansi_code_page = 0) const;
    bool Insert(std::uint64_t offset, std::span<const std::uint8_t> bytes);
    bool Erase(std::uint64_t offset, std::uint64_t length);
    bool Replace(std::uint64_t offset, std::uint64_t length,
                 std::span<const std::uint8_t> bytes);
    bool SaveAs(const std::filesystem::path& path) noexcept;

private:
    enum class Storage { original, added };
    struct Piece {
        Storage storage = Storage::original;
        std::uint64_t offset = 0;
        std::uint64_t length = 0;
    };

    bool SplitAt(std::uint64_t offset, std::size_t& index);
    void Normalize() noexcept;

    std::filesystem::path source_path_;
    MappedFile source_;
    std::vector<std::uint8_t> added_;
    std::vector<Piece> pieces_;
    std::uint64_t size_ = 0;
    bool modified_ = false;
    std::uint64_t source_disk_size_ = 0;
    std::filesystem::file_time_type source_last_write_{};
};

}  // namespace notepad_colon
