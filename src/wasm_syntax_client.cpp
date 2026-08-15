#include "wasm_syntax_client.h"

#include <notepad_colon/wasm_syntax_protocol.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace notepad_colon {
namespace {
using namespace wasm_protocol;

bool WriteAll(HANDLE pipe, std::span<const std::uint8_t> bytes) noexcept {
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

bool ReadExact(HANDLE pipe, HANDLE process, std::span<std::uint8_t> bytes,
               std::chrono::milliseconds timeout) noexcept {
  const auto deadline =
      ::GetTickCount64() + static_cast<ULONGLONG>(timeout.count());
  while (!bytes.empty()) {
    DWORD available = 0;
    if (!::PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
      return false;
    if (available == 0) {
      if (::WaitForSingleObject(process, 0) != WAIT_TIMEOUT ||
          ::GetTickCount64() >= deadline)
        return false;
      ::Sleep(2);
      continue;
    }
    DWORD read = 0;
    const auto count =
        static_cast<DWORD>((std::min<std::size_t>)(bytes.size(), available));
    if (!::ReadFile(pipe, bytes.data(), count, &read, nullptr) || read == 0)
      return false;
    bytes = bytes.subspan(read);
  }
  return true;
}
} // namespace

struct WasmSyntaxClient::State {
  HANDLE input = INVALID_HANDLE_VALUE;
  HANDLE output = INVALID_HANDLE_VALUE;
  HANDLE process = nullptr;
  HANDLE thread = nullptr;
  HANDLE job = nullptr;
  bool ready = false;

  ~State() { Stop(); }

  void Stop() noexcept {
    if (ready) {
      const RequestHeader request{magic, version, Command::quit, 0};
      static_cast<void>(
          WriteAll(input, {reinterpret_cast<const std::uint8_t *>(&request),
                           sizeof(request)}));
    }
    ready = false;
    if (input != INVALID_HANDLE_VALUE) {
      ::CloseHandle(input);
      input = INVALID_HANDLE_VALUE;
    }
    if (output != INVALID_HANDLE_VALUE) {
      ::CloseHandle(output);
      output = INVALID_HANDLE_VALUE;
    }
    if (process) {
      static_cast<void>(::WaitForSingleObject(process, 200));
      ::CloseHandle(process);
      process = nullptr;
    }
    if (thread) {
      ::CloseHandle(thread);
      thread = nullptr;
    }
    if (job) {
      ::CloseHandle(job);
      job = nullptr;
    }
  }

  bool Transact(Command command, std::span<const std::uint8_t> payload,
                ResponseHeader &response, std::vector<std::uint8_t> &result,
                std::chrono::milliseconds timeout) noexcept {
    try {
      if (!process || payload.size() > maximum_payload)
        return false;
      const RequestHeader request{magic, version, command,
                                  static_cast<std::uint32_t>(payload.size())};
      if (!WriteAll(input, {reinterpret_cast<const std::uint8_t *>(&request),
                            sizeof(request)}) ||
          !WriteAll(input, payload) ||
          !ReadExact(
              output, process,
              {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
              timeout) ||
          response.magic_value != magic || response.version_value != version ||
          response.payload_size > maximum_payload) {
        if (job)
          ::TerminateJobObject(job, ERROR_TIMEOUT);
        ready = false;
        return false;
      }
      result.resize(response.payload_size);
      if (!result.empty() && !ReadExact(output, process, result, timeout)) {
        if (job)
          ::TerminateJobObject(job, ERROR_TIMEOUT);
        ready = false;
        return false;
      }
      return response.status == 0;
    } catch (...) {
      if (job)
        ::TerminateJobObject(job, ERROR_TIMEOUT);
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
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE child_input = nullptr, child_output = nullptr;
    if (!::CreatePipe(&child_input, &state_->input, &attributes, 0))
      return false;
    if (!::CreatePipe(&state_->output, &child_output, &attributes, 0)) {
      ::CloseHandle(child_input);
      return false;
    }
    ::SetHandleInformation(state_->input, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(state_->output, HANDLE_FLAG_INHERIT, 0);

    state_->job = ::CreateJobObjectW(nullptr, nullptr);
    if (!state_->job) {
      ::CloseHandle(child_input);
      ::CloseHandle(child_output);
      return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
        JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.ProcessMemoryLimit = 256ull * 1024 * 1024;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    if (!::SetInformationJobObject(state_->job,
                                   JobObjectExtendedLimitInformation, &limits,
                                   sizeof(limits))) {
      ::CloseHandle(child_input);
      ::CloseHandle(child_output);
      return false;
    }
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
    cpu.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE |
                       JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
    cpu.CpuRate = 2500;
    if (!::SetInformationJobObject(state_->job,
                                   JobObjectCpuRateControlInformation, &cpu,
                                   sizeof(cpu))) {
      ::CloseHandle(child_input);
      ::CloseHandle(child_output);
      return false;
    }

    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = child_input;
    startup.hStdOutput = child_output;
    startup.hStdError = child_output;
    PROCESS_INFORMATION process{};
    auto command = L"\"" + host.wstring() + L"\"";
    const auto started =
        ::CreateProcessW(host.c_str(), command.data(), nullptr, nullptr, TRUE,
                         CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                         host.parent_path().c_str(), &startup, &process);
    ::CloseHandle(child_input);
    ::CloseHandle(child_output);
    if (!started)
      return false;
    state_->process = process.hProcess;
    state_->thread = process.hThread;
    if (!::AssignProcessToJobObject(state_->job, state_->process))
      return false;
    ::ResumeThread(state_->thread);

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
