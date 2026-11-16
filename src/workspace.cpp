#include <notepad_colon/workspace.h>
#include <notepad_colon/encoding_analysis.h>
#include <notepad_colon/editor_config.h>
#include <notepad_colon/localization.h>

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <span>
#include <stdexcept>

namespace notepad_colon {
namespace {
std::wstring Lower(std::wstring_view value) {
    std::wstring result{value};
    std::transform(result.begin(), result.end(), result.begin(), std::towlower);
    return result;
}

bool IsWord(wchar_t value) noexcept { return std::iswalnum(value) != 0 || value == L'_'; }

struct LoadedText {
    std::wstring text;
    EncodingKind encoding = EncodingKind::utf8;
    unsigned int code_page = 65001;
    FileState state;
    bool unicode_bom = true;
};

// Reads and decodes a file the same way the editor does: Unicode by byte
// order mark or validity, otherwise the detected legacy code page.
std::optional<LoadedText> LoadText(const std::filesystem::path& path,
                                   std::uintmax_t maximum_size) {
    auto decoded = ReadEncodedFileText(path, maximum_size);
    if (!decoded) {
        const auto fallback = ResolveEditorConfig(path).encoding;
        if (fallback == EncodingKind::utf16_le || fallback == EncodingKind::utf16_be)
            decoded = ReadEncodedFileText(path, maximum_size, 0, {}, fallback);
    }
    if (!decoded) return std::nullopt;
    LoadedText loaded;
    loaded.text = std::move(decoded->text);
    loaded.encoding = decoded->analysis.encoding;
    loaded.code_page = decoded->analysis.code_page;
    loaded.state = decoded->state;
    loaded.unicode_bom = decoded->analysis.has_bom;
    return loaded;
}

bool DecodeText(const std::filesystem::path& path, std::wstring& text,
                std::uintmax_t maximum_size) {
    auto loaded = LoadText(path, maximum_size);
    if (!loaded) return false;
    text = std::move(loaded->text);
    return true;
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

std::vector<std::wregex> CompileGlobs(const std::vector<std::wstring>& patterns) {
    std::vector<std::wregex> result;
    for (const auto& pattern : patterns) {
        if (pattern.empty()) continue;
        try {
            result.emplace_back(GlobRegex(pattern, false),
                std::regex_constants::ECMAScript | std::regex_constants::icase);
        } catch (const std::regex_error&) {}
    }
    return result;
}
bool MatchesAny(const std::filesystem::path& relative,
                const std::vector<std::wregex>& patterns) {
    const auto value = relative.generic_wstring();
    return std::ranges::any_of(patterns, [&](const auto& pattern) {
        return std::regex_match(value, pattern);
    });
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

SearchResult SearchText(const std::filesystem::path& path, std::wstring_view text,
                        std::wstring_view query, const SearchOptions& options,
                        std::stop_token stop) {
    SearchResult result;
    if (query.empty() || options.maximum_results == 0) return result;
    ++result.files_searched;
    std::optional<std::wregex> expression;
    if (options.regular_expression) {
        try {
            auto flags = std::regex_constants::ECMAScript;
            if (!options.match_case) flags |= std::regex_constants::icase;
            expression.emplace(std::wstring{query}, flags);
        } catch (const std::regex_error&) {
            result.error = L"Invalid regular expression"; return result;
        }
        using ViewRegexIterator = std::regex_iterator<std::wstring_view::const_iterator>;
        for (ViewRegexIterator match{text.begin(), text.end(), *expression}, end;
             match != end; ++match) {
            if (stop.stop_requested()) { result.cancelled = true; return result; }
            const auto offset = static_cast<std::size_t>(match->position());
            const auto [line, column] = LineColumn(text, offset);
            result.matches.push_back({path, line, column, Preview(text, offset)});
            if (result.matches.size() == options.maximum_results) {
                result.truncated = true; return result;
            }
        }
        return result;
    }
    for (std::size_t offset = 0; offset < text.size();) {
        if (stop.stop_requested()) { result.cancelled = true; return result; }
        if (MatchAt(text, query, offset, options)) {
            const auto [line, column] = LineColumn(text, offset);
            result.matches.push_back({path, line, column, Preview(text, offset)});
            if (result.matches.size() == options.maximum_results) {
                result.truncated = true; return result;
            }
            offset += query.size();
        } else ++offset;
    }
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
    const auto include_patterns = CompileGlobs(options.include_globs);
    const auto exclude_patterns = CompileGlobs(options.exclude_globs);
    result.files_skipped += scan.skipped;
    if (stop.stop_requested()) {
        result.cancelled = true;
        return result;
    }
    for (const auto& entry : scan.entries) {
        if (stop.stop_requested()) { result.cancelled = true; break; }
        if (entry.directory) continue;
        if ((!include_patterns.empty() && !MatchesAny(entry.relative_path, include_patterns)) ||
            MatchesAny(entry.relative_path, exclude_patterns)) {
            ++result.files_skipped; continue;
        }
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

bool MatchesWorkspaceGlobs(const std::filesystem::path& relative,
                           const std::vector<std::wstring>& include_globs,
                           const std::vector<std::wstring>& exclude_globs) {
    const auto include_patterns = CompileGlobs(include_globs);
    return (include_patterns.empty() || MatchesAny(relative, include_patterns)) &&
           !MatchesAny(relative, CompileGlobs(exclude_globs));
}

namespace {
constexpr std::size_t kMaximumReplacementText = 32 * 1024 * 1024;
struct ReplacementOutputIterator {
    using iterator_category = std::output_iterator_tag;
    using value_type = void;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = void;
    std::wstring* output;
    ReplacementOutputIterator& operator*() { return *this; }
    ReplacementOutputIterator& operator++() { return *this; }
    ReplacementOutputIterator operator++(int) { return *this; }
    ReplacementOutputIterator& operator=(wchar_t value) {
        if (output->size() >= kMaximumReplacementText) throw std::length_error("replacement limit");
        output->push_back(value); return *this;
    }
};
}
TextReplacement ReplaceAllInText(std::wstring_view text, std::wstring_view query,
                                 std::wstring_view replacement, const SearchOptions& options,
                                 std::stop_token stop) {
    TextReplacement result;
    const auto cancel = [&] {
        result.text.clear(); result.count = 0; result.spans.clear(); result.cancelled = true;
    };
    const auto too_large = [&] {
        result.text.clear(); result.count = 0; result.spans.clear();
        result.error = Tr(L"Replacement output exceeds the 64 MiB text limit; use a smaller scope or replacement");
    };
    if (stop.stop_requested()) { cancel(); return result; }
    if (query.empty()) { result.text = std::wstring{text}; return result; }
    if (options.regular_expression) {
        try {
            auto flags = std::regex_constants::ECMAScript;
            if (!options.match_case) flags |= std::regex_constants::icase;
            const std::wregex expression{std::wstring{query}, flags};
            const std::wstring format{replacement};
            using Iterator = std::regex_iterator<std::wstring_view::const_iterator>;
            auto cursor = text.begin();
            for (Iterator match{text.begin(), text.end(), expression}, end; match != end; ++match) {
                if (stop.stop_requested()) { cancel(); return result; }
                const auto prefix_size = static_cast<std::size_t>((*match)[0].first - cursor);
                if (prefix_size > kMaximumReplacementText - result.text.size()) throw std::length_error("replacement limit");
                result.text.append(cursor, (*match)[0].first);
                const auto start = result.text.size();
                match->format(ReplacementOutputIterator{&result.text}, format);
                if (result.spans.size() < 256)
                    result.spans.push_back({static_cast<std::size_t>((*match)[0].first - text.begin()),
                        static_cast<std::size_t>(match->length()), start, result.text.size() - start});
                cursor = (*match)[0].second;
                ++result.count;
            }
            if (static_cast<std::size_t>(text.end() - cursor) > kMaximumReplacementText - result.text.size())
                throw std::length_error("replacement limit");
            result.text.append(cursor, text.end());
        } catch (const std::length_error&) { too_large();
        } catch (const std::regex_error&) {
            result.text = std::wstring{text};
            result.count = 0;
            result.spans.clear();
            result.error = L"Invalid regular expression";
        }
        return result;
    }
    result.text.reserve((std::min)(text.size(), kMaximumReplacementText));
    std::size_t copied = 0;
    for (std::size_t offset = 0; offset + query.size() <= text.size();) {
        if (stop.stop_requested()) { cancel(); return result; }
        if (MatchAt(text, query, offset, options)) {
            const auto amount = offset - copied;
            if (amount > kMaximumReplacementText - result.text.size() ||
                replacement.size() > kMaximumReplacementText - result.text.size() - amount) {
                too_large(); return result;
            }
            result.text.append(text, copied, amount);
            if (result.spans.size() < 256)
                result.spans.push_back({offset, query.size(), result.text.size(), replacement.size()});
            result.text.append(replacement);
            offset += query.size();
            copied = offset;
            ++result.count;
        } else ++offset;
    }
    if (text.size() - copied > kMaximumReplacementText - result.text.size()) { too_large(); return result; }
    result.text.append(text, copied, text.size() - copied);
    return result;
}

ReplaceResult ReplaceInWorkspace(const std::filesystem::path& root, std::wstring_view query,
                                 std::wstring_view replacement, const ReplaceOptions& options,
                                 std::stop_token stop) {
    ReplaceResult result;
    if (query.empty()) return result;
    if (options.search.regular_expression) {
        // Validate once so an invalid pattern is one error, not one per file.
        if (const auto probe = ReplaceAllInText(L"", query, replacement, options.search);
            !probe.error.empty()) {
            result.error = probe.error;
            return result;
        }
    }
    const auto scan = ScanWorkspace(root, options.maximum_files, stop, options.search.use_gitignore);
    result.files_skipped += scan.skipped;
    result.truncated = scan.truncated;
    const auto include_patterns = CompileGlobs(options.search.include_globs);
    const auto exclude_patterns = CompileGlobs(options.search.exclude_globs);
    const auto excluded = [&](const std::filesystem::path& path) {
        const auto normal = Lower(path.lexically_normal().wstring());
        return std::ranges::any_of(options.excluded_paths, [&](const auto& candidate) {
            return Lower(candidate.lexically_normal().wstring()) == normal;
        });
    };
    for (const auto& entry : scan.entries) {
        if (stop.stop_requested()) { result.cancelled = true; break; }
        if (entry.directory) continue;
        if ((!include_patterns.empty() && !MatchesAny(entry.relative_path, include_patterns)) ||
            MatchesAny(entry.relative_path, exclude_patterns)) {
            ++result.files_skipped; continue;
        }
        const auto path = root / entry.relative_path;
        if (excluded(path)) { ++result.files_skipped; continue; }
        const auto loaded = LoadText(path, options.search.maximum_file_size);
        if (!loaded) { ++result.files_skipped; continue; }
        ++result.files_searched;
        auto replaced = ReplaceAllInText(loaded->text, query, replacement, options.search, stop);
        if (replaced.cancelled) { result.cancelled = true; break; }
        if (!replaced.error.empty()) { result.error = replaced.error; break; }
        if (replaced.count == 0) continue;
        ReplaceFileOutcome outcome{path, replaced.count};
        if (options.dry_run) {
            result.replacements += replaced.count;
            result.files.push_back(std::move(outcome));
            continue;
        }
        // A replacement that the file's own encoding cannot represent must not
        // silently change that encoding or drop characters.
        if (!EncodeText(replaced.text, loaded->encoding, loaded->code_page, loaded->unicode_bom)) {
            outcome.error = L"The replacement is not representable in the file's encoding";
        } else {
            if (options.create_backups) {
                auto backup = path;
                backup += L".bak";
                if (!::CopyFileW(path.c_str(), backup.c_str(), FALSE))
                    outcome.error = L"The backup could not be created";
            }
            if (outcome.error.empty()) {
                const auto expected = loaded->state;
                if (WriteEncodedFileAtomic(path, replaced.text, loaded->encoding,
                                           loaded->code_page, expected, loaded->unicode_bom))
                    outcome.written = true;
                else outcome.error = L"The file changed on disk or could not be written";
            }
        }
        if (outcome.written) {
            ++result.files_changed;
            result.replacements += replaced.count;
        } else ++result.files_failed;
        result.files.push_back(std::move(outcome));
    }
    return result;
}

FileState CaptureFileState(const std::filesystem::path& path) noexcept {
    return ReadEncodedFileState(path).value_or(FileState{});
}

}  // namespace notepad_colon
