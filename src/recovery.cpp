#include <notepad_colon/recovery.h>
#include <notepad_colon/encoding_analysis.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cwctype>
#include <fstream>
#include <limits>
#include <set>
#include <map>

namespace notepad_colon {
namespace {
constexpr std::uint64_t kMaximumText = 128ull * 1024 * 1024;
std::uint64_t Hash(std::string_view bytes, std::uint64_t value = 14695981039346656037ull) {
    for (const auto byte : bytes) { value ^= static_cast<unsigned char>(byte); value *= 1099511628211ull; }
    return value;
}
std::optional<std::string> Utf8(std::wstring_view text) {
    if (text.find(L'\0') != std::wstring_view::npos) return {};
    const auto bytes = EncodeText(text, EncodingKind::utf8);
    if (!bytes) return {};
    return std::string{bytes->begin(), bytes->end()};
}
std::optional<std::wstring> Wide(std::string_view text) {
    const auto value = DecodeFileText({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()},
        0, EncodingKind::utf8);
    return value ? std::optional{value->text} : std::nullopt;
}
std::wstring SafeName(std::wstring_view key) {
    std::wstring result;
    for (const auto value : key.substr(0, 80)) result += std::iswalnum(value) ? value : L'_';
    return result.empty() ? L"document" : result;
}
bool Owned(const std::filesystem::path& directory, const std::filesystem::path& file) {
    return std::filesystem::absolute(file).lexically_normal().parent_path() ==
        std::filesystem::absolute(directory).lexically_normal();
}
struct Header {
    std::uint64_t title = 0, path = 0, text = 0, document = 0, checksum = 0;
    bool checked = false;
};
std::optional<Header> ReadHeader(std::ifstream& input, std::uintmax_t size) {
    char signature[64]{}, sizes[256]{};
    if (!input.getline(signature, sizeof(signature)) || !input.getline(sizes, sizeof(sizes))) return {};
    const std::string_view version{signature};
    const bool checked = version == "NPCRECOVERY\t2";
    if (!checked && version != "NPCRECOVERY\t1") return {};
    std::vector<std::uint64_t> fields;
    std::string_view remaining{sizes};
    for (;;) {
        const auto tab = remaining.find('\t');
        const auto token = remaining.substr(0, tab);
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) return {};
        fields.push_back(value);
        if (tab == std::string_view::npos) break;
        remaining.remove_prefix(tab + 1);
    }
    if (fields.size() != (checked ? 5u : 2u) || fields[0] > 4096 || fields[1] > 32768) return {};
    const auto position = input.tellg();
    if (position < 0 || static_cast<std::uint64_t>(position) > size) return {};
    const auto metadata = static_cast<std::uint64_t>(position) + fields[0] + fields[1];
    if (metadata > size) return {};
    Header header{fields[0], fields[1], size - metadata, 0, 0, checked};
    if (header.text > kMaximumText) return {};
    if (checked) {
        if (fields[2] != header.text) return {};
        header.document = fields[3]; header.checksum = fields[4];
    }
    return header;
}
std::uintmax_t TotalSize(const std::vector<RecoverySnapshot>& snapshots) {
    std::uintmax_t size = 0;
    for (const auto& snapshot : snapshots) {
        if (snapshot.size > (std::numeric_limits<std::uintmax_t>::max)() - size)
            return (std::numeric_limits<std::uintmax_t>::max)();
        size += snapshot.size;
    }
    return size;
}
bool WriteSnapshot(const std::filesystem::path& path, std::initializer_list<std::string_view> parts) {
    const auto temporary = path.wstring() + L".tmp";
    const auto file = ::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool good = true;
    for (auto data : parts)
        while (!data.empty() && good) {
            const auto count = static_cast<DWORD>((std::min<std::size_t>)(1024 * 1024, data.size()));
            DWORD written = 0;
            good = ::WriteFile(file, data.data(), count, &written, nullptr) && written == count;
            data.remove_prefix(written);
        }
    if (good) good = ::FlushFileBuffers(file) != FALSE;
    ::CloseHandle(file);
    if (good) good = ::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!good) ::DeleteFileW(temporary.c_str());
    return good;
}
} // namespace

RecoveryStore::RecoveryStore(std::filesystem::path directory, std::size_t retention,
                             std::uintmax_t maximum_bytes)
    : directory_(std::move(directory)), retention_((std::max)(std::size_t{1}, retention)),
      maximum_bytes_(maximum_bytes) {}

bool RecoveryStore::Save(std::wstring_view document_key, std::wstring_view title,
                         const std::filesystem::path& original_path, std::wstring_view text) {
    std::scoped_lock lock{mutex_};
    quota_full_.store(false);
    const auto key = Utf8(document_key), caption = Utf8(title), path = Utf8(original_path.wstring()), body = Utf8(text);
    if (!key || !caption || !path || !body || caption->size() > 4096 || path->size() > 32768 || body->size() > kMaximumText)
        return false;
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) return false;
    const auto document = Hash(*key);
    const auto checksum = Hash(*body, Hash(*path, Hash(*caption)));
    const auto header = std::string{"NPCRECOVERY\t2\n"} + std::to_string(caption->size()) + '\t' +
        std::to_string(path->size()) + '\t' + std::to_string(body->size()) + '\t' +
        std::to_string(document) + '\t' + std::to_string(checksum) + '\n';
    const auto bytes = header.size() + caption->size() + path->size() + body->size();
    Prune();
    const auto previous = List();
    const auto occupied = TotalSize(previous);
    // Admit conservatively before writing, with room for both generations.
    // If old files cannot be removed, the newest valid copy is still retained.
    if (bytes > maximum_bytes_ || occupied > maximum_bytes_ - bytes || previous.size() >= 1000) {
        quota_full_.store(true); return false;
    }
    static std::atomic_uint64_t sequence{0};
    const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto serial = std::to_wstring(sequence.fetch_add(1));
    serial.insert(0, 20 - serial.size(), L'0');
    const auto target = directory_ / (SafeName(document_key) + L"-" + std::to_wstring(ticks) + L"-" +
        std::to_wstring(::GetCurrentProcessId()) + L"-" + serial + L".recovery");
    if (!WriteSnapshot(target, {header, *caption, *path, *body})) return false;
    Prune();
    return true;
}

std::vector<RecoverySnapshot> RecoveryStore::List() const {
    std::scoped_lock lock{mutex_};
    std::vector<RecoverySnapshot> result;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory_, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || it->is_symlink(error) || (it->path().extension() != L".recovery" &&
                !it->path().filename().wstring().ends_with(L".recovery.tmp"))) continue;
        const auto size = it->file_size(error);
        const auto written = it->last_write_time(error);
        if (error) break;
        const auto created = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            std::chrono::clock_cast<std::chrono::system_clock>(written));
        std::ifstream input(it->path(), std::ios::binary);
        const auto header = ReadHeader(input, size);
        std::optional<std::wstring> title, path;
        if (header) {
            std::string title_bytes(static_cast<std::size_t>(header->title), '\0');
            std::string path_bytes(static_cast<std::size_t>(header->path), '\0');
            input.read(title_bytes.data(), static_cast<std::streamsize>(title_bytes.size()));
            input.read(path_bytes.data(), static_cast<std::streamsize>(path_bytes.size()));
            if (input) { title = Wide(title_bytes); path = Wide(path_bytes); }
        }
        if (!header || !title || !path) {
            const auto filename = it->path().filename().wstring();
            result.push_back({it->path(), {}, filename, created, size, Hash(Utf8(filename).value_or("damaged"))});
            continue; // Keep damaged records visible so the user can inspect or delete them.
        }
        const auto document = header->checked ? header->document : Hash(Utf8(*path).value_or("") +
            (path->empty() ? Utf8(*title).value_or("") : std::string{}));
        result.push_back({it->path(), *path, *title, created, size, document});
    }
    std::ranges::sort(result, [](const auto& left, const auto& right) {
        if (left.created != right.created) return left.created > right.created;
        return left.file.filename().native() > right.file.filename().native();
    });
    return result;
}

std::optional<std::wstring> RecoveryStore::Load(const RecoverySnapshot& snapshot) const {
    std::scoped_lock lock{mutex_};
    if (!Owned(directory_, snapshot.file)) return {};
    std::error_code error;
    const auto size = std::filesystem::file_size(snapshot.file, error);
    if (error) return {};
    std::ifstream input(snapshot.file, std::ios::binary);
    const auto header = ReadHeader(input, size);
    if (!header) return {};
    const auto count = static_cast<std::size_t>(header->title + header->path + header->text);
    std::string bytes(count, '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input || (header->checked && Hash(bytes) != header->checksum)) return {};
    return Wide(std::string_view{bytes}.substr(static_cast<std::size_t>(header->title + header->path)));
}

bool RecoveryStore::Remove(const RecoverySnapshot& snapshot) const {
    std::scoped_lock lock{mutex_};
    if (!Owned(directory_, snapshot.file)) return false;
    std::error_code error;
    return std::filesystem::remove(snapshot.file, error) && !error;
}

void RecoveryStore::Prune() const {
    std::scoped_lock lock{mutex_};
    const auto snapshots = List();
    std::map<std::uint64_t, unsigned int> newest;
    std::set<std::filesystem::path> protected_files;
    for (const auto& snapshot : snapshots) {
        auto& valid_generations = newest[snapshot.document];
        if (valid_generations >= 2) continue;
        protected_files.insert(snapshot.file);
        // A valid header alone is not a usable fallback. Retain damaged newer
        // records for inspection and find two actually loadable generations
        // before allowing an older recovery point to be pruned.
        if (Load(snapshot)) ++valid_generations;
    }
    auto bytes = TotalSize(snapshots);
    auto count = snapshots.size();
    for (std::size_t index = snapshots.size(); index-- > 0;) {
        if (protected_files.contains(snapshots[index].file)) continue;
        if (index >= retention_ || bytes > maximum_bytes_ || count > 1000) {
            if (Remove(snapshots[index])) { bytes -= (std::min)(bytes, snapshots[index].size); --count; }
        }
    }
}
} // namespace notepad_colon
