#include <notepad_colon/wasm_syntax_protocol.h>

#include <tree_sitter/api.h>
#include <wasm.h>
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace notepad_colon;
using namespace notepad_colon::wasm_protocol;

bool ReadAll(HANDLE pipe, std::span<std::uint8_t> bytes) {
  while (!bytes.empty()) {
    DWORD read = 0;
    const auto count =
        static_cast<DWORD>((std::min<std::size_t>)(bytes.size(), 1024u * 1024));
    if (!::ReadFile(pipe, bytes.data(), count, &read, nullptr) || read == 0)
      return false;
    bytes = bytes.subspan(read);
  }
  return true;
}

bool WriteAll(HANDLE pipe, std::span<const std::uint8_t> bytes) {
  while (!bytes.empty()) {
    DWORD written = 0;
    const auto count =
        static_cast<DWORD>((std::min<std::size_t>)(bytes.size(), 1024u * 1024));
    if (!::WriteFile(pipe, bytes.data(), count, &written, nullptr) ||
        written != count)
      return false;
    bytes = bytes.subspan(written);
  }
  return true;
}

SyntaxKind CaptureKind(std::string_view capture) {
  if (capture == "comment")
    return SyntaxKind::comment;
  if (capture == "string")
    return SyntaxKind::string;
  if (capture == "number")
    return SyntaxKind::number;
  if (capture == "keyword")
    return SyntaxKind::keyword;
  if (capture == "type")
    return SyntaxKind::type;
  if (capture == "function")
    return SyntaxKind::function;
  if (capture == "property")
    return SyntaxKind::property;
  if (capture == "variable")
    return SyntaxKind::variable;
  if (capture == "constant")
    return SyntaxKind::constant;
  if (capture == "preprocessor")
    return SyntaxKind::preprocessor;
  return SyntaxKind::none;
}

TSPoint Point(std::uint32_t row, std::uint32_t column) { return {row, column}; }

struct HostState {
  wasm_engine_t *engine = nullptr;
  TSParser *parser = nullptr;
  TSTree *tree = nullptr;
  const TSLanguage *language = nullptr;
  TSQuery *highlights = nullptr;
  TSQuery *symbols = nullptr;
  std::string source;

  ~HostState() { Reset(); }
  void Reset() {
    if (symbols) {
      ts_query_delete(symbols);
      symbols = nullptr;
    }
    if (highlights) {
      ts_query_delete(highlights);
      highlights = nullptr;
    }
    if (tree) {
      ts_tree_delete(tree);
      tree = nullptr;
    }
    auto *store = parser ? ts_parser_take_wasm_store(parser) : nullptr;
    if (parser)
      static_cast<void>(ts_parser_set_language(parser, nullptr));
    if (parser) {
      ts_parser_delete(parser);
      parser = nullptr;
    }
    if (language) {
      ts_language_delete(language);
      language = nullptr;
    }
    if (store)
      ts_wasm_store_delete(store);
    if (engine) {
      wasm_engine_delete(engine);
      engine = nullptr;
    }
    source.clear();
  }

  bool Configure(std::span<const std::uint8_t> payload) {
    Reset();
    if (payload.size() < sizeof(ConfigurePayload))
      return false;
    ConfigurePayload prefix{};
    std::memcpy(&prefix, payload.data(), sizeof(prefix));
    const std::uint64_t total = static_cast<std::uint64_t>(sizeof(prefix)) +
                                prefix.name_size + prefix.wasm_size +
                                prefix.highlights_size + prefix.symbols_size;
    if (total != payload.size() || prefix.name_size == 0 ||
        prefix.name_size > 128 || prefix.wasm_size == 0 ||
        prefix.wasm_size > 16u * 1024 * 1024 ||
        prefix.highlights_size > 1024 * 1024 ||
        prefix.symbols_size > 1024 * 1024)
      return false;
    auto cursor = payload.subspan(sizeof(prefix));
    const std::string name{reinterpret_cast<const char *>(cursor.data()),
                           prefix.name_size};
    cursor = cursor.subspan(prefix.name_size);
    const auto wasm = cursor.first(prefix.wasm_size);
    cursor = cursor.subspan(prefix.wasm_size);
    const std::string_view highlight_query{
        reinterpret_cast<const char *>(cursor.data()), prefix.highlights_size};
    cursor = cursor.subspan(prefix.highlights_size);
    const std::string_view symbol_query{
        reinterpret_cast<const char *>(cursor.data()), prefix.symbols_size};

    engine = wasm_engine_new();
    if (!engine)
      return false;
    TSWasmError wasm_error{};
    auto *store = ts_wasm_store_new(engine, &wasm_error);
    if (!store) {
      std::free(wasm_error.message);
      return false;
    }
    language = ts_wasm_store_load_language(
        store, name.c_str(), reinterpret_cast<const char *>(wasm.data()),
        prefix.wasm_size, &wasm_error);
    if (!language) {
      ts_wasm_store_delete(store);
      std::free(wasm_error.message);
      return false;
    }
    parser = ts_parser_new();
    if (!parser) {
      ts_wasm_store_delete(store);
      return false;
    }
    ts_parser_set_wasm_store(parser, store);
    if (!ts_parser_set_language(parser, language))
      return false;
    std::uint32_t offset = 0;
    TSQueryError query_error = TSQueryErrorNone;
    highlights = ts_query_new(language, highlight_query.data(),
                              prefix.highlights_size, &offset, &query_error);
    if (!highlights)
      return false;
    if (!symbol_query.empty()) {
      symbols = ts_query_new(language, symbol_query.data(), prefix.symbols_size,
                             &offset, &query_error);
      if (!symbols)
        return false;
    }
    return true;
  }

  bool Parse(std::span<const std::uint8_t> payload) {
    if (!parser || payload.size() < sizeof(ParsePayload))
      return false;
    ParsePayload prefix{};
    std::memcpy(&prefix, payload.data(), sizeof(prefix));
    if (prefix.text_size > 8u * 1024 * 1024 ||
        payload.size() != sizeof(prefix) + prefix.text_size)
      return false;
    source.assign(
        reinterpret_cast<const char *>(payload.data() + sizeof(prefix)),
        prefix.text_size);
    if (prefix.incremental && tree) {
      const auto &edit = prefix.edit;
      const TSInputEdit input{edit.start_byte,
                              edit.old_end_byte,
                              edit.new_end_byte,
                              Point(edit.start_row, edit.start_column),
                              Point(edit.old_end_row, edit.old_end_column),
                              Point(edit.new_end_row, edit.new_end_column)};
      ts_tree_edit(tree, &input);
    }
    auto *next =
        ts_parser_parse_string(parser, prefix.incremental ? tree : nullptr,
                               source.data(), prefix.text_size);
    if (!next)
      return false;
    if (tree)
      ts_tree_delete(tree);
    tree = next;
    return true;
  }

  std::vector<std::uint8_t> HighlightPayload(const RangePayload &range,
                                             std::uint32_t &count) {
    std::vector<WireSpan> spans;
    if (!tree || !highlights || range.start_byte >= range.end_byte)
      return {};
    auto *cursor = ts_query_cursor_new();
    if (!cursor)
      return {};
    ts_query_cursor_set_byte_range(cursor, range.start_byte, range.end_byte);
    ts_query_cursor_exec(cursor, highlights, ts_tree_root_node(tree));
    TSQueryMatch match{};
    std::uint32_t capture_index = 0;
    while (spans.size() < 100000 &&
           ts_query_cursor_next_capture(cursor, &match, &capture_index)) {
      const auto &capture = match.captures[capture_index];
      std::uint32_t length = 0;
      const auto *name =
          ts_query_capture_name_for_id(highlights, capture.index, &length);
      const auto kind = CaptureKind({name, length});
      if (kind != SyntaxKind::none)
        spans.push_back({ts_node_start_byte(capture.node),
                         ts_node_end_byte(capture.node),
                         static_cast<std::uint32_t>(kind)});
    }
    ts_query_cursor_delete(cursor);
    std::ranges::sort(spans, {}, &WireSpan::start_byte);
    count = static_cast<std::uint32_t>(spans.size());
    std::vector<std::uint8_t> output(spans.size() * sizeof(WireSpan));
    if (!output.empty())
      std::memcpy(output.data(), spans.data(), output.size());
    return output;
  }

  std::vector<std::uint8_t> SymbolPayload(std::uint32_t &count) {
    std::vector<std::uint8_t> output;
    if (!tree || !symbols)
      return output;
    auto *cursor = ts_query_cursor_new();
    if (!cursor)
      return output;
    ts_query_cursor_exec(cursor, symbols, ts_tree_root_node(tree));
    TSQueryMatch match{};
    while (count < 50000 && ts_query_cursor_next_match(cursor, &match)) {
      for (std::uint16_t index = 0;
           index < match.capture_count && count < 50000; ++index) {
        const auto &capture = match.captures[index];
        const auto begin = ts_node_start_byte(capture.node),
                   end = ts_node_end_byte(capture.node);
        if (begin >= end || end > source.size())
          continue;
        std::uint32_t capture_length = 0;
        const auto *capture_name = ts_query_capture_name_for_id(
            symbols, capture.index, &capture_length);
        std::string_view kind{capture_name, capture_length};
        if (const auto dot = kind.find('.'); dot != std::string_view::npos)
          kind.remove_prefix(dot + 1);
        const std::string_view name{source.data() + begin, end - begin};
        const WireSymbol wire{begin, end,
                              static_cast<std::uint32_t>(name.size()),
                              static_cast<std::uint32_t>(kind.size())};
        if (output.size() + sizeof(wire) + name.size() + kind.size() >
            maximum_payload)
          break;
        const auto old = output.size();
        output.resize(old + sizeof(wire) + name.size() + kind.size());
        auto *destination = output.data() + old;
        std::memcpy(destination, &wire, sizeof(wire));
        destination += sizeof(wire);
        std::memcpy(destination, name.data(), name.size());
        destination += name.size();
        std::memcpy(destination, kind.data(), kind.size());
        ++count;
      }
    }
    ts_query_cursor_delete(cursor);
    return output;
  }
};
} // namespace

int main() {
  ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                 SEM_NOOPENFILEERRORBOX);
  const auto input = ::GetStdHandle(STD_INPUT_HANDLE),
             output = ::GetStdHandle(STD_OUTPUT_HANDLE);
  if (!input || !output || input == INVALID_HANDLE_VALUE ||
      output == INVALID_HANDLE_VALUE)
    return 2;
  HostState state;
  for (;;) {
    RequestHeader request{};
    if (!ReadAll(input,
                 {reinterpret_cast<std::uint8_t *>(&request), sizeof(request)}))
      return 0;
    if (request.magic_value != magic || request.version_value != version ||
        request.payload_size > maximum_payload)
      return 3;
    std::vector<std::uint8_t> payload(request.payload_size);
    if (!payload.empty() && !ReadAll(input, payload))
      return 4;
    if (request.command == Command::quit)
      return 0;
    ResponseHeader response{};
    std::vector<std::uint8_t> response_payload;
    bool succeeded = false;
    if (request.command == Command::configure)
      succeeded = state.Configure(payload);
    else if (request.command == Command::parse)
      succeeded = state.Parse(payload);
    else if (request.command == Command::highlights &&
             payload.size() == sizeof(RangePayload)) {
      RangePayload range{};
      std::memcpy(&range, payload.data(), sizeof(range));
      response_payload = state.HighlightPayload(range, response.count);
      succeeded = true;
    } else if (request.command == Command::symbols && payload.empty()) {
      response_payload = state.SymbolPayload(response.count);
      succeeded = true;
    }
    response.status = succeeded ? 0 : 1;
    response.payload_size = static_cast<std::uint32_t>(response_payload.size());
    if (!WriteAll(output, {reinterpret_cast<const std::uint8_t *>(&response),
                           sizeof(response)}) ||
        (!response_payload.empty() && !WriteAll(output, response_payload)))
      return 5;
  }
}
