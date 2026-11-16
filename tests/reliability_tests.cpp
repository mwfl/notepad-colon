#include <notepad_colon/encoding_analysis.h>
#include <notepad_colon/large_file_buffer.h>
#include <notepad_colon/recovery_writer.h>
#include <notepad_colon/session.h>
#include <notepad_colon/workspace.h>
#include <notepad_colon/line_tools.h>
#include <notepad_colon/regex_budget.h>
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <thread>

namespace {
using namespace notepad_colon;
int failures = 0;
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAILED: " << message << '\n'; ++failures; }
}
std::vector<std::uint8_t> Bytes(std::string_view value) { return {value.begin(), value.end()}; }
void Write(const std::filesystem::path& path, std::string_view value) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc); stream << value;
    Check(static_cast<bool>(stream), "write fixture");
}
}
int main() {
    using namespace notepad_colon;
    static_assert(std::random_access_iterator<RegexInputIterator<wchar_t>>);
    static_assert(!std::contiguous_iterator<RegexInputIterator<wchar_t>>);
    const std::wstring hostile = std::wstring(64 * 1024, L'a') + L'!';
    const auto cancel_match = [&](auto operation) {
        std::stop_source stop;
        std::jthread cancel([&] { std::this_thread::sleep_for(std::chrono::milliseconds{5}); stop.request_stop(); });
        const auto start = std::chrono::steady_clock::now();
        const bool cancelled = operation(stop.get_token());
        Check(cancelled && std::chrono::steady_clock::now() - start < std::chrono::milliseconds{500},
            "cancellation interrupts a single backtracking regex match within 500 ms");
    };
    cancel_match([&](std::stop_token stop) {
        return SearchText({}, hostile, L"^(a+)+$", {.regular_expression = true}, stop).cancelled;
    });
    cancel_match([&](std::stop_token stop) {
        const auto changed = ReplaceAllInText(hostile, L"^(a+)+$", L"changed", {.regular_expression = true}, stop);
        return changed.cancelled && changed.count == 0 && changed.text.empty() && changed.spans.empty();
    });
    cancel_match([&](std::stop_token stop) {
        const auto filtered = FilterLines(std::string(64 * 1024, 'a') + "!", "^(a+)+$",
            {.regular_expression = true, .invert = true}, stop);
        return filtered.cancelled && filtered.lines.empty();
    });
    {
        const std::wregex expression{L"^(a+)+$"};
        bool timed_out = false;
        try {
            RegexBudget budget{{}, std::chrono::milliseconds{1}};
            static_cast<void>(std::regex_search(RegexInputIterator<wchar_t>{hostile, 0, budget},
                              RegexInputIterator<wchar_t>{hostile, hostile.size(), budget}, expression));
        } catch (const RegexInterrupted& interrupted) { timed_out = !interrupted.cancelled; }
        Check(timed_out, "backtracking checks the elapsed-time limit inside the regex engine");
    }
    const auto captures = ReplaceAllInText(L"ab12 ab34", LR"(ab(\d+)(?=\b))", L"[$1]", {.regular_expression = true});
    Check(captures.text == L"[12] [34]" && captures.count == 2, "checked regex retains captures and lookahead semantics");
    const auto empty_matches = ReplaceAllInText(L"ab", L"(?=.)", L"_", {.regular_expression = true});
    Check(empty_matches.text == L"_a_b" && empty_matches.count == 2, "zero-width regex matching keeps forward progress");
    std::wstring dense;
    for (int row = 0; row < 10000; ++row) dense += L"a\r\n";
    const auto dense_result = SearchText({}, dense, L"a", {.regular_expression = true, .maximum_results = 10000});
    Check(dense_result.matches.size() == 10000 && dense_result.matches.back().line == 10000 &&
        dense_result.matches.back().column == 1 && dense_result.matches.back().preview == L"a",
        "dense search results preserve line positions and bounded previews");
    const auto root = std::filesystem::temp_directory_path() /
        (L"npc-reliability-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto source = root / L"source.txt";
    Write(source, "0123456789abcdefghijklmnopqrstuvwxyz\n");
    LargeFileBuffer buffer;
    Check(buffer.Open(source, false), "open random edit fixture");
    auto expected = Bytes("0123456789abcdefghijklmnopqrstuvwxyz\n");
    std::mt19937 random{75219};
    for (int step = 0; step < 10000; ++step) {
        const auto offset = random() % (expected.size() + 1);
        const auto erase = (std::min<std::size_t>)(random() % 24, expected.size() - offset);
        const std::vector<std::uint8_t> insert(random() % 32, static_cast<std::uint8_t>('a' + random() % 26));
        Check(buffer.Replace(offset, erase, insert), "random edit commits");
        expected.erase(expected.begin() + offset, expected.begin() + offset + erase);
        expected.insert(expected.begin() + offset, insert.begin(), insert.end());
        if (step % 127 == 0) Check(buffer.Read(0, expected.size()) == expected, "piece table agrees with independent reference");
    }
    Check(buffer.Read(0, expected.size()) == expected, "random replacements preserve complete text");
    const auto frozen = buffer.Snapshot();
    const auto anchor = buffer.Size();
    const std::vector<std::uint8_t> transient(64 * 1024, 'x');
    for (int step = 0; step < 2000; ++step) {
        Check(buffer.Insert(anchor, transient) && buffer.Erase(anchor, transient.size()), "insert/erase churn");
        if (step % 100 == 0) {
            const auto memory = buffer.MemoryUsage();
            Check(memory.added_capacity < 4 * 1024 * 1024, "deleted insertion storage stays bounded under 125 MiB of churn");
        }
    }
    Check(frozen && frozen->Read(0, expected.size()) == expected, "compaction does not mutate frozen recovery input");
    Check(!buffer.Replace(buffer.Size() + 1, 0, transient) && buffer.Read(0, expected.size()) == expected,
        "invalid replacement leaves original text intact");
    Check(buffer.SaveAs(root / L"saved.txt") && buffer.MemoryUsage().added_capacity == 0, "save releases inserted arena");
    buffer.Close();
    Check(buffer.MemoryUsage().added_capacity == 0 && buffer.MemoryUsage().piece_capacity == 0, "close releases retained allocations");

    const std::vector<std::uint8_t> invalid(1024 * 1024, 0xff);
    const auto analysis = AnalyzeEncoding(invalid, 1252);
    Check(analysis.invalid_byte_count == invalid.size() && analysis.invalid_byte_offsets.size() == 4096,
        "invalid-byte diagnostics keep an exact count with bounded storage");
    const auto controls = AnalyzeDecodedText(std::wstring(100000, L'\u200b'), EncodingKind::utf8);
    Check(controls.unicode_risk_count == 100000 && controls.unicode_risks.size() == 4096,
        "Unicode diagnostic memory is bounded while counts remain exact");
    const std::vector<std::uint8_t> binary(32 * 1024 * 1024, 0);
    Check(!DecodeFileText(binary), "binary input is rejected without materializing text");

    const auto recovery_directory = root / L"history";
    auto store = std::make_shared<RecoveryStore>(recovery_directory, 2, 64 * 1024);
    Check(store->Save(L"first", L"First", {}, L"original") && store->Save(L"second", L"Second", {}, L"other"),
        "distinct document originals saved");
    for (int index = 0; index < 20; ++index)
        Check(store->Save(L"first", L"First", {}, L"revision " + std::to_wstring(index)), "busy document saved");
    auto snapshots = store->List();
    bool kept_other = false;
    for (const auto& snapshot : snapshots) if (store->Load(snapshot) == L"other") kept_other = true;
    Check(kept_other, "busy document cannot evict another document's only recovery point");
    Check(!store->Save(L"oversize", L"Oversize", {}, std::wstring(128 * 1024, L'x')),
        "history quota rejects new data without removing originals");
    const auto corrupt = snapshots.front();
    { std::fstream stream(corrupt.file, std::ios::binary | std::ios::in | std::ios::out);
      stream.seekp(-1, std::ios::end); stream.put('!'); }
    Check(!store->Load(corrupt), "checksum refuses truncated or changed recovery contents");
    {
        RecoveryStore damaged{root / L"damaged-history", 2};
        Check(damaged.Save(L"quiet", L"Quiet", {}, L"last valid fallback"), "create valid fallback");
        const auto fallback = damaged.List().front();
        for (int index = 0; index < 2; ++index) {
            Check(damaged.Save(L"quiet", L"Quiet", {}, L"newer damaged text"), "create newer recovery");
            const auto newest_snapshot = damaged.List().front();
            std::fstream stream(newest_snapshot.file, std::ios::binary | std::ios::in | std::ios::out);
            stream.seekp(-1, std::ios::end); stream.put('!');
        }
        for (int index = 0; index < 5; ++index)
            Check(damaged.Save(L"busy", L"Busy", {}, L"busy " + std::to_wstring(index)), "prune unrelated document");
        Check(damaged.Load(fallback) == L"last valid fallback", "damaged newer generations never evict the last valid fallback");
    }
    Check(!store->Save(L"invalid", L"Bad", {}, std::wstring(1, static_cast<wchar_t>(0xd800))),
        "invalid Unicode cannot turn into an empty successful recovery");
    Check(!store->Remove({source}), "recovery deletion cannot escape its store");
    RecoveryWriter writer;
    for (int revision = 0; revision < 80; ++revision)
        Check(writer.Queue(store, L"queued", L"Queued", {}, L"queued " + std::to_wstring(revision)), "coalesced recovery accepted");
    Check(writer.Flush() && writer.PendingBytes() == 0, "recovery worker drains and releases pending buffers");
    bool newest = false;
    for (const auto& snapshot : store->List()) if (store->Load(snapshot) == L"queued 79") newest = true;
    Check(newest, "coalescing always retains final submitted contents");
    const auto denied = root / L"not-a-directory";
    Write(denied, "block");
    auto unavailable = std::make_shared<RecoveryStore>(denied);
    Check(writer.Queue(unavailable, L"bad", L"Bad", {}, L"unsaved") && !writer.Flush(), "background write failure surfaces");
    const auto failed_write = writer.TakeResult();
    Check(failed_write && !failed_write->success, "failure remains visible even after previous successes");
    RecoveryWriter bounded_writer{1024};
    Check(!bounded_writer.Queue(store, L"too-big", L"Too big", {}, std::wstring(1024, L'x')) &&
        bounded_writer.PendingBytes() == 0, "recovery queue rejects oversized work before retaining it");
    Check(writer.Queue(store, L"queued", L"Queued", {}, L"retry") && writer.Flush(), "worker recovers after failure");
    Check(store->Save(L"empty", L"Empty", {}, L""), "empty recovery text is valid");
    bool empty = false;
    for (const auto& snapshot : store->List()) if (snapshot.title == L"Empty") empty = store->Load(snapshot) == L"";
    Check(empty, "empty recovery remains distinguishable from failure");

    const auto session_path = root / L"session.state";
    Session session; session.documents.push_back({{}, L"preserved text", {}, {}, {}, true});
    Check(SaveSessionAtomic(session_path, session) && SaveSessionAtomic(session_path, session), "session generations created");
    std::filesystem::create_directory(session_path.wstring() + L".data");
    Write(std::filesystem::path{session_path.wstring() + L".data"} / L"orphan.npcrecovery", "orphan evidence");
    const auto archive = PreserveUnrestoredSession(session_path);
    Session loaded;
    Check(archive && !archive->empty() && LoadSession(*archive / session_path.filename(), loaded) &&
        loaded.documents.front().recovery_text == L"preserved text", "preserved session remains loadable under original relative names");
    Check(archive && std::filesystem::exists(*archive / L"session.state.data" / L"orphan.npcrecovery"),
        "orphan recovery data is preserved with the manifest");
    session.documents.front().recovery_text = L"new working session";
    Check(SaveSessionAtomic(session_path, session) && archive && LoadSession(*archive / session_path.filename(), loaded) &&
        loaded.documents.front().recovery_text == L"preserved text", "new checkpoints cannot prune the preserved session");
    Check(SaveSessionAtomic(session_path, session), "create rollback fixture");
    const auto locked_path = session_path.wstring() + L".previous";
    const auto locked = ::CreateFileW(locked_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(locked != INVALID_HANDLE_VALUE && !PreserveUnrestoredSession(session_path), "archive failure is reported");
    if (locked != INVALID_HANDLE_VALUE) ::CloseHandle(locked);
    Check(LoadSession(session_path, loaded) && loaded.documents.front().recovery_text == L"new working session",
        "failed preservation rolls back already moved generations");

    // The writer keeps one shared-write handle open, appending faster than a
    // full read. Old exact-size reads could wait forever for an idle interval.
    const auto live_path = root / L"live.log";
    { std::ofstream stream(live_path, std::ios::binary); const std::string block(1024 * 1024, 'a');
      for (int index = 0; index < 8; ++index) stream << block; }
    const auto live_handle = ::CreateFileW(live_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    Check(live_handle != INVALID_HANDLE_VALUE, "open continuous log writer");
    std::atomic_uint64_t writes{0};
    std::jthread append([&](std::stop_token stop) {
        const std::string block(4096, 'b');
        while (!stop.stop_requested()) {
            DWORD count = 0;
            if (!::WriteFile(live_handle, block.data(), static_cast<DWORD>(block.size()), &count, nullptr)) return;
            writes.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    });
    std::uintmax_t first_size = 0, last_size = 0;
    std::size_t live_frames = 0;
    for (int index = 0; index < 30; ++index) {
        const auto frame = ReadEncodedFileText(live_path, 64 * 1024 * 1024, 0, EncodingKind::utf8, {}, true);
        if (frame) { if (!first_size) first_size = frame->state.size; last_size = frame->state.size; ++live_frames; }
    }
    Check(live_frames >= 20 && last_size > first_size && writes.load() > 20, "tail frames advance while the writer remains active");
    Check(!WriteEncodedFileAtomic(live_path, L"must not overwrite", EncodingKind::utf8), "live display never relaxes save writer exclusion");
    append.request_stop(); append.join();
    if (live_handle != INVALID_HANDLE_VALUE) ::CloseHandle(live_handle);
    Check(writer.Flush(), "finish recovery work before fixture teardown");
    // This root is the explicit, process-specific test fixture created above.
    std::error_code error; std::filesystem::remove_all(root, error);
    return failures ? 1 : 0;
}
