#include <notepad_colon/workspace.h>
#include <notepad_colon/mapped_file.h>

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
    const auto large_path = std::filesystem::temp_directory_path() /
        (L"notepad-colon-mapped-" + std::to_wstring(::GetCurrentProcessId()) + L".txt");
    const auto large = ::CreateFileW(large_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (large == INVALID_HANDLE_VALUE) return 4;
    DWORD written = 0; const char start_marker[] = "mapped-start";
    if (!::WriteFile(large, start_marker, sizeof(start_marker) - 1, &written, nullptr)) return 5;
    LARGE_INTEGER end{}; end.QuadPart = 1024ll * 1024 * 1024 + 7;
    if (!::SetFilePointerEx(large, end, nullptr, FILE_BEGIN) || !::SetEndOfFile(large)) return 6;
    const char end_marker[] = "mapped-end";
    end.QuadPart -= static_cast<LONGLONG>(sizeof(end_marker) - 1);
    if (!::SetFilePointerEx(large, end, nullptr, FILE_BEGIN) ||
        !::WriteFile(large, end_marker, sizeof(end_marker) - 1, &written, nullptr)) return 7;
    ::CloseHandle(large);
    notepad_colon::MappedFile mapped;
    if (!mapped.Open(large_path) || mapped.Size() < 1024ull * 1024 * 1024) return 8;
    const auto beginning = mapped.Read(0, sizeof(start_marker) - 1);
    const auto ending = mapped.Read(mapped.Size() - (sizeof(end_marker) - 1), sizeof(end_marker) - 1);
    const std::string end_query = end_marker;
    const auto found_end = mapped.Find(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(end_query.data()), end_query.size()});
    mapped.Close(); std::filesystem::remove(large_path, ignored);
    if (std::string(beginning.begin(), beginning.end()) != start_marker ||
        std::string(ending.begin(), ending.end()) != end_marker || !found_end) return 9;
    return 0;
}
