#pragma once

#include <cstdint>

namespace notepad_colon {

enum class FileOpenMode { editable, protected_read_only, unsupported };

struct LargeFilePolicy {
    std::uintmax_t editable_limit = 32ull * 1024 * 1024;
    std::uintmax_t supported_limit = 256ull * 1024 * 1024;
};

FileOpenMode ClassifyFileSize(std::uintmax_t bytes,
                              LargeFilePolicy policy = {}) noexcept;

}  // namespace notepad_colon
