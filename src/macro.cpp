#include <notepad_colon/macro.h>

#include <windows.h>

#include <charconv>
#include <fstream>
#include <optional>
#include <sstream>

namespace notepad_colon {
namespace {
std::string Utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const auto size = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
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

bool ParseSize(std::string_view value, std::size_t& result) {
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}
}  // namespace

void MacroRecorder::Start() { actions_.clear(); recording_ = true; }
std::vector<MacroAction> MacroRecorder::Stop() { recording_ = false; return actions_; }
void MacroRecorder::Cancel() noexcept { recording_ = false; actions_.clear(); }

void MacroRecorder::RecordCommand(std::uint16_t command_id) {
    if (recording_) actions_.push_back({MacroActionKind::command, command_id});
}

void MacroRecorder::RecordText(std::wstring_view text) {
    if (!recording_ || text.empty()) return;
    if (!actions_.empty() && actions_.back().kind == MacroActionKind::insert_text)
        actions_.back().text += text;
    else actions_.push_back({MacroActionKind::insert_text, 0, std::wstring{text}});
}

void MacroRecorder::RecordDeleteBackward(std::size_t count) {
    if (!recording_ || !count) return;
    if (!actions_.empty() && actions_.back().kind == MacroActionKind::delete_backward)
        actions_.back().count += count;
    else actions_.push_back({MacroActionKind::delete_backward, 0, {}, count});
}

std::string SerializeMacros(const std::vector<SavedMacro>& macros) {
    std::ostringstream output;
    output << "NPCMACROS\t1\n" << macros.size() << '\n';
    for (const auto& macro : macros) {
        const auto name = Utf8(macro.name);
        output << name.size() << '\t' << macro.actions.size() << '\n';
        output.write(name.data(), static_cast<std::streamsize>(name.size())); output << '\n';
        for (const auto& action : macro.actions) {
            const auto text = Utf8(action.text);
            output << static_cast<unsigned>(action.kind) << '\t' << action.command_id << '\t'
                   << action.count << '\t' << text.size() << '\n';
            output.write(text.data(), static_cast<std::streamsize>(text.size())); output << '\n';
        }
    }
    return output.str();
}

bool DeserializeMacros(std::string_view encoded, std::vector<SavedMacro>& macros) {
    std::istringstream input{std::string(encoded)};
    std::string line;
    if (!std::getline(input, line) || line != "NPCMACROS\t1" || !std::getline(input, line)) return false;
    std::size_t macro_count = 0;
    if (!ParseSize(line, macro_count) || macro_count > 100) return false;
    std::vector<SavedMacro> parsed;
    for (std::size_t m = 0; m < macro_count; ++m) {
        if (!std::getline(input, line)) return false;
        const auto tab = line.find('\t');
        std::size_t name_size = 0, action_count = 0;
        if (tab == std::string::npos || !ParseSize(std::string_view(line).substr(0, tab), name_size) ||
            !ParseSize(std::string_view(line).substr(tab + 1), action_count) ||
            name_size > 4096 || action_count > 10000) return false;
        std::string name(name_size, '\0'); input.read(name.data(), static_cast<std::streamsize>(name_size));
        if (input.get() != '\n') return false;
        const auto wide_name = Wide(name); if (!wide_name) return false;
        SavedMacro macro{*wide_name, {}};
        for (std::size_t a = 0; a < action_count; ++a) {
            if (!std::getline(input, line)) return false;
            std::size_t fields[4]{}; std::size_t start = 0;
            for (std::size_t field = 0; field < 4; ++field) {
                const auto end = field == 3 ? line.size() : line.find('\t', start);
                if (end == std::string::npos || !ParseSize(std::string_view(line).substr(start, end - start), fields[field])) return false;
                start = end + 1;
            }
            if (fields[0] > static_cast<std::size_t>(MacroActionKind::delete_backward) ||
                fields[1] > UINT16_MAX || fields[3] > 1024 * 1024) return false;
            std::string text(fields[3], '\0'); input.read(text.data(), static_cast<std::streamsize>(text.size()));
            if (input.get() != '\n') return false;
            const auto wide_text = Wide(text); if (!wide_text) return false;
            macro.actions.push_back({static_cast<MacroActionKind>(fields[0]),
                                     static_cast<std::uint16_t>(fields[1]), *wide_text, fields[2]});
        }
        parsed.push_back(std::move(macro));
    }
    macros = std::move(parsed);
    return true;
}

bool SaveMacrosAtomic(const std::filesystem::path& path, const std::vector<SavedMacro>& macros) {
    std::error_code error; std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const auto temporary = path.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    const auto encoded = SerializeMacros(macros); output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    output.close();
    if (!output || !::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error); return false;
    }
    return true;
}

bool LoadMacros(const std::filesystem::path& path, std::vector<SavedMacro>& macros) {
    std::ifstream input(path, std::ios::binary); if (!input) return false;
    std::ostringstream encoded; encoded << input.rdbuf();
    return DeserializeMacros(encoded.str(), macros);
}
}  // namespace notepad_colon
