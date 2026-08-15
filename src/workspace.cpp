#include <notepad_colon/workspace.h>

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <regex>

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

std::wstring GlobRegex(std::wstring_view pattern, bool directory) {
    std::wstring expression;
    const bool anchored = !pattern.empty() && pattern.front() == L'/';
    if (anchored) pattern.remove_prefix(1);
    const bool has_slash = pattern.find(L'/') != std::wstring_view::npos;
    expression = anchored || has_slash ? L"^" : L"(^|.*/)";
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        const auto value = pattern[index];
        if (value == L'*') {
            if (index + 1 < pattern.size() && pattern[index + 1] == L'*') {
                ++index;
                if (index + 1 < pattern.size() && pattern[index + 1] == L'/') {
                    ++index; expression += L"(.*/)?";
                } else expression += L".*";
            } else expression += L"[^/]*";
        } else if (value == L'?') expression += L"[^/]";
        else {
            if (std::wstring_view{L".^$|()[]{}+\\"}.find(value) != std::wstring_view::npos)
                expression.push_back(L'\\');
            expression.push_back(value == L'\\' ? L'/' : value);
        }
    }
    expression += directory ? L"(/.*)?$" : L"$";
    return expression;
}

struct IgnoreRule {
    std::wregex expression;
    bool negated = false;
    bool directory = false;
};

std::vector<IgnoreRule> LoadIgnoreRules(const std::filesystem::path& root) {
    std::vector<IgnoreRule> rules;
    std::wifstream input(root / L".gitignore");
    std::wstring line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line.front() == L'#') continue;
        bool negated = line.front() == L'!';
        if (negated) line.erase(line.begin());
        bool directory = !line.empty() && line.back() == L'/';
        if (directory) line.pop_back();
        if (line.empty()) continue;
        try {
            rules.push_back({std::wregex{GlobRegex(line, directory),
                std::regex_constants::ECMAScript | std::regex_constants::icase},
                negated, directory});
        } catch (const std::regex_error&) {}
    }
    return rules;
}

bool IsIgnored(const std::filesystem::path& relative, bool directory,
               const std::vector<IgnoreRule>& rules) {
    auto value = relative.generic_wstring();
    bool ignored = false;
    for (const auto& rule : rules) {
        if (rule.directory && !directory && value.find(L'/') == std::wstring::npos) continue;
        if (std::regex_match(value, rule.expression)) ignored = !rule.negated;
    }
    return ignored;
}

std::pair<std::size_t, std::size_t> LineColumn(std::wstring_view text, std::size_t offset) {
    std::size_t line = 1, column = 1;
    for (std::size_t index = 0; index < offset && index < text.size(); ++index) {
        if (text[index] == L'\n') { ++line; column = 1; }
        else if (text[index] != L'\r') ++column;
    }
    return {line, column};
}

std::wstring Preview(std::wstring_view text, std::size_t offset) {
    const auto begin = text.rfind(L'\n', offset);
    const auto start = begin == std::wstring_view::npos ? 0 : begin + 1;
    const auto end = text.find(L'\n', offset);
    auto value = std::wstring{text.substr(start,
        (std::min)(std::size_t{300}, (end == std::wstring_view::npos ? text.size() : end) - start))};
    if (!value.empty() && value.back() == L'\r') value.pop_back();
    return value;
}
}  // namespace

bool IsExcludedWorkspaceDirectory(std::wstring_view name) noexcept {
    const auto lower = Lower(name);
    return lower == L".git" || lower == L".vs" || lower == L"build" ||
           lower == L"out" || lower == L"node_modules" || lower == L".cache";
}

WorkspaceScan ScanWorkspace(const std::filesystem::path& root,
                            std::size_t maximum_entries, std::stop_token stop,
                            bool use_gitignore) {
    WorkspaceScan result;
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) return result;
    const auto ignore_rules = use_gitignore ? LoadIgnoreRules(root) : std::vector<IgnoreRule>{};
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
        const auto relative = item.path().lexically_relative(root);
        if (IsIgnored(relative, directory, ignore_rules)) {
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
    std::optional<std::wregex> regular_expression;
    if (options.regular_expression) {
        try {
            auto flags = std::regex_constants::ECMAScript;
            if (!options.match_case) flags |= std::regex_constants::icase;
            regular_expression.emplace(std::wstring{query}, flags);
        } catch (const std::regex_error&) {
            result.error = L"Invalid regular expression";
            return result;
        }
    }
    const auto scan = ScanWorkspace(root, 100000, stop, options.use_gitignore);
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
        if (options.multiline && !options.regular_expression) {
            for (std::size_t offset = 0; offset < text.size();) {
                if (MatchAt(text, query, offset, options)) {
                    const auto [line, column] = LineColumn(text, offset);
                    result.matches.push_back({path, line, column, Preview(text, offset)});
                    if (result.matches.size() >= options.maximum_results) {
                        result.truncated = true; return result;
                    }
                    offset += query.size();
                } else ++offset;
            }
            continue;
        }
        if (options.regular_expression) {
            for (std::wsregex_iterator match{text.begin(), text.end(), *regular_expression}, end;
                 match != end; ++match) {
                if (stop.stop_requested()) { result.cancelled = true; return result; }
                const auto offset = static_cast<std::size_t>(match->position());
                const auto [line, column] = LineColumn(text, offset);
                result.matches.push_back({path, line, column, Preview(text, offset)});
                if (result.matches.size() >= options.maximum_results) {
                    result.truncated = true; return result;
                }
            }
            continue;
        }
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
