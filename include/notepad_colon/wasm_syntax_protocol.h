#pragma once

#include <notepad_colon/tree_sitter_document.h>

#include <cstdint>

namespace notepad_colon::wasm_protocol {

constexpr std::uint32_t magic = 0x5743504e; // NPCW
constexpr std::uint16_t version = 1;
constexpr std::uint32_t maximum_payload = 32u * 1024 * 1024;

enum class Command : std::uint16_t {
  configure = 1,
  parse = 2,
  highlights = 3,
  symbols = 4,
  quit = 5
};

struct RequestHeader {
  std::uint32_t magic_value = magic;
  std::uint16_t version_value = version;
  Command command{};
  std::uint32_t payload_size = 0;
};

struct ResponseHeader {
  std::uint32_t magic_value = magic;
  std::uint16_t version_value = version;
  std::uint16_t status = 0;
  std::uint32_t count = 0;
  std::uint32_t payload_size = 0;
};

struct ConfigurePayload {
  std::uint32_t name_size = 0;
  std::uint32_t wasm_size = 0;
  std::uint32_t highlights_size = 0;
  std::uint32_t symbols_size = 0;
};

struct ParsePayload {
  SyntaxEdit edit{};
  std::uint32_t text_size = 0;
  std::uint8_t incremental = 0;
  std::uint8_t reserved[3]{};
};

struct RangePayload {
  std::uint32_t start_byte = 0;
  std::uint32_t end_byte = 0;
};
struct WireSpan {
  std::uint32_t start_byte = 0;
  std::uint32_t end_byte = 0;
  std::uint32_t kind = 0;
};
struct WireSymbol {
  std::uint32_t start_byte = 0;
  std::uint32_t end_byte = 0;
  std::uint32_t name_size = 0;
  std::uint32_t kind_size = 0;
};

} // namespace notepad_colon::wasm_protocol
