#include <notepad_colon/recovery.h>

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace notepad_colon {
namespace {
std::wstring SafeName(std::wstring_view key) {
    std::wstring result;
    for (const auto c : key) result += std::iswalnum(c) ? c : L'_';
    if (result.empty()) result = L"document";
    return result.substr(0, 80);
}

std::string Utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const auto size = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                          result.data(), size, nullptr, nullptr);
    return result;
}

std::optional<std::wstring> Wide(std::string_view value) {
    if (value.empty()) return std::wstring{};
    const auto size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return std::nullopt;
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), size);
    return result;
}
}  // namespace

RecoveryStore::RecoveryStore(std::filesystem::path directory, std::size_t retention)
    : directory_(std::move(directory)), retention_((std::max)(std::size_t{1}, retention)) {}

bool RecoveryStore::Save(std::wstring_view document_key, std::wstring_view title,
                         const std::filesystem::path& original_path, std::wstring_view text) {
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) return false;
    auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto target = directory_ / (SafeName(document_key) + L"-" + std::to_wstring(ticks) + L".recovery");
    while (std::filesystem::exists(target, error) && !error)
        target = directory_ / (SafeName(document_key) + L"-" + std::to_wstring(++ticks) + L".recovery");
    if (error) return false;
    const auto temporary = target.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    const auto title_utf8 = Utf8(title);
    const auto path_utf8 = Utf8(original_path.wstring());
    const auto text_utf8 = Utf8(text);
    output << "NPCRECOVERY\t1\n" << title_utf8.size() << '\t' << path_utf8.size() << '\n';
    output.write(title_utf8.data(), static_cast<std::streamsize>(title_utf8.size()));
    output.write(path_utf8.data(), static_cast<std::streamsize>(path_utf8.size()));
    output.write(text_utf8.data(), static_cast<std::streamsize>(text_utf8.size()));
    output.close();
    if (!output || !::MoveFileExW(temporary.c_str(), target.c_str(),
                                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    Prune();
    return true;
}

std::vector<RecoverySnapshot> RecoveryStore::List() const {
    std::vector<RecoverySnapshot> result;
    std::error_code error;
    if (!std::filesystem::is_directory(directory_, error)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(directory_, error)) {
        if (error || !entry.is_regular_file(error) || entry.path().extension() != L".recovery") continue;
        std::ifstream input(entry.path(), std::ios::binary);
        std::string header, sizes;
        if (!std::getline(input, header) || header != "NPCRECOVERY\t1" || !std::getline(input, sizes)) continue;
        const auto separator = sizes.find('\t');
        if (separator == std::string::npos) continue;
        std::size_t title_size = 0, path_size = 0;
        try {
            title_size = std::stoull(sizes.substr(0, separator));
            path_size = std::stoull(sizes.substr(separator + 1));
        } catch (...) { continue; }
        if (title_size > 4096 || path_size > 32768) continue;
        std::string title(title_size, '\0'), path(path_size, '\0');
        input.read(title.data(), static_cast<std::streamsize>(title.size()));
        input.read(path.data(), static_cast<std::streamsize>(path.size()));
        const auto wide_title = Wide(title); const auto wide_path = Wide(path);
        if (!input || !wide_title || !wide_path) continue;
        const auto written = entry.last_write_time(error);
        const auto created = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            written - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
        result.push_back({entry.path(), *wide_path, *wide_title, created, entry.file_size(error)});
    }
    std::ranges::sort(result, [](const auto& left, const auto& right) {
        if (left.created != right.created) return left.created > right.created;
        // Some CI file systems expose coarser write-time precision than the
        // millisecond timestamp embedded in our snapshot filename.
        return left.file.filename().native() > right.file.filename().native();
    });
    return result;
}

std::optional<std::wstring> RecoveryStore::Load(const RecoverySnapshot& snapshot) const {
    std::ifstream input(snapshot.file, std::ios::binary);
    std::string header, sizes;
    if (!std::getline(input, header) || header != "NPCRECOVERY\t1" || !std::getline(input, sizes)) return std::nullopt;
    const auto separator = sizes.find('\t');
    if (separator == std::string::npos) return std::nullopt;
    std::size_t skip = 0;
    try { skip = std::stoull(sizes.substr(0, separator)) + std::stoull(sizes.substr(separator + 1)); }
    catch (...) { return std::nullopt; }
    input.seekg(static_cast<std::streamoff>(skip), std::ios::cur);
    std::ostringstream content; content << input.rdbuf();
    return Wide(content.str());
}

bool RecoveryStore::Remove(const RecoverySnapshot& snapshot) const {
    std::error_code error;
    return std::filesystem::remove(snapshot.file, error) && !error;
}

void RecoveryStore::Prune() const {
    auto snapshots = List();
    for (std::size_t index = retention_; index < snapshots.size(); ++index)
        static_cast<void>(Remove(snapshots[index]));
}
}  // namespace notepad_colon
