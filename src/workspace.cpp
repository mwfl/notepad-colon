#include <notepad_colon/workspace.h>

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>

namespace notepad_colon {
namespace {
std::wstring Lower(std::wstring_view value) {
    std::wstring result{value};
    std::transform(result.begin(), result.end(), result.begin(), std::towlower);
    return result;
}

bool IsWord(wchar_t value) noexcept { return std::iswalnum(value) != 0 || value == L'_'; }

bool DecodeText(const std::filesystem::path& path, std::wstring& text,
                std::uintmax_t maximum_size) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_size) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (bytes.find('\0') != std::string::npos) return false;
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
        static_cast<unsigned char>(bytes[1]) == 0xbb && static_cast<unsigned char>(bytes[2]) == 0xbf)
        bytes.erase(0, 3);
    if (bytes.empty()) { text.clear(); return true; }
    int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                                       static_cast<int>(bytes.size()), nullptr, 0);
    UINT page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (length <= 0) {
        page = CP_ACP;
        flags = 0;
        length = ::MultiByteToWideChar(page, flags, bytes.data(),
                                       static_cast<int>(bytes.size()), nullptr, 0);
    }
    if (length <= 0) return false;
    text.resize(static_cast<std::size_t>(length));
    return ::MultiByteToWideChar(page, flags, bytes.data(), static_cast<int>(bytes.size()),
                                 text.data(), length) == length;
}

bool MatchAt(std::wstring_view line, std::wstring_view query, std::size_t offset,
             const SearchOptions& options) {
    if (offset + query.size() > line.size()) return false;
    const int comparison = ::CompareStringOrdinal(
        line.data() + offset, static_cast<int>(query.size()), query.data(),
        static_cast<int>(query.size()), options.match_case ? FALSE : TRUE);
    if (comparison != CSTR_EQUAL) return false;
    if (!options.whole_word) return true;
    return (offset == 0 || !IsWord(line[offset - 1])) &&
           (offset + query.size() == line.size() || !IsWord(line[offset + query.size()]));
}
}  // namespace

bool IsExcludedWorkspaceDirectory(std::wstring_view name) noexcept {
    const auto lower = Lower(name);
    return lower == L".git" || lower == L".vs" || lower == L"build" ||
           lower == L"out" || lower == L"node_modules" || lower == L".cache";
}

WorkspaceScan ScanWorkspace(const std::filesystem::path& root,
                            std::size_t maximum_entries, std::stop_token stop) {
    WorkspaceScan result;
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) return result;
    std::filesystem::recursive_directory_iterator iterator{
        root, std::filesystem::directory_options::skip_permission_denied, error};
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (stop.stop_requested()) break;
        const auto& item = *iterator;
        const bool directory = item.is_directory(error);
        if (error) { error.clear(); ++result.skipped; iterator.increment(error); continue; }
        if (directory && IsExcludedWorkspaceDirectory(item.path().filename().wstring())) {
            iterator.disable_recursion_pending();
            ++result.skipped;
            iterator.increment(error);
            continue;
        }
        if (result.entries.size() >= maximum_entries) { result.truncated = true; break; }
        WorkspaceEntry entry;
        entry.relative_path = item.path().lexically_relative(root);
        entry.search_key = Lower(entry.relative_path.wstring());
        entry.directory = directory;
        if (!directory) {
            entry.size = item.file_size(error);
            if (error) { error.clear(); entry.size = 0; ++result.skipped; }
        }
        result.entries.push_back(std::move(entry));
        iterator.increment(error);
    }
    std::ranges::sort(result.entries, [](const auto& left, const auto& right) {
        if (left.directory != right.directory) return left.directory > right.directory;
        return Lower(left.relative_path.wstring()) < Lower(right.relative_path.wstring());
    });
    return result;
}

SearchResult SearchWorkspace(const std::filesystem::path& root, std::wstring_view query,
                             const SearchOptions& options, std::stop_token stop) {
    SearchResult result;
    if (query.empty() || options.maximum_results == 0) return result;
    const auto scan = ScanWorkspace(root, 100000, stop);
    result.files_skipped += scan.skipped;
    if (stop.stop_requested()) {
        result.cancelled = true;
        return result;
    }
    for (const auto& entry : scan.entries) {
        if (stop.stop_requested()) { result.cancelled = true; break; }
        if (entry.directory) continue;
        std::wstring text;
        const auto path = root / entry.relative_path;
        if (!DecodeText(path, text, options.maximum_file_size)) {
            ++result.files_skipped;
            continue;
        }
        ++result.files_searched;
        std::size_t line_number = 1;
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto end = text.find_first_of(L"\r\n", start);
            const auto line = std::wstring_view{text}.substr(
                start, (end == std::wstring::npos ? text.size() : end) - start);
            for (std::size_t offset = 0; offset < line.size(); ++offset) {
                if (!MatchAt(line, query, offset, options)) continue;
                result.matches.push_back({path, line_number, offset + 1,
                    std::wstring{line.substr(0, (std::min)(line.size(), std::size_t{300}))}});
                if (result.matches.size() >= options.maximum_results) {
                    result.truncated = true;
                    return result;
                }
                offset += query.size() - 1;
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
            if (text[end] == L'\r' && start < text.size() && text[start] == L'\n') ++start;
            ++line_number;
        }
    }
    result.truncated |= scan.truncated;
    return result;
}

FileState CaptureFileState(const std::filesystem::path& path) noexcept {
    FileState state;
    std::error_code error;
    state.exists = std::filesystem::is_regular_file(path, error);
    if (!state.exists || error) return state;
    state.size = std::filesystem::file_size(path, error);
    if (error) return {};
    state.last_write = std::filesystem::last_write_time(path, error);
    return error ? FileState{} : state;
}

}  // namespace notepad_colon
