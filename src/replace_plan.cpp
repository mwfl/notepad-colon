#include <notepad_colon/replace_plan.h>
#include <notepad_colon/editor_config.h>
#include <notepad_colon/localization.h>
#include <windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <set>

namespace notepad_colon {
namespace {
using Json = nlohmann::json;
constexpr std::size_t kFiles = 256;
constexpr std::size_t kSnapshotLimit = 64 * 1024 * 1024;
constexpr std::uintmax_t kPlanLimit = 128 * 1024 * 1024;
constexpr std::uintmax_t kHistoryLimit = 512 * 1024 * 1024;
std::wstring Key(const std::filesystem::path& path) {
    auto value = std::filesystem::absolute(path).lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(), std::towlower);
    return value;
}
std::filesystem::path SnapshotPath(const ReplacePlan& plan, std::size_t index, bool after) {
    return plan.directory / (std::to_wstring(index) + (after ? L".after" : L".before"));
}
std::uint64_t Hash(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t value = 14695981039346656037ull;
    for (const auto byte : bytes) { value ^= byte; value *= 1099511628211ull; }
    return value;
}
std::string Utf8(std::wstring_view value) {
    const auto encoded = EncodeText(value, EncodingKind::utf8);
    if (!encoded) throw std::runtime_error("Invalid Unicode");
    return {encoded->begin(), encoded->end()};
}
std::wstring Wide(const std::string& value) {
    const std::vector<std::uint8_t> bytes(value.begin(), value.end());
    const auto decoded = DecodeFileText(bytes, 0, EncodingKind::utf8);
    if (!decoded) throw std::runtime_error("Invalid Unicode");
    return decoded->text;
}
Json State(const FileState& state) {
    return {state.size, state.last_write.time_since_epoch().count(), state.exists,
            state.file_index, state.volume_serial};
}
FileState State(const Json& value) {
    FileState state;
    state.size = value.at(0).get<std::uintmax_t>();
    state.last_write = std::filesystem::file_time_type{
        std::filesystem::file_time_type::duration{value.at(1).get<std::int64_t>()}};
    state.exists = value.at(2).get<bool>();
    state.file_index = value.at(3).get<std::uint64_t>();
    state.volume_serial = value.at(4).get<std::uint32_t>();
    return state;
}
bool MatchesText(std::wstring_view text, const ReviewedReplacement& entry, bool after) {
    const auto bytes = EncodeText(text, EncodingKind::utf8);
    return bytes && bytes->size() == (after ? entry.after_size : entry.before_size) &&
        Hash(*bytes) == (after ? entry.after_hash : entry.before_hash);
}
std::optional<EncodedFileText> ReadTarget(const ReviewedReplacement& entry) {
    return ReadEncodedFileText(entry.path, kSnapshotLimit, entry.code_page, entry.encoding);
}
std::uintmax_t HistorySize(const std::filesystem::path& history) {
    std::error_code error;
    std::uintmax_t size = 0;
    for (std::filesystem::recursive_directory_iterator it(history,
            std::filesystem::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        if (it->is_symlink(error)) { it.disable_recursion_pending(); continue; }
        if (it->is_regular_file(error)) {
            const auto amount = it->file_size(error);
            if (error || amount > kHistoryLimit - (std::min)(size, kHistoryLimit)) return kHistoryLimit;
            size += amount;
        }
    }
    return error ? kHistoryLimit : size;
}
void Fail(ReplacePlan& plan, ReviewedReplacement& entry, std::wstring_view message) {
    entry.progress = ReplaceProgress::conflict;
    entry.error = message;
    plan.preflight_failed = true;
}
bool Persist(ReplacePlan& plan) {
    if (SaveReplacePlan(plan)) return true;
    plan.error = Tr(L"The replacement journal could not be saved; no further files were changed");
    return false;
}
}

static std::optional<ReplacePlan> LoadReplaceManifest(const std::filesystem::path& directory,
                                                     const wchar_t* filename) noexcept;

bool SaveReplacePlan(const ReplacePlan& plan) noexcept {
    try {
        Json files = Json::array();
        for (const auto& entry : plan.files) {
            Json spans = Json::array();
            for (const auto& span : entry.spans)
                spans.push_back({span.before_start, span.before_length, span.after_start, span.after_length});
            files.push_back({{"path", Utf8(entry.path.wstring())}, {"document", entry.document},
                {"revision", entry.revision}, {"encoding", static_cast<int>(entry.encoding)},
                {"page", entry.code_page}, {"bom", entry.bom}, {"original", State(entry.original_state)},
                {"written", entry.written_state ? State(*entry.written_state) : Json{}},
                {"before_hash", entry.before_hash}, {"after_hash", entry.after_hash},
                {"before_size", entry.before_size}, {"after_size", entry.after_size},
                {"count", entry.replacements}, {"spans", spans}, {"selected", entry.selected},
                {"progress", static_cast<int>(entry.progress)}, {"error", Utf8(entry.error)}});
        }
        const Json json{{"version", 1}, {"query", Utf8(plan.query)}, {"replacement", Utf8(plan.replacement)},
            {"backups", plan.backups}, {"started", plan.started}, {"cancelled", plan.cancelled},
            {"truncated", plan.truncated}, {"files", files}, {"searched", plan.files_searched},
            {"skipped", plan.files_skipped}};
        const auto body = Wide(json.dump());
        if (body.size() > 2 * 1024 * 1024) return false;
        const auto path = plan.directory / L"plan.json";
        if (const auto previous = ReadEncodedFileText(path, 4 * 1024 * 1024, 0, EncodingKind::utf8);
            previous && LoadReplaceManifest(plan.directory, L"plan.json")) {
            // Never discard the last usable generation when replacing a journal.
            if (!WriteEncodedFileAtomic(plan.directory / L"plan.previous.json", previous->text, EncodingKind::utf8)) return false;
        }
        return WriteEncodedFileAtomic(path, body, EncodingKind::utf8);
    } catch (...) { return false; }
}

static std::optional<ReplacePlan> LoadReplaceManifest(const std::filesystem::path& directory,
                                                     const wchar_t* filename) noexcept {
    try {
        const auto text = ReadEncodedFileText(directory / filename, 4 * 1024 * 1024, 0, EncodingKind::utf8);
        if (!text) return {};
        const auto json = Json::parse(Utf8(text->text));
        if (json.at("version") != 1 || json.at("files").size() > kFiles) return {};
        ReplacePlan plan;
        plan.directory = directory;
        plan.query = Wide(json.at("query").get<std::string>());
        plan.replacement = Wide(json.at("replacement").get<std::string>());
        plan.backups = json.at("backups").get<bool>();
        plan.started = json.at("started").get<bool>();
        plan.cancelled = json.at("cancelled").get<bool>();
        plan.truncated = json.at("truncated").get<bool>();
        plan.files_searched = json.at("searched").get<std::size_t>();
        plan.files_skipped = json.at("skipped").get<std::size_t>();
        for (const auto& item : json.at("files")) {
            ReviewedReplacement entry;
            entry.path = Wide(item.at("path").get<std::string>());
            if (!entry.path.is_absolute()) return {};
            entry.document = item.at("document").get<std::uint64_t>();
            entry.revision = item.at("revision").get<std::uint64_t>();
            const auto kind = item.at("encoding").get<int>();
            const auto progress = item.at("progress").get<int>();
            if (kind < 0 || kind >= static_cast<int>(EncodingKind::binary) || progress < 0 ||
                progress > static_cast<int>(ReplaceProgress::failed)) return {};
            entry.encoding = static_cast<EncodingKind>(kind);
            entry.progress = static_cast<ReplaceProgress>(progress);
            entry.code_page = item.at("page").get<unsigned int>();
            entry.bom = item.at("bom").get<bool>();
            entry.original_state = State(item.at("original"));
            if (!item.at("written").is_null()) entry.written_state = State(item.at("written"));
            entry.before_hash = item.at("before_hash").get<std::uint64_t>();
            entry.after_hash = item.at("after_hash").get<std::uint64_t>();
            entry.before_size = item.at("before_size").get<std::size_t>();
            entry.after_size = item.at("after_size").get<std::size_t>();
            if (entry.before_size > kSnapshotLimit || entry.after_size > kSnapshotLimit) return {};
            entry.replacements = item.at("count").get<std::size_t>();
            entry.selected = item.at("selected").get<bool>();
            entry.error = Wide(item.at("error").get<std::string>());
            if (item.at("spans").size() > 256) return {};
            for (const auto& span : item.at("spans"))
                entry.spans.push_back({span.at(0).get<std::size_t>(), span.at(1).get<std::size_t>(),
                    span.at(2).get<std::size_t>(), span.at(3).get<std::size_t>()});
            plan.files.push_back(std::move(entry));
        }
        return plan;
    } catch (...) { return {}; }
}

std::optional<ReplacePlan> LoadReplacePlan(const std::filesystem::path& directory) noexcept {
    auto plan = LoadReplaceManifest(directory, L"plan.json");
    return plan ? std::move(plan) : LoadReplaceManifest(directory, L"plan.previous.json");
}

std::optional<std::wstring> ReadReplacementSnapshot(const ReplacePlan& plan, std::size_t index, bool after) {
    if (index >= plan.files.size()) return {};
    const auto text = ReadEncodedFileText(SnapshotPath(plan, index, after), kSnapshotLimit, 0, EncodingKind::utf8);
    if (!text || !MatchesText(text->text, plan.files[index], after)) return {};
    return text->text;
}

ReplacePlan PreviewWorkspaceReplacement(const std::vector<std::filesystem::path>& roots,
    std::wstring_view query, std::wstring_view replacement, const ReplaceOptions& options,
    const std::vector<ReplaceBufferSnapshot>& buffers, const std::filesystem::path& history,
    std::stop_token stop) {
    ReplacePlan plan;
    plan.query = query; plan.replacement = replacement; plan.backups = options.create_backups;
    if (query.empty()) return plan;
    if (const auto probe = ReplaceAllInText(L"", query, replacement, options.search); !probe.error.empty()) {
        plan.error = probe.error; return plan;
    }
    std::error_code error;
    std::filesystem::create_directories(history, error);
    if (error) { plan.error = Tr(L"The replacement history folder could not be created"); return plan; }
    const auto history_size = HistorySize(history);
    if (history_size >= kHistoryLimit - 4 * 1024 * 1024 || LoadReplacementHistory(history).size() >= 100) {
        plan.error = Tr(L"Replacement history is full. Review and delete old operations before replacing more files");
        return plan;
    }
    static std::atomic_uint64_t sequence{0};
    FILETIME time{}; ::GetSystemTimeAsFileTime(&time);
    const auto ticks = (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    for (int attempt = 0; attempt < 100; ++attempt) {
        plan.directory = history / (std::to_wstring(ticks) + L"-" + std::to_wstring(::GetCurrentProcessId()) +
            L"-" + std::to_wstring(sequence.fetch_add(1)));
        if (std::filesystem::create_directory(plan.directory, error)) break;
        plan.directory.clear();
        if (error) break;
    }
    if (plan.directory.empty()) { plan.error = Tr(L"The replacement journal could not be created"); return plan; }
    std::uintmax_t staged_size = 0;
    std::size_t span_count = 0;
    const auto stage = [&](ReviewedReplacement entry, std::wstring_view before) {
        auto replaced = ReplaceAllInText(before, query, replacement, options.search, stop);
        if (replaced.cancelled) { plan.cancelled = true; return false; }
        if (!replaced.error.empty()) { plan.error = replaced.error; return false; }
        if (!replaced.count || replaced.text == before) return true;
        const auto original = EncodeText(before, EncodingKind::utf8);
        const auto output = EncodeText(replaced.text, EncodingKind::utf8);
        if (!original || !output) { ++plan.files_skipped; return true; }
        const auto amount = original->size() + output->size();
        if (original->size() > kSnapshotLimit || output->size() > kSnapshotLimit ||
            amount > kPlanLimit - staged_size || amount + 4 * 1024 * 1024 > kHistoryLimit - history_size - staged_size ||
            plan.files.size() == kFiles) { plan.truncated = true; return false; }
        entry.before_size = original->size(); entry.before_hash = Hash(*original);
        entry.after_size = output->size(); entry.after_hash = Hash(*output);
        entry.replacements = replaced.count; entry.spans = std::move(replaced.spans);
        if (entry.spans.size() > 4096 - span_count) entry.spans.resize(4096 - span_count);
        span_count += entry.spans.size();
        if (!entry.document && !EncodeText(replaced.text, entry.encoding, entry.code_page, entry.bom)) {
            entry.selected = false; entry.progress = ReplaceProgress::failed;
            entry.error = Tr(L"The replacement is not representable in the file's encoding");
        }
        if (!entry.document && plan.backups && std::ranges::any_of(options.excluded_paths, [&](const auto& path) {
                return Key(path) == Key(entry.path.wstring() + L".bak");
            })) {
            entry.selected = false; entry.progress = ReplaceProgress::failed;
            entry.error = Tr(L"The adjacent backup is open in the editor; disable adjacent backups and preview again");
        }
        const auto index = plan.files.size();
        if (!WriteEncodedFileAtomic(SnapshotPath(plan, index, false), before, EncodingKind::utf8) ||
            !WriteEncodedFileAtomic(SnapshotPath(plan, index, true), replaced.text, EncodingKind::utf8)) {
            plan.error = Tr(L"The replacement snapshots could not be saved"); return false;
        }
        staged_size += amount;
        plan.files.push_back(std::move(entry));
        return true;
    };
    std::set<std::wstring> seen, excluded;
    for (const auto& path : options.excluded_paths) excluded.insert(Key(path));
    bool finished = false;
    for (const auto& root : roots) {
        if (finished || plan.cancelled) break;
        const auto scan = ScanWorkspace(root, options.maximum_files, stop, options.search.use_gitignore);
        plan.truncated = plan.truncated || scan.truncated;
        plan.files_skipped += scan.skipped;
        for (const auto& item : scan.entries) {
            if (stop.stop_requested()) { plan.cancelled = true; break; }
            if (item.directory) continue;
            const auto path = std::filesystem::absolute(root / item.relative_path).lexically_normal();
            const auto key = Key(path);
            const auto history_key = Key(history) + L"\\";
            if (key.starts_with(history_key) || !seen.insert(key).second) continue;
            if (!MatchesWorkspaceGlobs(item.relative_path, options.search.include_globs, options.search.exclude_globs)) {
                ++plan.files_skipped; continue;
            }
            const auto buffer = std::ranges::find_if(buffers, [&](const auto& item) { return Key(item.path) == key; });
            if (buffer != buffers.end()) {
                ReviewedReplacement entry;
                entry.path = path; entry.document = buffer->document; entry.revision = buffer->revision;
                if (!stage(std::move(entry), buffer->text)) { finished = true; break; }
                continue;
            }
            if (excluded.contains(key)) { ++plan.files_skipped; continue; }
            auto text = ReadEncodedFileText(path, options.search.maximum_file_size);
            if (!text) {
                const auto fallback = ResolveEditorConfig(path).encoding;
                if (fallback == EncodingKind::utf16_le || fallback == EncodingKind::utf16_be)
                    text = ReadEncodedFileText(path, options.search.maximum_file_size, 0, {}, fallback);
            }
            if (!text) { ++plan.files_skipped; continue; }
            ++plan.files_searched;
            ReviewedReplacement entry;
            entry.path = path; entry.encoding = text->analysis.encoding;
            entry.code_page = text->analysis.code_page; entry.bom = text->analysis.has_bom;
            entry.original_state = text->state;
            if (!stage(std::move(entry), text->text)) { finished = true; break; }
        }
    }
    if (plan.error.empty() && !SaveReplacePlan(plan)) plan.error = Tr(L"The replacement journal could not be saved");
    return plan;
}

void ApplyReplacePlan(ReplacePlan& plan, std::stop_token stop) {
    plan.preflight_failed = false; plan.cancelled = false;
    for (std::size_t index = 0; index < plan.files.size(); ++index) {
        auto& entry = plan.files[index];
        if (!entry.selected) continue;
        if (stop.stop_requested()) { plan.cancelled = true; return; }
        if (entry.progress != ReplaceProgress::ready || !ReadReplacementSnapshot(plan, index, false) ||
            !ReadReplacementSnapshot(plan, index, true)) {
            Fail(plan, entry, Tr(L"The preview or its recovery copy is unavailable. Preview again")); continue;
        }
        if (entry.document) continue;
        const auto current = ReadTarget(entry);
        if (!current || !MatchesEncodedFileState(entry.original_state, current->state) ||
            !MatchesText(current->text, entry, false)) {
            Fail(plan, entry, Tr(L"The file changed after preview. Preview again")); continue;
        }
        if (plan.backups) {
            const auto backup = Key(entry.path.wstring() + L".bak");
            if (std::ranges::any_of(plan.files, [&](const auto& other) {
                return other.selected && Key(other.path) == backup;
            })) Fail(plan, entry, Tr(L"A backup would overwrite another selected file. Disable adjacent backups or exclude that file"));
        }
    }
    if (plan.preflight_failed) { static_cast<void>(Persist(plan)); return; }
    plan.started = true;
    if (!Persist(plan)) return;
    for (std::size_t index = 0; index < plan.files.size(); ++index) {
        auto& entry = plan.files[index];
        if (!entry.selected || entry.document) continue;
        if (stop.stop_requested()) { plan.cancelled = true; break; }
        const auto before = ReadReplacementSnapshot(plan, index, false);
        const auto after = ReadReplacementSnapshot(plan, index, true);
        if (!before || !after) {
            entry.progress = ReplaceProgress::failed;
            entry.error = Tr(L"The recovery copy failed its integrity check");
            plan.error = Tr(L"A recovery copy changed during application; earlier changes are recorded in Replacement History");
            break;
        }
        entry.progress = ReplaceProgress::writing;
        if (!Persist(plan)) break;
        bool backup_ok = true;
        if (plan.backups) {
            const std::filesystem::path backup{entry.path.wstring() + L".bak"};
            const auto expected = ReadEncodedFileState(backup);
            backup_ok = expected && WriteEncodedFileAtomic(backup, *before, entry.encoding,
                entry.code_page, *expected, entry.bom);
        }
        FileState written;
        if (backup_ok && WriteEncodedFileAtomic(entry.path, *after, entry.encoding,
                entry.code_page, entry.original_state, entry.bom, &written)) {
            entry.progress = ReplaceProgress::applied; entry.written_state = written;
        } else {
            entry.progress = ReplaceProgress::failed;
            entry.error = backup_ok ? Tr(L"The file changed on disk or could not be written") : Tr(L"The adjacent backup could not be created");
        }
        if (!Persist(plan)) break;
    }
    static_cast<void>(Persist(plan));
}

void RestoreReplacePlan(ReplacePlan& plan, std::stop_token stop) {
    plan.cancelled = false; plan.error.clear();
    if (!plan.started) return;
    for (std::size_t index = 0; index < plan.files.size(); ++index) {
        auto& entry = plan.files[index];
        if (!entry.selected || entry.document || entry.progress == ReplaceProgress::ready ||
            entry.progress == ReplaceProgress::restored || entry.progress == ReplaceProgress::conflict) continue;
        if (stop.stop_requested()) { plan.cancelled = true; break; }
        const auto before = ReadReplacementSnapshot(plan, index, false);
        const auto after = ReadReplacementSnapshot(plan, index, true);
        const auto current = ReadTarget(entry);
        if (!before || !after || !current) {
            entry.error = Tr(L"The file or recovery copy could not be read");
            if (!Persist(plan)) break;
            continue;
        }
        if (entry.progress == ReplaceProgress::restoring && MatchesText(current->text, entry, false)) {
            entry.progress = ReplaceProgress::restored;
            if (!Persist(plan)) break;
            continue;
        }
        if (!MatchesText(current->text, entry, true) ||
            (entry.written_state && !MatchesEncodedFileState(*entry.written_state, current->state))) {
            entry.error = Tr(L"The file has newer changes; it was not restored");
            if (!Persist(plan)) break;
            continue;
        }
        entry.progress = ReplaceProgress::restoring;
        if (!Persist(plan)) break;
        if (WriteEncodedFileAtomic(entry.path, *before, entry.encoding,
                entry.code_page, current->state, entry.bom)) {
            entry.progress = ReplaceProgress::restored; entry.error.clear();
        } else entry.error = Tr(L"The file changed on disk or could not be restored");
        if (!Persist(plan)) break;
    }
    static_cast<void>(Persist(plan));
}

std::vector<ReplacePlan> LoadReplacementHistory(const std::filesystem::path& history) {
    std::vector<ReplacePlan> plans;
    std::error_code error;
    for (std::filesystem::directory_iterator it(history, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_directory(error) || it->is_symlink(error)) continue;
        auto plan = LoadReplacePlan(it->path());
        if (plan) plans.push_back(std::move(*plan));
        else {
            ReplacePlan unavailable;
            unavailable.directory = it->path();
            unavailable.query = Tr(L"Unreadable or incomplete operation");
            plans.push_back(std::move(unavailable));
        }
    }
    std::ranges::sort(plans, [](const auto& left, const auto& right) {
        return left.directory.filename() > right.directory.filename();
    });
    return plans;
}

bool DeleteReplacePlan(const std::filesystem::path& history, const std::filesystem::path& directory) noexcept {
    try {
        const auto root = std::filesystem::absolute(history).lexically_normal();
        const auto target = std::filesystem::absolute(directory).lexically_normal();
        if (Key(target.parent_path()) != Key(root) || target.filename().empty() ||
            std::filesystem::is_symlink(target)) return false;
        std::error_code error;
        std::filesystem::remove_all(target, error);
        return !error;
    } catch (...) { return false; }
}
} // namespace notepad_colon
