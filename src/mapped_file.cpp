#include <notepad_colon/mapped_file.h>

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace notepad_colon {

MappedFile::~MappedFile() noexcept { Close(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
    : file_(other.file_), mapping_(other.mapping_), size_(other.size_) {
    other.file_ = INVALID_HANDLE_VALUE; other.mapping_ = nullptr; other.size_ = 0;
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        Close(); file_ = other.file_; mapping_ = other.mapping_; size_ = other.size_;
        other.file_ = INVALID_HANDLE_VALUE; other.mapping_ = nullptr; other.size_ = 0;
    }
    return *this;
}

bool MappedFile::Open(const std::filesystem::path& path) noexcept {
    Close();
    const auto file = ::CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file, &size) || size.QuadPart < 0) { ::CloseHandle(file); return false; }
    HANDLE mapping = nullptr;
    if (size.QuadPart != 0) {
        mapping = ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping) { ::CloseHandle(file); return false; }
    }
    file_ = file; mapping_ = mapping; size_ = static_cast<std::uint64_t>(size.QuadPart); return true;
}

void MappedFile::Close() noexcept {
    if (mapping_) ::CloseHandle(static_cast<HANDLE>(mapping_));
    if (file_ != INVALID_HANDLE_VALUE) ::CloseHandle(static_cast<HANDLE>(file_));
    file_ = INVALID_HANDLE_VALUE; mapping_ = nullptr; size_ = 0;
}

bool MappedFile::IsOpen() const noexcept { return file_ != INVALID_HANDLE_VALUE; }

std::vector<std::uint8_t> MappedFile::Read(std::uint64_t offset, std::size_t length) const {
    if (!IsOpen() || offset >= size_ || !length || !mapping_) return {};
    const auto available = size_ - offset;
    const auto wanted = static_cast<std::size_t>((std::min)(available, static_cast<std::uint64_t>(length)));
    SYSTEM_INFO information{}; ::GetSystemInfo(&information);
    const auto granularity = static_cast<std::uint64_t>(information.dwAllocationGranularity);
    const auto aligned = offset - offset % granularity;
    const auto delta = static_cast<std::size_t>(offset - aligned);
    if (wanted > (std::numeric_limits<std::size_t>::max)() - delta) return {};
    const auto mapped_length = wanted + delta;
    const auto view = ::MapViewOfFile(static_cast<HANDLE>(mapping_), FILE_MAP_READ,
        static_cast<DWORD>(aligned >> 32), static_cast<DWORD>(aligned & 0xffffffffu), mapped_length);
    if (!view) return {};
    std::vector<std::uint8_t> output(wanted);
    std::memcpy(output.data(), static_cast<const std::uint8_t*>(view) + delta, wanted);
    ::UnmapViewOfFile(view); return output;
}

std::optional<std::uint64_t> MappedFile::Find(std::span<const std::uint8_t> needle,
                                              std::uint64_t start,
                                              std::stop_token stop) const {
    if (needle.empty() || start >= size_) return std::nullopt;
    constexpr std::size_t chunk_size = 8u * 1024 * 1024;
    auto offset = start;
    while (offset < size_ && !stop.stop_requested()) {
        auto chunk = Read(offset, chunk_size);
        if (chunk.empty()) break;
        const auto found = std::search(chunk.begin(), chunk.end(), needle.begin(), needle.end());
        if (found != chunk.end()) return offset + static_cast<std::uint64_t>(found - chunk.begin());
        if (chunk.size() < chunk_size) break;
        const auto overlap = needle.size() > 1 ? needle.size() - 1 : 0;
        offset += static_cast<std::uint64_t>(chunk.size() - (std::min)(overlap, chunk.size() - 1));
    }
    return std::nullopt;
}

}  // namespace notepad_colon
