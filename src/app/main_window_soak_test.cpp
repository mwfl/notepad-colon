#include "main_window.h"
#include <psapi.h>
#include <sstream>

namespace notepad_colon::app {
namespace {
using SoakClock = std::chrono::steady_clock;
struct Resources {
    std::uint64_t private_bytes = 0;
    DWORD handles = 0, gdi = 0, user = 0;
};
Resources SampleResources() {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    Resources value;
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        value.private_bytes = memory.PrivateUsage;
    static_cast<void>(::GetProcessHandleCount(::GetCurrentProcess(), &value.handles));
    value.gdi = ::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS);
    value.user = ::GetGuiResources(::GetCurrentProcess(), GR_USEROBJECTS);
    return value;
}
double Milliseconds(SoakClock::duration value) {
    return std::chrono::duration<double, std::milli>(value).count();
}
}

void MainWindow::RunSustainedFileSelfTest(EditorDocument*& document, int& result) {
    wchar_t requested[16]{};
    if (!::GetEnvironmentVariableW(L"NOTEPAD_COLON_SOAK_SECONDS", requested, 16)) return;
    const auto seconds = std::wcstoul(requested, nullptr, 10);
    if (seconds < 3 || seconds > 3600 || !document) { result = 200; return; }
    const auto id = document->id;
    const auto path = workspace_.Find(id)->path;
    const auto directory = std::filesystem::path{path.wstring() + L".soak"};
    if (!std::filesystem::create_directory(directory)) { result = 201; return; }
    struct Cleanup {
        std::filesystem::path directory, parent;
        ~Cleanup() {
            // Only the exact test directory created above can be removed.
            const auto absolute = std::filesystem::absolute(directory).lexically_normal();
            if (absolute.parent_path() == std::filesystem::absolute(parent).lexically_normal()) {
                std::error_code error; std::filesystem::remove_all(absolute, error);
            }
        }
    } cleanup{directory, path.parent_path()};
    const auto baseline = directory / L"baseline.cpp";
    const auto rotated = directory / L"rotated.cpp";
    const auto staging = directory / L"next.cpp";
    const auto scratch = directory / L"scratch.cpp";
    if (!::CopyFileW(path.c_str(), baseline.c_str(), TRUE)) { result = 202; return; }
    { std::ofstream stream(scratch); stream << "// needle scratch\nint main() { return 42; }\n"; }
    static_cast<void>(session_writer_.Flush());
    std::atomic<unsigned int> requested_rotation{0}, completed_rotation{0};
    std::atomic<std::uint64_t> writes{0};
    std::atomic_bool writer_failed{false};
    std::jthread log_writer([&](std::stop_token stop) {
        HANDLE handle = INVALID_HANDLE_VALUE;
        const auto open = [&] {
            handle = ::CreateFileW(path.c_str(), FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            return handle != INVALID_HANDLE_VALUE;
        };
        if (!open()) { writer_failed.store(true); return; }
        while (!stop.stop_requested()) {
            const auto rotate = requested_rotation.load();
            if (rotate != completed_rotation.load()) {
                ::CloseHandle(handle); handle = INVALID_HANDLE_VALUE;
                const bool changed = rotate == 1
                    ? ::MoveFileExW(path.c_str(), rotated.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE
                    : (::CopyFileW(baseline.c_str(), staging.c_str(), TRUE) &&
                       ::ReplaceFileW(path.c_str(), staging.c_str(), nullptr, 0, nullptr, nullptr));
                if (!changed || !open()) { writer_failed.store(true); break; }
                completed_rotation.store(rotate);
            }
            const auto line = std::string(1024, 'x') + " // NPC-TICK=" + std::to_string(writes.load() + 1) + "\n";
            DWORD written = 0;
            if (!::WriteFile(handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) || written != line.size()) {
                writer_failed.store(true); break;
            }
            writes.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
    });
    const HWND window = GetHwnd();
    std::vector<double> ping_times;
    std::atomic_uint missed_pings{0};
    std::jthread heartbeat([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            DWORD_PTR answer = 0;
            const auto begin = SoakClock::now();
            const auto sent = ::SendMessageTimeoutW(window, WM_NULL, 0, 0,
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &answer);
            if (stop.stop_requested()) break;
            ping_times.push_back(Milliseconds(SoakClock::now() - begin));
            if (!sent) missed_pings.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    });
    const auto pump = [&] {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { result = 203; return; }
            ::TranslateMessage(&message); ::DispatchMessageW(&message);
        }
    };
    const auto started = SoakClock::now();
    std::vector<std::pair<double, Resources>> samples;
    std::size_t frames = 0, advancing = 0, scratch_cycles = 0, search_jobs = 0;
    std::uint64_t previous_tick = 0;
    double maximum_cycle_ms = 0;
    const auto original_language = ui_language_;
    document->follow_tail = true;
    try {
        while (SoakClock::now() - started < std::chrono::seconds{seconds} && result == 0) {
            pump();
            const auto cycle = SoakClock::now();
            const auto elapsed = std::chrono::duration<double>(cycle - started).count();
            if (elapsed > seconds / 3.0 && completed_rotation.load() == 0) requested_rotation.store(1);
            if (elapsed > seconds * 2.0 / 3.0 && completed_rotation.load() == 1) requested_rotation.store(2);
            CheckExternalChanges(true);
            document = FindDocument(id);
            if (!document || !document->follow_tail || workspace_.Find(id)->dirty || document->syntax_tree) {
                result = 204; break;
            }
            const auto length = document->editor->GetLength();
            std::array<char, 257> tail{};
            Sci_TextRangeFull range{{(std::max<LRESULT>)(0, length - 256), length}, tail.data()};
            document->editor->Send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&range));
            const std::string visible{tail.data()};
            if (const auto token = visible.rfind("NPC-TICK="); token != std::string::npos) {
                const auto tick = std::strtoull(visible.c_str() + token + 9, nullptr, 10);
                if (tick > previous_tick) ++advancing;
                previous_tick = tick;
            }
            ++frames;
            // Exercise real native document/Tree-sitter teardown, history
            // capture, language/menu rebuilds and search worker cancellation.
            if (frames % 3 == 0) {
                if (!OpenPath(scratch)) { result = 205; break; }
                auto* note = ActiveDocument();
                const auto text = "// cycle " + std::to_string(frames) + "\n";
                note->editor->Send(SCI_APPENDTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()));
                last_recovery_snapshot_ = {};
                SaveRecoverySnapshotsIfDue();
                note->editor->Undo();
                if (!CloseActive(true)) { result = 206; break; }
                document = FindDocument(id);
                ++scratch_cycles;
                const auto languages = notepad_colon::SupportedUiLanguages();
                SetUiLanguage(languages[scratch_cycles % languages.size()].language);
                for (auto& worker : search_workers_) worker.thread.request_stop();
                ReapCompletedWorkers(search_workers_);
                const auto generation = search_operations_.Begin();
                auto done = std::make_shared<std::atomic_bool>(false);
                search_workers_.push_back({done, std::jthread([this, generation, directory, done, window](std::stop_token stop) {
                    auto found = notepad_colon::SearchWorkspace(directory, L"needle",
                        {.include_globs = {L"scratch.cpp"}}, stop);
                    if (search_operations_.Publish(generation, std::move(found)))
                        ::PostMessageW(window, kSearchCompleteMessage, static_cast<WPARAM>(generation), 0);
                    done->store(true);
                })});
                if (scratch_cycles % 2 == 0) CancelFolderSearch();
                ++search_jobs;
            }
            maximum_cycle_ms = (std::max)(maximum_cycle_ms, Milliseconds(SoakClock::now() - cycle));
            pump();
            if (elapsed >= (std::min)(5.0, seconds / 6.0)) samples.emplace_back(elapsed, SampleResources());
            if (writer_failed.load()) { result = 207; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    } catch (...) { result = 208; }
    log_writer.request_stop(); log_writer.join();
    heartbeat.request_stop(); heartbeat.join();
    StopWorkers();
    static_cast<void>(session_writer_.Flush());
    const bool recovery_good = recovery_writer_.Flush();
    if (!writer_failed.load() && completed_rotation.load() == 2 &&
        ::ReplaceFileW(path.c_str(), baseline.c_str(), nullptr, 0, nullptr, nullptr)) CheckExternalChanges(true);
    else if (result == 0) result = 209;
    document = FindDocument(id);
    SetUiLanguage(original_language);
    std::ranges::sort(ping_times);
    const auto percentile = [&](double proportion) {
        return ping_times.empty() ? 0.0 : ping_times[(std::min)(ping_times.size() - 1,
            static_cast<std::size_t>(ping_times.size() * proportion))];
    };
    Resources first{}, last{};
    bool first_set = false;
    for (const auto& [elapsed, sample] : samples) {
        if (elapsed < seconds / 3.0) {
            if (!first_set || sample.private_bytes < first.private_bytes) first = sample;
            first_set = true;
        }
        if (elapsed > seconds * 5.0 / 6.0) {
            last.private_bytes = (std::max)(last.private_bytes, sample.private_bytes);
            last.handles = (std::max)(last.handles, sample.handles);
            last.gdi = (std::max)(last.gdi, sample.gdi);
            last.user = (std::max)(last.user, sample.user);
        }
    }
    const auto memory_growth = static_cast<std::int64_t>(last.private_bytes) - static_cast<std::int64_t>(first.private_bytes);
    const auto handles_growth = static_cast<long long>(last.handles) - first.handles;
    const auto gdi_growth = static_cast<long long>(last.gdi) - first.gdi;
    const auto user_growth = static_cast<long long>(last.user) - first.user;
#ifdef _DEBUG
    constexpr double ping_budget = 1000.0;
#else
    constexpr double ping_budget = 250.0;
#endif
    if (result == 0 && (frames < 10 || advancing * 5 < frames * 4 || scratch_cycles < 3 || writes.load() < 100 ||
        completed_rotation.load() != 2)) result = 210;
    if (result == 0 && (ping_times.size() < 10 || missed_pings.load() || percentile(0.99) > ping_budget)) result = 211;
    if (result == 0 && (!first_set || !last.private_bytes || memory_growth > 64ll * 1024 * 1024 ||
        handles_growth > 24 || gdi_growth > 32 || user_growth > 32)) result = 212;
    if (result == 0 && (!recovery_good || recovery_writer_.PendingBytes() != 0)) result = 213;
    std::ostringstream report;
    report << "{\n  \"seconds\": " << seconds << ",\n  \"frames\": " << frames
        << ",\n  \"advancing_frames\": " << advancing << ",\n  \"writer_appends\": " << writes.load()
        << ",\n  \"rotations\": " << completed_rotation.load() << ",\n  \"open_edit_close_cycles\": " << scratch_cycles
        << ",\n  \"search_jobs\": " << search_jobs << ",\n  \"ping_p99_ms\": " << percentile(0.99)
        << ",\n  \"ping_max_ms\": " << percentile(1.0) << ",\n  \"missed_pings\": " << missed_pings.load()
        << ",\n  \"maximum_cycle_ms\": " << maximum_cycle_ms << ",\n  \"private_start_bytes\": " << first.private_bytes
        << ",\n  \"private_end_bytes\": " << last.private_bytes << ",\n  \"private_growth_bytes\": " << memory_growth
        << ",\n  \"handle_growth\": " << handles_growth << ",\n  \"gdi_growth\": " << gdi_growth
        << ",\n  \"user_object_growth\": " << user_growth << ",\n  \"exit_code\": " << result << "\n}\n";
    std::ofstream(path.wstring() + L".soak.json") << report.str();
    if (result != 0) std::ofstream(path.wstring() + L".selftest-error") << report.str();
}
} // namespace notepad_colon::app
