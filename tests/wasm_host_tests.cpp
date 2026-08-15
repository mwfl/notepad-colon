#include "wasm_syntax_client.h"

#include <array>
#include <fstream>
#include <iostream>

int wmain(int argc, wchar_t **argv) {
  if (argc != 3)
    return 1;
  std::ifstream input(argv[2], std::ios::binary);
  const std::vector<std::uint8_t> wasm{std::istreambuf_iterator<char>{input},
                                       {}};
  if (wasm.empty())
    return 2;
  constexpr std::string_view highlights =
      "(string) @string\n(number) @number\n[(true) (false) (null)] @constant\n"
      "(pair key: (string) @property)\n";
  constexpr std::string_view symbols =
      "(pair key: (string) @symbol.property value: (_))\n";
  constexpr std::string_view source = R"({"name":"colon","count":42})";
  DWORD handles_before = 0, handles_after = 0;
  {
    notepad_colon::WasmSyntaxClient client;
    if (!client.Start(argv[1], "json", wasm, highlights, symbols) ||
        !client.Parse(source))
      return 3;
    const auto spans =
        client.Highlights(0, static_cast<std::uint32_t>(source.size()));
    if (!std::ranges::any_of(spans, [](const auto &span) {
          return span.kind == notepad_colon::SyntaxKind::property;
        }))
      return 4;
    const auto document_symbols = client.Symbols();
    if (document_symbols.size() != 2)
      return 5;
    const auto number = source.find("42");
    std::string edited{source};
    edited.replace(number, 2, "420");
    const notepad_colon::SyntaxEdit edit{
        static_cast<std::uint32_t>(number),
        static_cast<std::uint32_t>(number + 2),
        static_cast<std::uint32_t>(number + 3),
        0,
        static_cast<std::uint32_t>(number),
        0,
        static_cast<std::uint32_t>(number + 2),
        0,
        static_cast<std::uint32_t>(number + 3)};
    if (!client.Reparse(edited, edit))
      return 6;
    const auto edited_spans =
        client.Highlights(0, static_cast<std::uint32_t>(edited.size()));
    if (!std::ranges::any_of(edited_spans, [&](const auto &span) {
          return span.kind == notepad_colon::SyntaxKind::number &&
                 span.start_byte == number && span.end_byte == number + 3;
        }))
      return 7;
  }
  const std::array<std::uint8_t, 8> invalid{0, 'a', 's', 'm', 1, 0, 0, 0};
  {
    notepad_colon::WasmSyntaxClient warmup;
    if (warmup.Start(argv[1], "invalid", invalid, highlights))
      return 8;
  }
  ::GetProcessHandleCount(::GetCurrentProcess(), &handles_before);
  for (int iteration = 0; iteration < 10; ++iteration) {
    notepad_colon::WasmSyntaxClient rejected;
    if (rejected.Start(argv[1], "invalid", invalid, highlights))
      return 8;
  }
  ::GetProcessHandleCount(::GetCurrentProcess(), &handles_after);
  if (handles_after > handles_before + 4) {
    std::cerr << "Wasm client handle delta: "
              << (handles_after - handles_before) << '\n';
    return 9;
  }
  std::cout << "isolated Wasm grammar parsed, highlighted, symbolized, and "
               "incrementally reparsed\n";
  return 0;
}
