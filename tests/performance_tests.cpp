#include <notepad_colon/workspace.h>

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        (L"notepad-colon-performance-" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    for (int index = 0; index < 250; ++index) {
        std::ofstream file(root / ("file-" + std::to_string(index) + ".txt"));
        for (int line = 0; line < 80; ++line)
            file << "ordinary editor workload line " << line
                 << (line == 40 ? " searchable-token" : "") << '\n';
    }
    const auto started = std::chrono::steady_clock::now();
    const auto scan = notepad_colon::ScanWorkspace(root);
    const auto search = notepad_colon::SearchWorkspace(root, L"searchable-token");
    const auto elapsed = std::chrono::steady_clock::now() - started;
    std::filesystem::remove_all(root, ignored);
    if (scan.truncated || scan.entries.size() != 250) return 1;
    if (search.cancelled || search.truncated || search.matches.size() != 250) return 2;
    if (elapsed > std::chrono::seconds{10}) {
        std::cerr << "workspace performance guard exceeded 10 seconds\n";
        return 3;
    }
    return 0;
}
