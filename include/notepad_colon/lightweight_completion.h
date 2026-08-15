#pragma once

#include <notepad_colon/language.h>
#include <notepad_colon/tree_sitter_document.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace notepad_colon {

struct CompletionResult {
  std::size_t prefix_bytes = 0;
  std::vector<std::string> candidates;
};

CompletionResult CompleteLocally(std::string_view utf8, std::size_t caret_byte,
                                 Language language,
                                 std::span<const DocumentSymbol> symbols = {},
                                 std::size_t maximum_candidates = 256);

} // namespace notepad_colon
