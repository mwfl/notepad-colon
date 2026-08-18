#include <notepad_colon/git_status.h>

#include <mwfl/process.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <string>

namespace notepad_colon {
GitChangedLines QueryGitChangedLines(const std::filesystem::path& file) noexcept {
    GitChangedLines result;
    try {
        auto launched = mwfl::ProcessBuilder{}
                            .Executable(L"git.exe")
                            .Argument(L"-C")
                            .Argument(file.parent_path().wstring())
                            .Argument(L"diff")
                            .Argument(L"--no-color")
                            .Argument(L"--unified=0")
                            .Argument(L"--")
                            .Argument(file.filename().wstring())
                            .NoWindow()
                            .MergeStderrIntoStdout()
                            .LaunchSupervised();
        if (!launched) return result;
        auto collected = launched.Value().RunUntilExit(
            1024 * 1024, 0, mwfl::Deadline::After(std::chrono::seconds(2)));
        if (!collected || collected.Value().status != mwfl::CompletionStatus::Completed) {
            static_cast<void>(launched.Value().TerminateTree(ERROR_TIMEOUT));
            return result;
        }
        const auto& process_output = *collected.Value().value;
        if (process_output.exit_code != 0 || process_output.stdout_truncated) return result;
        const std::string output(reinterpret_cast<const char*>(process_output.stdout_bytes.data()),
                                 process_output.stdout_bytes.size());
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
