#include <notepad_colon/editor_config.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <vector>

namespace notepad_colon {
namespace {
std::string Trim(std::string value) {
    const auto whitespace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(), value.end());
    return value;
}

std::string Lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool GlobMatch(std::string_view pattern, std::string_view text) {
    std::size_t p = 0, t = 0, star = std::string_view::npos, retry = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) { ++p; ++t; }
        else if (p < pattern.size() && pattern[p] == '*') { star = p++; retry = t; }
        else if (star != std::string_view::npos) { p = star + 1; t = ++retry; }
        else return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

void Apply(EditorConfigSettings& out, std::string key, std::string value) {
    key = Lower(Trim(std::move(key))); value = Lower(Trim(std::move(value)));
    if (value == "unset") return;
    if (key == "indent_style") out.use_tabs = value == "tab";
    else if (key == "indent_size" && value != "tab") {
        try { const auto size = std::stoul(value); if (size >= 1 && size <= 16) out.indent_size = size; }
        catch (...) {}
    } else if (key == "end_of_line") {
        if (value == "lf") out.line_ending = LineEnding::lf;
        else if (value == "crlf") out.line_ending = LineEnding::crlf;
        else if (value == "cr") out.line_ending = LineEnding::cr;
    } else if (key == "charset") {
        if (value == "utf-8") out.encoding = EncodingKind::utf8;
        else if (value == "utf-8-bom") out.encoding = EncodingKind::utf8_bom;
        else if (value == "utf-16le") out.encoding = EncodingKind::utf16_le;
        else if (value == "utf-16be") out.encoding = EncodingKind::utf16_be;
    } else if (key == "trim_trailing_whitespace") out.trim_trailing_whitespace = value == "true";
    else if (key == "insert_final_newline") out.insert_final_newline = value == "true";
}

struct ConfigFile { std::filesystem::path path; bool root = false; };
}

EditorConfigSettings ResolveEditorConfig(const std::filesystem::path& file) {
    EditorConfigSettings result;
    std::vector<ConfigFile> configs;
    auto directory = file.parent_path();
    for (std::size_t depth = 0; !directory.empty() && depth < 64; ++depth) {
        const auto config = directory / L".editorconfig";
        if (std::filesystem::is_regular_file(config)) {
            bool root = false; std::ifstream input(config); std::string line;
            while (std::getline(input, line)) {
                const auto equal = line.find('=');
                if (equal != std::string::npos && Lower(Trim(line.substr(0, equal))) == "root" &&
                    Lower(Trim(line.substr(equal + 1))) == "true") { root = true; break; }
            }
            configs.push_back({config, root});
            if (root) break;
        }
        const auto parent = directory.parent_path();
        if (parent == directory) break;
        directory = parent;
    }
    std::ranges::reverse(configs);
    for (const auto& config : configs) {
        std::ifstream input(config.path); std::string line, section; bool active = false;
        const auto relative = std::filesystem::relative(file, config.path.parent_path()).generic_string();
        const auto filename = file.filename().string();
        while (std::getline(input, line)) {
            line = Trim(std::move(line));
            if (line.empty() || line.front() == '#' || line.front() == ';') continue;
            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                active = GlobMatch(section, section.find('/') == std::string::npos ? filename : relative);
                continue;
            }
            const auto equal = line.find('=');
            if (active && equal != std::string::npos)
                Apply(result, line.substr(0, equal), line.substr(equal + 1));
        }
    }
    return result;
}

}  // namespace notepad_colon
