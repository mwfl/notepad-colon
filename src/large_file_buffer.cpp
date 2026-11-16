#include <notepad_colon/large_file_buffer.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <string>

namespace notepad_colon {
namespace {
constexpr std::size_t kIoChunk = 1024 * 1024;

bool WriteAll(HANDLE file, std::span<const std::uint8_t> bytes) noexcept {
    while (!bytes.empty()) {
        const auto count = static_cast<DWORD>((std::min<std::size_t>)(bytes.size(), MAXDWORD));
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), count, &written, nullptr) || written != count) return false;
        bytes = bytes.subspan(written);
    }
    return true;
}
}

bool LargeFileBuffer::Open(const std::filesystem::path& path, bool use_mapping) noexcept {
    Close();
    if (!source_.Open(path, use_mapping)) return false;
    try {
        source_path_ = path;
        use_mapping_ = use_mapping;
        size_ = source_.Size();
        const auto state = source_.State();
        if (!state || ReadEncodedFileState(path) != state || state->size != size_) { Close(); return false; }
        source_state_ = *state;
        if (size_ != 0) pieces_.push_back({Storage::original, 0, size_});
        return true;
    } catch (...) { Close(); return false; }
}

void LargeFileBuffer::Close() noexcept {
    source_.Close();
    source_path_.clear();
    added_.clear();
    pieces_.clear();
    size_ = 0;
    modified_ = false;
    source_state_ = {};
    checkpoint_copies_.clear();
}

std::vector<std::uint8_t> LargeFileBuffer::Read(std::uint64_t offset, std::size_t length) const {
    if (offset >= size_ || length == 0) return {};
    const auto wanted64 = (std::min)(size_ - offset, static_cast<std::uint64_t>(length));
    const auto wanted = static_cast<std::size_t>(wanted64);
    std::vector<std::uint8_t> output;
    output.reserve(wanted);
    std::uint64_t logical = 0;
    for (const auto& piece : pieces_) {
        const auto piece_end = logical + piece.length;
        if (piece_end <= offset) { logical = piece_end; continue; }
        if (logical >= offset + wanted64) break;
        const auto within = offset > logical ? offset - logical : 0;
        const auto available = piece.length - within;
        const auto count = static_cast<std::size_t>((std::min)(
            available, offset + wanted64 - (logical + within)));
        if (piece.storage == Storage::original) {
            const auto bytes = source_.Read(piece.offset + within, count);
            if (bytes.size() != count) return {};
            output.insert(output.end(), bytes.begin(), bytes.end());
        } else {
            const auto begin = added_.begin() + static_cast<std::ptrdiff_t>(piece.offset + within);
            output.insert(output.end(), begin, begin + static_cast<std::ptrdiff_t>(count));
        }
        logical = piece_end;
    }
    return output.size() == wanted ? output : std::vector<std::uint8_t>{};
}

std::optional<std::uint64_t> LargeFileBuffer::Find(
    std::span<const std::uint8_t> needle, std::uint64_t start, std::uint64_t end,
    bool match_case) const {
    if (needle.empty() || start >= size_) return std::nullopt;
    end = (std::min)(end, size_);
    if (end <= start || needle.size() > end - start) return std::nullopt;

    const auto equal = [match_case](std::uint8_t left, std::uint8_t right) {
        if (match_case) return left == right;
        const auto fold = [](std::uint8_t value) {
            return value >= 'A' && value <= 'Z' ? static_cast<std::uint8_t>(value + ('a' - 'A'))
                                                : value;
        };
        return fold(left) == fold(right);
    };
    const auto overlap = needle.size() - 1;
    std::vector<std::uint8_t> carry;
    std::uint64_t cursor = start;
    while (cursor < end) {
        const auto count = static_cast<std::size_t>((std::min<std::uint64_t>)(kIoChunk, end - cursor));
        auto chunk = Read(cursor, count);
        if (chunk.size() != count) return std::nullopt;
        std::vector<std::uint8_t> haystack;
        haystack.reserve(carry.size() + chunk.size());
        haystack.insert(haystack.end(), carry.begin(), carry.end());
        haystack.insert(haystack.end(), chunk.begin(), chunk.end());
        const auto found = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), equal);
        if (found != haystack.end()) {
            const auto base = cursor - carry.size();
            return base + static_cast<std::uint64_t>(found - haystack.begin());
        }
        const auto keep = (std::min)(overlap, haystack.size());
        carry.assign(haystack.end() - static_cast<std::ptrdiff_t>(keep), haystack.end());
        cursor += count;
    }
    return std::nullopt;
}

std::optional<MappedTextWindow> LargeFileBuffer::ReadTextWindow(
    std::uint64_t offset, std::size_t length, EncodingKind encoding,
    unsigned int ansi_code_page) const {
    if (!IsOpen() || offset >= size_ || length == 0 || encoding == EncodingKind::binary)
        return std::nullopt;
    auto read_offset = offset;
    if ((encoding == EncodingKind::utf8 || encoding == EncodingKind::utf8_bom) && offset != 0) {
        const auto probe_start = offset > 3 ? offset - 3 : 0;
        const auto probe = Read(probe_start, static_cast<std::size_t>(offset - probe_start + 1));
        if (!probe.empty() && (probe.back() & 0xc0u) == 0x80u) {
            auto index = probe.size() - 1;
            while (index > 0 && (probe[index] & 0xc0u) == 0x80u) --index;
            read_offset = probe_start + index;
        }
    } else if ((encoding == EncodingKind::utf16_le || encoding == EncodingKind::utf16_be) &&
               offset >= 2) {
        if (offset % 2) --read_offset;
        const auto probe = Read(read_offset, 2);
        if (probe.size() == 2) {
            const auto unit = static_cast<std::uint16_t>(encoding == EncodingKind::utf16_le
                ? probe[0] | (probe[1] << 8) : (probe[0] << 8) | probe[1]);
            if (unit >= 0xdc00 && unit <= 0xdfff && read_offset >= 2) read_offset -= 2;
        }
    }
    auto bytes = Read(read_offset, length + static_cast<std::size_t>(offset - read_offset));
    if (bytes.empty()) return std::nullopt;
    const auto window_encoding = encoding == EncodingKind::utf8_bom && read_offset != 0
        ? EncodingKind::utf8 : encoding;
    const auto maximum_trim = read_offset + bytes.size() < size_ ? std::size_t{4} : std::size_t{0};
    for (std::size_t trim = 0; trim <= maximum_trim && trim <= bytes.size(); ++trim) {
        auto decoded = DecodeBytes(std::span<const std::uint8_t>{bytes}.first(bytes.size() - trim),
                                   window_encoding, ansi_code_page);
        if (!decoded) continue;
        if (!decoded->empty() && decoded->back() >= 0xd800 && decoded->back() <= 0xdbff) continue;
        const auto decoded_offset = encoding == EncodingKind::utf8_bom && read_offset == 0
            ? std::uint64_t{3} : read_offset;
        return MappedTextWindow{offset, decoded_offset, read_offset + bytes.size() - trim,
                                std::move(*decoded)};
    }
    return std::nullopt;
}

bool LargeFileBuffer::SplitAt(std::uint64_t offset, std::size_t& index) {
    if (offset > size_) return false;
    if (offset == size_) { index = pieces_.size(); return true; }
    std::uint64_t logical = 0;
    for (index = 0; index < pieces_.size(); ++index) {
        const auto& piece = pieces_[index];
        if (offset == logical) return true;
        if (offset < logical + piece.length) {
            const auto left_length = offset - logical;
            const Piece right{piece.storage, piece.offset + left_length, piece.length - left_length};
            pieces_[index].length = left_length;
            pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(index + 1), right);
            ++index;
            return true;
        }
        logical += piece.length;
    }
    return false;
}

bool LargeFileBuffer::Insert(std::uint64_t offset, std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return offset <= size_;
    if (bytes.size() > (std::numeric_limits<std::uint64_t>::max)() - size_ ||
        added_.size() > (std::numeric_limits<std::size_t>::max)() - bytes.size()) return false;
    try {
        std::size_t index = 0;
        if (!SplitAt(offset, index)) return false;
        const auto added_offset = static_cast<std::uint64_t>(added_.size());
        added_.insert(added_.end(), bytes.begin(), bytes.end());
        pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(index),
                       {Storage::added, added_offset, bytes.size()});
        size_ += bytes.size();
        modified_ = true;
        checkpoint_copies_.clear();
        Normalize();
        return true;
    } catch (...) { return false; }
}

bool LargeFileBuffer::Erase(std::uint64_t offset, std::uint64_t length) {
    if (offset > size_ || length > size_ - offset) return false;
    if (length == 0) return true;
    try {
        std::size_t begin = 0, end = 0;
        if (!SplitAt(offset, begin) || !SplitAt(offset + length, end)) return false;
        pieces_.erase(pieces_.begin() + static_cast<std::ptrdiff_t>(begin),
                      pieces_.begin() + static_cast<std::ptrdiff_t>(end));
        size_ -= length;
        modified_ = true;
        checkpoint_copies_.clear();
        Normalize();
        return true;
    } catch (...) { return false; }
}

bool LargeFileBuffer::Replace(std::uint64_t offset, std::uint64_t length,
                              std::span<const std::uint8_t> bytes) {
    if (offset > size_ || length > size_ - offset) return false;
    if (!Erase(offset, length)) return false;
    return Insert(offset, bytes);
}

void LargeFileBuffer::Normalize() noexcept {
    std::erase_if(pieces_, [](const Piece& piece) { return piece.length == 0; });
    for (std::size_t index = 1; index < pieces_.size();) {
        auto& left = pieces_[index - 1];
        const auto& right = pieces_[index];
        if (left.storage == right.storage && left.offset + left.length == right.offset) {
            left.length += right.length;
            pieces_.erase(pieces_.begin() + static_cast<std::ptrdiff_t>(index));
        } else ++index;
    }
}

std::shared_ptr<const LargeFileBuffer> LargeFileBuffer::Snapshot() const noexcept {
    try {
        auto snapshot = std::make_shared<LargeFileBuffer>();
        auto source = source_.CloneBuffered();
        if (!source || source->State() != std::optional{source_state_}) return {};
        snapshot->source_ = std::move(*source);
        snapshot->source_path_ = source_path_;
        snapshot->source_state_ = source_state_;
        snapshot->use_mapping_ = false;
        snapshot->pieces_ = pieces_;
        snapshot->added_ = added_;
        snapshot->size_ = size_;
        snapshot->modified_ = modified_;
        return snapshot;
    } catch (...) { return {}; }
}

bool LargeFileBuffer::WriteCopyAtomic(const std::filesystem::path& path, std::uint64_t& checksum) const noexcept {
    HANDLE file = INVALID_HANDLE_VALUE;
    std::filesystem::path temporary;
    try {
        if (!IsOpen() || path.empty() || path == source_path_) return false;
        auto stable_source = source_.CloneBuffered(true);
        if (!stable_source || stable_source->State() != std::optional{source_state_}) return false;
        static std::atomic_uint64_t sequence{0};
        temporary = path.wstring() + L".tmp-" + std::to_wstring(::GetCurrentProcessId()) +
            L"-" + std::to_wstring(++sequence);
        file = ::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        std::uint64_t hash = 14695981039346656037ull;
        bool succeeded = true;
        for (const auto& piece : pieces_) {
            for (std::uint64_t offset = 0; offset < piece.length && succeeded;) {
                const auto count = static_cast<std::size_t>((std::min<std::uint64_t>)(
                    kIoChunk, piece.length - offset));
                std::vector<std::uint8_t> original;
                std::span<const std::uint8_t> bytes;
                if (piece.storage == Storage::original) {
                    original = stable_source->Read(piece.offset + offset, count);
                    bytes = original;
                } else bytes = std::span<const std::uint8_t>{added_}.subspan(
                    static_cast<std::size_t>(piece.offset + offset), count);
                succeeded = bytes.size() == count && WriteAll(file, bytes);
                for (const auto byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
                offset += count;
            }
            if (!succeeded) break;
        }
        if (succeeded) succeeded = ::FlushFileBuffers(file) != FALSE;
        ::CloseHandle(file); file = INVALID_HANDLE_VALUE;
        if (source_.State() != std::optional{source_state_}) succeeded = false;
        if (succeeded) succeeded = ::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!succeeded) { ::DeleteFileW(temporary.c_str()); return false; }
        checksum = hash;
        return true;
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) ::CloseHandle(file);
        if (!temporary.empty()) ::DeleteFileW(temporary.c_str());
        return false;
    }
}

std::optional<CheckpointCopy> LargeFileBuffer::Checkpoint(const std::filesystem::path& directory) const noexcept {
    try {
        std::scoped_lock lock{checkpoint_mutex_};
        const auto found = checkpoint_copies_.find(directory);
        if (found != checkpoint_copies_.end()) {
            const auto state = ReadEncodedFileState(found->second.path);
            if (state && *state == found->second.state) return found->second;
        }
        std::filesystem::create_directories(directory);
        static std::atomic_uint64_t sequence{0};
        CheckpointCopy copy;
        copy.path = directory / (std::to_wstring(::GetCurrentProcessId()) + L"-" +
            std::to_wstring(::GetTickCount64()) + L"-" + std::to_wstring(++sequence) + L".npcrecovery");
        if (!WriteCopyAtomic(copy.path, copy.checksum)) return std::nullopt;
        copy.size = Size();
        const auto state = ReadEncodedFileState(copy.path);
        if (!state || !state->exists) return std::nullopt;
        copy.state = *state;
        checkpoint_copies_[directory] = copy;
        return copy;
    } catch (...) { return std::nullopt; }
}

bool LargeFileBuffer::SaveAs(const std::filesystem::path& path) noexcept {
    if (!IsOpen() || path.empty()) return false;
    auto stable_source = source_.CloneBuffered(true);
    if (!stable_source || stable_source->State() != std::optional{source_state_}) return false;
    auto temporary = path;
    temporary += L".notepad-colon-" + std::to_wstring(::GetCurrentProcessId()) + L".tmp";
    const auto file = ::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool succeeded = true;
    for (const auto& piece : pieces_) {
        std::uint64_t consumed = 0;
        while (consumed < piece.length && succeeded) {
            const auto count = static_cast<std::size_t>((std::min<std::uint64_t>)(
                kIoChunk, piece.length - consumed));
            if (piece.storage == Storage::original) {
                const auto bytes = stable_source->Read(piece.offset + consumed, count);
                succeeded = bytes.size() == count && WriteAll(file, bytes);
            } else {
                const auto begin = added_.data() + piece.offset + consumed;
                succeeded = WriteAll(file, {begin, count});
            }
            consumed += count;
        }
        if (!succeeded) break;
    }
    if (succeeded) succeeded = ::FlushFileBuffers(file) != FALSE;
    ::CloseHandle(file);
    if (!succeeded) { ::DeleteFileW(temporary.c_str()); return false; }
    const auto replacement_state = ReadEncodedFileState(temporary);
    if (source_.State() != std::optional{source_state_} ||
        !replacement_state || (path == source_path_ && ReadEncodedFileState(path) != std::optional{source_state_})) {
        ::DeleteFileW(temporary.c_str());
        ::SetLastError(ERROR_FILE_INVALID);
        return false;
    }
    auto retained = source_.CloneBuffered();
    if (!retained) { ::DeleteFileW(temporary.c_str()); return false; }
    source_.Close();
    // ReplaceFile permits shared-delete recovery readers and preserves the
    // destination's metadata. MoveFileEx cannot replace an open destination.
    const auto target_state = ReadEncodedFileState(path);
    const bool replaced = target_state && (target_state->exists
        ? ::ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr) != FALSE
        : ::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE);
    if (!replaced) {
        const auto error = ::GetLastError();
        source_ = std::move(*retained);
        ::DeleteFileW(temporary.c_str());
        ::SetLastError(error);
        return false;
    }
    MappedFile reopened;
    if (!reopened.Open(path, use_mapping_) || reopened.State() != replacement_state) {
        source_ = std::move(*retained);
        return false;
    }
    source_ = std::move(reopened);
    source_path_ = path;
    source_state_ = *replacement_state;
    pieces_.clear();
    if (size_ != 0) pieces_.push_back({Storage::original, 0, size_});
    added_.clear();
    modified_ = false;
    return true;
}

}  // namespace notepad_colon
