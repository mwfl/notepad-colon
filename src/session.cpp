#include <notepad_colon/session.h>

#include <charconv>
#include <sstream>

namespace notepad_colon {
namespace {
std::string Escape(std::wstring_view value) {
    std::ostringstream output;
    for (wchar_t character : value) {
        const auto code = static_cast<std::uint32_t>(character);
        if (character == L'\\' || character == L'\t' || character == L'\n' || character == L'\r') {
            output << '\\' << std::hex << code << ';' << std::dec;
        } else if (code >= 0x20 && code <= 0x7e) {
            output << static_cast<char>(code);
        } else {
            output << '\\' << std::hex << code << ';' << std::dec;
        }
    }
    return output.str();
}

bool Unescape(std::string_view value, std::wstring& output) {
    output.clear();
    for (std::size_t index = 0; index < value.size();) {
        if (value[index] != '\\') {
            output.push_back(static_cast<unsigned char>(value[index++]));
            continue;
        }
        const auto end = value.find(';', index + 1);
        if (end == std::string_view::npos) return false;
        std::uint32_t code = 0;
        const auto parsed = std::from_chars(value.data() + index + 1, value.data() + end, code, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + end || code > 0xffff) return false;
        output.push_back(static_cast<wchar_t>(code));
        index = end + 1;
    }
    return true;
}
}  // namespace

std::string SerializeSession(const Session& session) {
    std::ostringstream output;
    output << "NPCSESSION\t1\t" << session.active_index << '\n';
    for (const auto& entry : session.documents) {
        output << static_cast<int>(entry.encoding) << '\t'
               << static_cast<int>(entry.line_ending) << '\t'
               << entry.view.anchor << '\t' << entry.view.caret << '\t'
               << entry.view.first_visible_line << '\t' << (entry.dirty ? 1 : 0) << '\t'
               << Escape(entry.path.wstring()) << '\t' << Escape(entry.recovery_text) << '\n';
    }
    return output.str();
}

bool DeserializeSession(std::string_view input, Session& session) noexcept {
    try {
        std::istringstream stream{std::string(input)};
        std::string line;
        if (!std::getline(stream, line)) return false;
        const auto first_tab = line.find('\t');
        const auto second_tab = first_tab == std::string::npos ? std::string::npos : line.find('\t', first_tab + 1);
        if (first_tab == std::string::npos || second_tab == std::string::npos ||
            line.substr(0, first_tab) != "NPCSESSION" ||
            line.substr(first_tab + 1, second_tab - first_tab - 1) != "1") return false;
        std::size_t active = 0;
        const auto active_text = std::string_view{line}.substr(second_tab + 1);
        const auto active_result = std::from_chars(active_text.data(), active_text.data() + active_text.size(), active);
        if (active_result.ec != std::errc{} || active_result.ptr != active_text.data() + active_text.size()) return false;

        Session parsed;
        parsed.active_index = active;
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            std::vector<std::string> fields;
            std::istringstream row{line};
            std::string field;
            while (std::getline(row, field, '\t')) fields.push_back(field);
            if (!line.empty() && line.back() == '\t') fields.emplace_back();
            if (fields.size() != 8) return false;
            SessionEntry entry;
            const int encoding = std::stoi(fields[0]);
            const int ending = std::stoi(fields[1]);
            if (encoding < 0 || encoding > static_cast<int>(Encoding::ansi) ||
                ending < 0 || ending > static_cast<int>(LineEnding::cr)) return false;
            entry.encoding = static_cast<Encoding>(encoding);
            entry.line_ending = static_cast<LineEnding>(ending);
            entry.view.anchor = std::stoll(fields[2]);
            entry.view.caret = std::stoll(fields[3]);
            entry.view.first_visible_line = std::stoll(fields[4]);
            entry.dirty = fields[5] == "1";
            std::wstring path;
            if (!Unescape(fields[6], path) || !Unescape(fields[7], entry.recovery_text)) return false;
            entry.path = path;
            parsed.documents.push_back(std::move(entry));
        }
        if (!parsed.documents.empty() && parsed.active_index >= parsed.documents.size()) return false;
        session = std::move(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace notepad_colon
