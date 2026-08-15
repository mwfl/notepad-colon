#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace notepad_colon {

struct GitChangedLines {
    std::vector<std::size_t> added_or_modified;
    bool repository = false;
};

GitChangedLines QueryGitChangedLines(const std::filesystem::path& file) noexcept;

}  // namespace notepad_colon
