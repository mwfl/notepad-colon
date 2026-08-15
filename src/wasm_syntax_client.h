#pragma once

#include <notepad_colon/tree_sitter_document.h>

#include <windows.h>

#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace notepad_colon {

class WasmSyntaxClient final {
public:
  WasmSyntaxClient();
  ~WasmSyntaxClient();
  WasmSyntaxClient(const WasmSyntaxClient &) = delete;
  WasmSyntaxClient &operator=(const WasmSyntaxClient &) = delete;

  bool Start(const std::filesystem::path &host, std::string_view language_name,
             std::span<const std::uint8_t> wasm,
             std::string_view highlights_query,
             std::string_view symbols_query = {}) noexcept;
  bool Parse(std::string_view utf8) noexcept;
  bool Reparse(std::string_view utf8, const SyntaxEdit &edit) noexcept;
  std::vector<SyntaxSpan> Highlights(std::uint32_t start,
                                     std::uint32_t end) noexcept;
  std::vector<DocumentSymbol> Symbols() noexcept;
  bool IsReady() const noexcept;

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace notepad_colon
