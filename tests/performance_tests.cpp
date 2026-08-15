#include <notepad_colon/workspace.h>
#include <notepad_colon/mapped_file.h>
#include <notepad_colon/session_writer.h>

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
long long Milliseconds(Clock::duration value) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(value).count();
}
}

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
    const auto started = Clock::now();
    const auto scan = notepad_colon::ScanWorkspace(root);
    const auto scanned = Clock::now();
    const auto search = notepad_colon::SearchWorkspace(root, L"searchable-token");
    const auto searched = Clock::now();
    const auto elapsed = searched - started;
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
    DWORD handles_before = 0, handles_after = 0;
    ::GetProcessHandleCount(::GetCurrentProcess(), &handles_before);
    const auto mapped_started = Clock::now();
    if (!mapped.Open(large_path) || mapped.Size() < 1024ull * 1024 * 1024) return 8;
    const auto beginning = mapped.Read(0, sizeof(start_marker) - 1);
    const auto ending = mapped.Read(mapped.Size() - (sizeof(end_marker) - 1), sizeof(end_marker) - 1);
    const std::string end_query = end_marker;
    const auto found_end = mapped.Find(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(end_query.data()), end_query.size()});
    const auto mapped_finished = Clock::now();
    const auto handle_path = std::filesystem::temp_directory_path() /
        (L"notepad-colon-handle-performance-" + std::to_wstring(::GetCurrentProcessId()));
    { std::ofstream handle_file(handle_path, std::ios::binary); handle_file << 'x'; }
    for (int iteration = 0; iteration < 1000; ++iteration) {
        notepad_colon::MappedFile probe;
        if (!probe.Open(handle_path)) return 10;
    }
    std::filesystem::remove(handle_path, ignored);
    mapped.Close(); std::filesystem::remove(large_path, ignored);
    ::GetProcessHandleCount(::GetCurrentProcess(), &handles_after);
    if (std::string(beginning.begin(), beginning.end()) != start_marker ||
        std::string(ending.begin(), ending.end()) != end_marker || !found_end) return 9;
    if (handles_after > handles_before + 4) return 11;

    const auto session_path = std::filesystem::temp_directory_path() /
        (L"notepad-colon-session-performance-" + std::to_wstring(::GetCurrentProcessId()));
    notepad_colon::Session session;
    session.documents.push_back({{}, std::wstring(1024 * 1024, L'x'), {}, {}, {}, true});
    const auto session_started = Clock::now();
    {
        notepad_colon::SessionWriter writer;
        for (int revision = 0; revision < 50; ++revision) writer.Queue(session_path, session);
        if (!writer.Flush()) return 12;
    }
    const auto session_finished = Clock::now();
    std::filesystem::remove(session_path, ignored);

    std::vector<std::wstring> filter_keys;
    filter_keys.reserve(20000);
    for (int index = 0; index < 20000; ++index)
        filter_keys.push_back(L"src/component/file-" + std::to_wstring(index) + L".cpp");
    const auto filter_started = Clock::now();
    std::size_t filter_matches = 0;
    for (const auto& key : filter_keys)
        filter_matches += key.find(L"file-199") != std::wstring::npos ? 1u : 0u;
    const auto filter_finished = Clock::now();
    if (filter_matches == 0 || filter_finished - filter_started > std::chrono::milliseconds{100}) return 13;

    std::cout << "{\n"
              << "  \"workspace_scan_ms\": " << Milliseconds(scanned - started) << ",\n"
              << "  \"workspace_search_ms\": " << Milliseconds(searched - scanned) << ",\n"
              << "  \"mapped_1gib_search_ms\": " << Milliseconds(mapped_finished - mapped_started) << ",\n"
              << "  \"session_coalesce_50x_1mib_ms\": " << Milliseconds(session_finished - session_started) << ",\n"
              << "  \"filter_20000_ms\": " << Milliseconds(filter_finished - filter_started) << ",\n"
              << "  \"handle_delta\": " << static_cast<long long>(handles_after) - handles_before << "\n"
              << "}\n";
    return 0;
}
