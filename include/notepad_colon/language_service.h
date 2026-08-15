#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace notepad_colon {

struct LanguageLocation {
    std::filesystem::path path;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct LanguageCompletion { std::wstring label; std::wstring detail; };

// Boundary for a future out-of-process LSP client. Implementations must never
// execute inside a Win32 callback and must return results through a UI handoff.
class LanguageService {
public:
    virtual ~LanguageService() = default;
    virtual void DidOpen(const std::filesystem::path&, std::string_view utf8) = 0;
    virtual void DidChange(std::string_view utf8, std::uint64_t revision) = 0;
    virtual std::vector<LanguageCompletion> Complete(std::uint32_t line,
                                                     std::uint32_t column) = 0;
    virtual std::vector<LanguageLocation> Definition(std::uint32_t line,
                                                     std::uint32_t column) = 0;
};

}  // namespace notepad_colon
