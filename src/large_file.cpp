#include <notepad_colon/large_file.h>

namespace notepad_colon {

FileOpenMode ClassifyFileSize(std::uintmax_t bytes, LargeFilePolicy policy) noexcept {
    if (policy.editable_limit > policy.supported_limit) return FileOpenMode::unsupported;
    if (bytes <= policy.editable_limit) return FileOpenMode::editable;
    if (bytes <= policy.supported_limit) return FileOpenMode::protected_read_only;
    return FileOpenMode::unsupported;
}

}  // namespace notepad_colon
