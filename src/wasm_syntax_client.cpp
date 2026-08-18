#include "wasm_syntax_client.h"

#include <notepad_colon/wasm_syntax_protocol.h>
#include <mwfl/process.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace notepad_colon {
namespace {
using namespace wasm_protocol;

bool WriteAll(mwfl::SupervisedProcess &process,
              std::span<const std::uint8_t> bytes,
              mwfl::Deadline deadline) noexcept {
  while (!bytes.empty()) {
    const auto input = std::as_bytes(bytes.first(
        (std::min<std::size_t>)(bytes.size(), 1024u * 1024)));
    auto written = process.WriteInput(input, deadline);
    if (!written || written.Value().status != mwfl::CompletionStatus::Completed ||
        !written.Value().value || *written.Value().value == 0)
      return false;
    bytes = bytes.subspan(*written.Value().value);
  }
  return true;
}

bool ReadExact(mwfl::SupervisedProcess &process,
               std::span<std::uint8_t> bytes,
               mwfl::Deadline deadline) noexcept {
  while (!bytes.empty()) {
    auto read = process.ReadStdout(std::as_writable_bytes(bytes), deadline);
    if (!read || read.Value().status != mwfl::CompletionStatus::Completed ||
        !read.Value().value || *read.Value().value == 0)
      return false;
    bytes = bytes.subspan(*read.Value().value);
  }
  return true;
}
} // namespace

struct WasmSyntaxClient::State {
  std::unique_ptr<mwfl::SupervisedProcess> process;
  bool ready = false;

  ~State() { Stop(); }

  void Stop() noexcept {
    if (ready) {
      const RequestHeader request{magic, version, Command::quit, 0};
      static_cast<void>(WriteAll(
          *process, {reinterpret_cast<const std::uint8_t *>(&request), sizeof(request)},
          mwfl::Deadline::After(std::chrono::milliseconds(200))));
    }
    ready = false;
    if (process) {
      process->CloseInput();
      auto stopped = process->Wait(mwfl::Deadline::After(std::chrono::milliseconds(200)));
      if (!stopped || stopped.Value().status != mwfl::CompletionStatus::Completed)
        static_cast<void>(process->TerminateTree(ERROR_TIMEOUT));
      process.reset();
    }
  }

  bool Transact(Command command, std::span<const std::uint8_t> payload,
                ResponseHeader &response, std::vector<std::uint8_t> &result,
                std::chrono::milliseconds timeout) noexcept {
    try {
      if (!process || payload.size() > maximum_payload)
        return false;
      const auto deadline = mwfl::Deadline::After(timeout);
      const RequestHeader request{magic, version, command,
                                  static_cast<std::uint32_t>(payload.size())};
      if (!WriteAll(*process, {reinterpret_cast<const std::uint8_t *>(&request),
                               sizeof(request)}, deadline) ||
          !WriteAll(*process, payload, deadline) ||
          !ReadExact(*process,
                     {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
                     deadline) ||
          response.magic_value != magic || response.version_value != version ||
          response.payload_size > maximum_payload) {
        static_cast<void>(process->TerminateTree(ERROR_TIMEOUT));
        ready = false;
        return false;
      }
      result.resize(response.payload_size);
      if (!result.empty() && !ReadExact(*process, result, deadline)) {
        static_cast<void>(process->TerminateTree(ERROR_TIMEOUT));
        ready = false;
        return false;
      }
      return response.status == 0;
    } catch (...) {
      if (process)
        static_cast<void>(process->TerminateTree(ERROR_TIMEOUT));
      ready = false;
      return false;
    }
  }
};

WasmSyntaxClient::WasmSyntaxClient() : state_(std::make_unique<State>()) {}
WasmSyntaxClient::~WasmSyntaxClient() = default;

bool WasmSyntaxClient::Start(const std::filesystem::path &host,
                             std::string_view language_name,
                             std::span<const std::uint8_t> wasm,
                             std::string_view highlights_query,
                             std::string_view symbols_query) noexcept {
  try {
    state_ = std::make_unique<State>();
    if (language_name.empty() || language_name.size() > 128 || wasm.empty() ||
        wasm.size() > 16u * 1024 * 1024 ||
        highlights_query.size() > 1024 * 1024 ||
        symbols_query.size() > 1024 * 1024)
      return false;
    mwfl::ProcessJobOptions limits;
    limits.active_process_limit = 1;
    limits.process_memory_limit = 256ull * 1024 * 1024;
    limits.cpu_rate_hard_cap = 2500;
    auto launched = mwfl::ProcessBuilder{}
                        .Executable(host)
                        .WorkingDirectory(host.parent_path())
                        .RedirectStdin()
                        .MergeStderrIntoStdout()
                        .NoWindow()
                        .LaunchSupervised(limits);
    if (!launched) return false;
    state_->process = std::make_unique<mwfl::SupervisedProcess>(
        std::move(launched.Value()));

    ConfigurePayload prefix{static_cast<std::uint32_t>(language_name.size()),
                            static_cast<std::uint32_t>(wasm.size()),
                            static_cast<std::uint32_t>(highlights_query.size()),
                            static_cast<std::uint32_t>(symbols_query.size())};
    std::vector<std::uint8_t> payload(sizeof(prefix) + language_name.size() +
                                      wasm.size() + highlights_query.size() +
                                      symbols_query.size());
    auto *cursor = payload.data();
    std::memcpy(cursor, &prefix, sizeof(prefix));
    cursor += sizeof(prefix);
    const auto append = [&cursor](const auto bytes) {
      std::memcpy(cursor, bytes.data(), bytes.size());
      cursor += bytes.size();
    };
    append(
        std::span{reinterpret_cast<const std::uint8_t *>(language_name.data()),
                  language_name.size()});
    append(wasm);
    append(std::span{
        reinterpret_cast<const std::uint8_t *>(highlights_query.data()),
        highlights_query.size()});
    append(
        std::span{reinterpret_cast<const std::uint8_t *>(symbols_query.data()),
                  symbols_query.size()});
    ResponseHeader response{};
    std::vector<std::uint8_t> result;
    state_->ready = state_->Transact(Command::configure, payload, response,
                                     result, std::chrono::seconds{5});
    return state_->ready;
  } catch (...) {
    state_.reset();
    return false;
  }
}

bool WasmSyntaxClient::Parse(std::string_view utf8) noexcept {
  return Reparse(utf8, {});
}

bool WasmSyntaxClient::Reparse(std::string_view utf8,
                               const SyntaxEdit &edit) noexcept {
  try {
    if (!state_ || !state_->ready || utf8.size() > 8u * 1024 * 1024)
      return false;
    ParsePayload prefix{
        edit, static_cast<std::uint32_t>(utf8.size()),
        static_cast<std::uint8_t>(edit.old_end_byte || edit.new_end_byte)};
    std::vector<std::uint8_t> payload(sizeof(prefix) + utf8.size());
    std::memcpy(payload.data(), &prefix, sizeof(prefix));
    std::memcpy(payload.data() + sizeof(prefix), utf8.data(), utf8.size());
    ResponseHeader response{};
    std::vector<std::uint8_t> result;
    return state_->Transact(Command::parse, payload, response, result,
                            std::chrono::milliseconds{1500});
  } catch (...) {
    return false;
  }
}

std::vector<SyntaxSpan>
WasmSyntaxClient::Highlights(std::uint32_t start, std::uint32_t end) noexcept {
  std::vector<SyntaxSpan> spans;
  try {
    if (!state_ || !state_->ready || start >= end)
      return spans;
    const RangePayload range{start, end};
    ResponseHeader response{};
    std::vector<std::uint8_t> result;
    if (!state_->Transact(
            Command::highlights,
            {reinterpret_cast<const std::uint8_t *>(&range), sizeof(range)},
            response, result, std::chrono::milliseconds{750}) ||
        result.size() != response.count * sizeof(WireSpan))
      return spans;
    const auto *wire = reinterpret_cast<const WireSpan *>(result.data());
    spans.reserve(response.count);
    for (std::uint32_t index = 0; index < response.count; ++index)
      if (wire[index].kind <=
          static_cast<std::uint32_t>(SyntaxKind::preprocessor))
        spans.push_back({wire[index].start_byte, wire[index].end_byte,
                         static_cast<SyntaxKind>(wire[index].kind)});
    return spans;
  } catch (...) {
    return {};
  }
}

std::vector<DocumentSymbol> WasmSyntaxClient::Symbols() noexcept {
  std::vector<DocumentSymbol> symbols;
  try {
    if (!state_ || !state_->ready)
      return symbols;
    ResponseHeader response{};
    std::vector<std::uint8_t> result;
    if (!state_->Transact(Command::symbols, {}, response, result,
                          std::chrono::milliseconds{750}))
      return symbols;
    std::size_t cursor = 0;
    for (std::uint32_t index = 0; index < response.count; ++index) {
      if (result.size() - cursor < sizeof(WireSymbol))
        return {};
      WireSymbol wire{};
      std::memcpy(&wire, result.data() + cursor, sizeof(wire));
      cursor += sizeof(wire);
      if (wire.name_size > result.size() - cursor)
        return {};
      std::string name{reinterpret_cast<const char *>(result.data() + cursor),
                       wire.name_size};
      cursor += wire.name_size;
      if (wire.kind_size > result.size() - cursor)
        return {};
      std::string kind{reinterpret_cast<const char *>(result.data() + cursor),
                       wire.kind_size};
      cursor += wire.kind_size;
      symbols.push_back(
          {std::move(name), std::move(kind), wire.start_byte, wire.end_byte});
    }
    return symbols;
  } catch (...) {
    return {};
  }
}

bool WasmSyntaxClient::IsReady() const noexcept {
  return state_ && state_->ready;
}

} // namespace notepad_colon
