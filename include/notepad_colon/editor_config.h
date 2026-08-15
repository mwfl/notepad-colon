#pragma once

#include <notepad_colon/encoding_analysis.h>
#include <notepad_colon/text.h>

#include <cstdint>
#include <filesystem>
#include <optional>

namespace notepad_colon {

struct EditorConfigSettings {
    std::optional<bool> use_tabs;
    std::optional<std::uint32_t> indent_size;
    std::optional<LineEnding> line_ending;
    std::optional<EncodingKind> encoding;
    std::optional<bool> trim_trailing_whitespace;
    std::optional<bool> insert_final_newline;
};

EditorConfigSettings ResolveEditorConfig(const std::filesystem::path& file);

}  // namespace notepad_colon
