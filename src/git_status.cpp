#include <notepad_colon/git_status.h>

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <string>

namespace notepad_colon {
namespace {
std::wstring Quote(std::wstring_view value) {
    std::wstring result = L"\"";
    for (const auto ch : value) result += ch == L'"' ? L"\\\"" : std::wstring(1, ch);
    return result + L"\"";
}
}

GitChangedLines QueryGitChangedLines(const std::filesystem::path& file) noexcept {
    GitChangedLines result;
    try {
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        HANDLE read_pipe = nullptr, write_pipe = nullptr;
        if (!::CreatePipe(&read_pipe, &write_pipe, &attributes, 0)) return result;
        ::SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        startup.hStdOutput = write_pipe; startup.hStdError = write_pipe;
        PROCESS_INFORMATION process{};
        auto command = L"git -C " + Quote(file.parent_path().wstring()) +
            L" diff --no-color --unified=0 -- " + Quote(file.filename().wstring());
        const bool started = ::CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
        ::CloseHandle(write_pipe);
        if (!started) { ::CloseHandle(read_pipe); return result; }
        std::string output; char buffer[4096]; DWORD read = 0;
        bool timed_out = false;
        const auto deadline = ::GetTickCount64() + 2000;
        while (::WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
            DWORD available = 0;
            if (::PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr) && available) {
                const auto wanted = (std::min<DWORD>)(available, sizeof(buffer));
                if (::ReadFile(read_pipe, buffer, wanted, &read, nullptr) && read &&
                    output.size() + read <= 1024 * 1024) output.append(buffer, read);
            } else ::WaitForSingleObject(process.hProcess, 10);
            if (::GetTickCount64() >= deadline) { timed_out = true; ::TerminateProcess(process.hProcess, ERROR_TIMEOUT); break; }
        }
        while (output.size() < 1024 * 1024 &&
               ::ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) && read)
            output.append(buffer, read);
        DWORD exit_code = 1; ::GetExitCodeProcess(process.hProcess, &exit_code);
        ::CloseHandle(read_pipe); ::CloseHandle(process.hThread); ::CloseHandle(process.hProcess);
        if (timed_out || exit_code != 0) return result;
        result.repository = true;
        std::size_t cursor = 0;
        while ((cursor = output.find("@@ -", cursor)) != std::string::npos) {
            const auto plus = output.find('+', cursor);
            if (plus == std::string::npos) break;
            std::size_t end = plus + 1;
            while (end < output.size() && output[end] >= '0' && output[end] <= '9') ++end;
            std::uint64_t first = 0;
            const auto parsed = std::from_chars(output.data() + plus + 1, output.data() + end, first);
            if (parsed.ec != std::errc{}) { cursor = end; continue; }
            std::uint64_t count = 1;
            if (end < output.size() && output[end] == ',') {
                const auto count_start = ++end;
                while (end < output.size() && output[end] >= '0' && output[end] <= '9') ++end;
                static_cast<void>(std::from_chars(output.data() + count_start, output.data() + end, count));
            }
            for (std::uint64_t index = 0; index < count && first + index > 0; ++index)
                result.added_or_modified.push_back(static_cast<std::size_t>(first + index));
            cursor = end;
        }
    } catch (...) {}
    return result;
}

}  // namespace notepad_colon
