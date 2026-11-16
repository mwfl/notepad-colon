#include <notepad_colon/replace_plan.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stop_token>
#include <thread>

namespace {
using namespace notepad_colon;
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED: " << message << '\n'; ++failures; }
}
void Write(const std::filesystem::path& path, std::wstring_view text,
           EncodingKind encoding = EncodingKind::utf8, bool bom = true) {
    Check(WriteEncodedFileAtomic(path, text, encoding, 0, {}, bom), "write fixture");
}
std::wstring Read(const std::filesystem::path& path, EncodingKind kind = EncodingKind::utf8) {
    const auto text = ReadEncodedFileText(path, 1024 * 1024, 0, kind);
    return text ? text->text : L"<unreadable>";
}
}
int main() {
    using namespace notepad_colon;
    const auto root = std::filesystem::temp_directory_path() /
        (L"npc-replace-plan-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(root / L"files");
    const auto workspace = root / L"files";
    const auto history = root / L"history";
    std::ofstream(workspace / L".editorconfig") << "root=true\n[one.txt]\ncharset=utf-16be\n";
    const auto one = workspace / L"one.txt", two = workspace / L"two.txt";
    ReplaceOptions options; options.create_backups = false;
    options.search.include_globs = {L"*.txt"};
    Write(one, L"needle Ω\r\n", EncodingKind::utf16_be, false);
    Write(two, L"needle needle\n", EncodingKind::utf8_bom);
    auto plan = PreviewWorkspaceReplacement({workspace, workspace}, L"needle", L"thread", options, {}, history);
    Check(plan.error.empty() && plan.files.size() == 2, "overlapping roots deduplicated");
    Check(Read(one, EncodingKind::utf16_be) == L"needle Ω\r\n", "preview leaves originals untouched");
    std::size_t one_index = 0, two_index = 0;
    for (std::size_t index = 0; index < plan.files.size(); ++index)
        (plan.files[index].path == one ? one_index : two_index) = index;
    Check(ReadReplacementSnapshot(plan, one_index, true) == L"thread Ω\r\n", "frozen output snapshot");
    plan.files[two_index].selected = false;
    Write(workspace / L"late.txt", L"needle created after preview\n");
    ApplyReplacePlan(plan);
    Check(plan.started && !plan.preflight_failed && plan.error.empty(), "reviewed apply succeeds");
    Check(Read(one, EncodingKind::utf16_be) == L"thread Ω\r\n", "selected file changed");
    Check(Read(two, EncodingKind::utf8_bom) == L"needle needle\n", "unselected file unchanged");
    Check(Read(workspace / L"late.txt") == L"needle created after preview\n", "new files excluded from frozen plan");
    auto loaded = LoadReplacePlan(plan.directory);
    Check(loaded && loaded->started && loaded->files[one_index].written_state.has_value(), "durable written identity");
    if (loaded) {
        RestoreReplacePlan(*loaded);
        Check(Read(one, EncodingKind::utf16_be) == L"needle Ω\r\n", "restore exact original text and encoding");
        std::ifstream stream(one, std::ios::binary); unsigned char first = 0; stream.read(reinterpret_cast<char*>(&first), 1);
        Check(first == 0, "restore preserves BOM-less UTF-16");
        Check(loaded->files[one_index].progress == ReplaceProgress::restored, "restore journal persisted");
    }
    // A stale selected input must abort all writes, even when other files are unchanged.
    auto stale = PreviewWorkspaceReplacement({workspace}, L"needle", L"thread", options, {}, history);
    Write(two, L"newer user edits\n", EncodingKind::utf8_bom);
    ApplyReplacePlan(stale);
    Check(stale.preflight_failed && !stale.started, "stale preview aborts entire application");
    Check(Read(one, EncodingKind::utf16_be) == L"needle Ω\r\n", "preflight makes zero writes");
    // Content comparison catches an in-place modification with size and timestamp restored.
    Write(two, L"needle\n");
    auto spoof = PreviewWorkspaceReplacement({workspace}, L"needle", L"thread", options, {}, history);
    const auto timestamp = std::filesystem::last_write_time(two);
    { std::ofstream stream(two, std::ios::binary | std::ios::trunc); stream << "edited\n"; }
    std::filesystem::last_write_time(two, timestamp);
    ApplyReplacePlan(spoof);
    Check(spoof.preflight_failed && Read(two) == L"edited\n", "same metadata with changed bytes is rejected");
    // Recovery refuses subsequent edits and even an atomic replacement with identical output.
    Write(two, L"needle\n");
    auto conflict = PreviewWorkspaceReplacement({workspace}, L"needle", L"thread", options, {}, history);
    ApplyReplacePlan(conflict);
    Write(two, L"thread\n");
    RestoreReplacePlan(conflict);
    Check(Read(two) == L"thread\n", "different identity with same output is not overwritten during restore");
    bool conflict_recorded = false;
    for (const auto& entry : conflict.files) if (entry.path == two) conflict_recorded = !entry.error.empty();
    Check(conflict_recorded, "restore conflict is visible");
    // Simulate a crash after a disk write but before its success event.
    Write(two, L"needle\n");
    auto interrupted = PreviewWorkspaceReplacement({workspace}, L"needle", L"thread", options, {}, history);
    for (auto& entry : interrupted.files) entry.selected = entry.path == two;
    ApplyReplacePlan(interrupted);
    for (auto& entry : interrupted.files) if (entry.path == two) {
        entry.progress = ReplaceProgress::writing; entry.written_state.reset();
    }
    Check(SaveReplacePlan(interrupted), "persist simulated interruption");
    auto recovered = LoadReplacePlan(interrupted.directory);
    if (recovered) RestoreReplacePlan(*recovered);
    Check(recovered && Read(two) == L"needle\n", "interrupted write can be recovered from exact staged output");
    // The previous generation records "writing" before the actual rewrite;
    // it remains usable if the newest completion manifest is damaged.
    Write(two, L"needle\n");
    auto fallback = PreviewWorkspaceReplacement({workspace}, L"needle", L"thread", options, {}, history);
    for (auto& entry : fallback.files) entry.selected = entry.path == two;
    ApplyReplacePlan(fallback);
    Write(fallback.directory / L"plan.json", L"broken manifest");
    auto previous = LoadReplacePlan(fallback.directory);
    Check(previous.has_value(), "corrupt latest journal falls back to previous generation");
    if (previous) RestoreReplacePlan(*previous);
    Check(previous && Read(two) == L"needle\n", "previous journal can safely recover a committed write");
    // A missing/corrupt snapshot blocks writes to every file.
    auto corrupt = PreviewWorkspaceReplacement({workspace}, L"needle", L"thread", options, {}, history);
    Write(corrupt.directory / L"0.after", L"corrupt snapshot");
    ApplyReplacePlan(corrupt);
    Check(corrupt.preflight_failed && Read(two) == L"needle\n", "snapshot checksum blocks corrupted plan");
    // Cancellation before application makes no writes and preserves review artifacts.
    auto cancelled = PreviewWorkspaceReplacement({workspace}, L"needle", L"thread", options, {}, history);
    std::stop_source stop; stop.request_stop();
    ApplyReplacePlan(cancelled, stop.get_token());
    Check(cancelled.cancelled && !cancelled.started && Read(two) == L"needle\n", "cancel before first write");
    // Open buffers are staged independently and are never written by the disk worker.
    options.excluded_paths = {two};
    const std::vector<ReplaceBufferSnapshot> buffers{{two, 19, 42, L"unsaved needle 😀\n"}};
    auto open = PreviewWorkspaceReplacement({workspace}, L"needle", L"thread", options, buffers, history);
    bool found = false;
    for (std::size_t index = 0; index < open.files.size(); ++index) if (open.files[index].document) {
        found = true;
        Check(open.files[index].revision == 42 && ReadReplacementSnapshot(open, index, true) == L"unsaved thread 😀\n",
            "open buffer preview keeps exact unsaved input and revision");
    }
    ApplyReplacePlan(open);
    Check(found && Read(two) == L"needle\n", "open buffer original on disk stays untouched");
    // Zero-width matches are valid ECMAScript regex matches, including the end of an empty string.
    SearchOptions regex; regex.regular_expression = true;
    const auto anchors = ReplaceAllInText(L"abc", L"^|$", L"!", regex);
    const auto empty = ReplaceAllInText(L"", L"^$", L"empty", regex);
    Check(anchors.text == L"!abc!" && anchors.count == 2 && anchors.spans.size() == 2, "zero-width regex advances safely");
    Check(empty.text == L"empty" && empty.count == 1, "empty input regex replacement");
    std::wstring expansion;
    for (int index = 0; index < 40; ++index) expansion += L"$'";
    const auto limited = ReplaceAllInText(std::wstring(1024 * 1024, L'a'), L"^.", expansion, regex);
    Check(!limited.error.empty() && limited.count == 0 && limited.text.empty(),
        "regex expansion is bounded while formatting, before allocating unbounded output");
    const auto partial_root = root / L"partial";
    std::filesystem::create_directory(partial_root);
    for (int index = 0; index < 40; ++index)
        Write(partial_root / (std::to_wstring(index) + L".txt"), L"needle original\n");
    auto partial = PreviewWorkspaceReplacement({partial_root}, L"needle", L"thread", options, {}, history);
    const auto observed = partial.files.front().path;
    const auto original_state = CaptureFileState(observed);
    std::stop_source mid_stop;
    std::jthread observer([&](std::stop_token observer_stop) {
        while (!observer_stop.stop_requested()) {
            if (const auto current = ReadEncodedFileState(observed); current &&
                    !MatchesEncodedFileState(original_state, *current)) { mid_stop.request_stop(); return; }
            std::this_thread::yield();
        }
    });
    ApplyReplacePlan(partial, mid_stop.get_token());
    observer.request_stop(); observer.join();
    std::size_t applied = 0;
    for (const auto& entry : partial.files) if (entry.progress == ReplaceProgress::applied) ++applied;
    Check(partial.cancelled && applied > 0 && applied < partial.files.size(), "mid-operation cancel preserves completed files");
    auto partial_history = LoadReplacePlan(partial.directory);
    Check(partial_history && partial_history->cancelled, "partial cancellation is durable");
    if (partial_history) RestoreReplacePlan(*partial_history);
    bool restored_all = true;
    for (const auto& entry : partial.files)
        restored_all = restored_all && Read(entry.path) == L"needle original\n";
    Check(restored_all, "partially applied operation can restore completed files");
    const auto ignored_root = root / L"ignored";
    std::filesystem::create_directory(ignored_root);
    const auto ignored = ignored_root / L"ignored.txt";
    Write(ignored, L"needle\n");
    std::ofstream(ignored_root / L".gitignore") << "ignored.txt\n";
    options.excluded_paths = {ignored};
    const auto ignored_plan = PreviewWorkspaceReplacement({ignored_root}, L"needle", L"thread", options,
        {{ignored, 50, 1, L"unsaved needle\n"}}, history);
    Check(ignored_plan.files.empty(), "gitignore also applies to unsaved open documents");
    const auto protected_root = root / L"protected";
    std::filesystem::create_directory(protected_root);
    const auto protected_file = protected_root / L"file.txt";
    Write(protected_file, L"needle\n");
    options.create_backups = true;
    options.excluded_paths = {protected_file.wstring() + L".bak"};
    const auto protected_plan = PreviewWorkspaceReplacement({protected_root}, L"needle", L"thread", options, {}, history);
    Check(protected_plan.files.size() == 1 && !protected_plan.files.front().selected &&
        !protected_plan.files.front().error.empty(), "adjacent backups cannot overwrite an open buffer's file");
    options.create_backups = false; options.excluded_paths.clear();
    const auto limit_root = root / L"limit";
    std::filesystem::create_directory(limit_root);
    for (int index = 0; index < 260; ++index)
        Write(limit_root / (std::to_wstring(index) + L".txt"), L"needle\n");
    const auto bounded = PreviewWorkspaceReplacement({limit_root}, L"needle", L"thread", options, {}, history);
    Check(bounded.error.empty() && bounded.truncated && bounded.files.size() == 256,
        "oversized batch has a visible finite preview, without silently expanding execution scope");
    Check(!DeleteReplacePlan(history, workspace), "history delete cannot escape its own root");
    Check(DeleteReplacePlan(history, plan.directory), "explicit history deletion works");
    Check(!LoadReplacePlan(plan.directory), "deleted record unavailable");
    std::error_code error;
    // root is an explicit, process-specific test fixture below the temp directory.
    std::filesystem::remove_all(root, error);
    if (failures) std::cerr << failures << " replacement-plan check(s) failed\n";
    return failures ? 1 : 0;
}
