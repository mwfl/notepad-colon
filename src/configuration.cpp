#include <notepad_colon/configuration.h>

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <cstdlib>
#include <sstream>

namespace notepad_colon {
namespace {
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
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                          result.data(), size);
    return result;
}
bool Number(std::string_view text, std::uint32_t& value) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
std::wstring Upper(std::wstring_view value) {
    std::wstring result(value);
    std::ranges::transform(result, result.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return result;
}
}  // namespace

std::wstring FormatShortcut(const ShortcutBinding& binding) {
    if (!binding.key) return L"";
    std::wstring result;
    if (binding.modifiers & FCONTROL) result += L"Ctrl+";
    if (binding.modifiers & FALT) result += L"Alt+";
    if (binding.modifiers & FSHIFT) result += L"Shift+";
    if (binding.key >= VK_F1 && binding.key <= VK_F24)
        result += L"F" + std::to_wstring(binding.key - VK_F1 + 1);
    else if ((binding.key >= 'A' && binding.key <= 'Z') ||
             (binding.key >= '0' && binding.key <= '9')) result += static_cast<wchar_t>(binding.key);
    else if (binding.key == VK_UP) result += L"Up";
    else if (binding.key == VK_DOWN) result += L"Down";
    else if (binding.key == VK_LEFT) result += L"Left";
    else if (binding.key == VK_RIGHT) result += L"Right";
    else if (binding.key == VK_OEM_PLUS) result += L"Plus";
    else if (binding.key == VK_OEM_MINUS) result += L"Minus";
    else if (binding.key == VK_OEM_2) result += L"Slash";
    else return L"Key" + std::to_wstring(binding.key);
    return result;
}

std::optional<ShortcutBinding> ParseShortcut(std::uint16_t command_id, std::wstring_view text) {
    if (text.empty()) return ShortcutBinding{command_id};
    std::uint8_t modifiers = FVIRTKEY;
    std::uint16_t key = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(L'+', start);
        const auto token = Upper(text.substr(start, end - start));
        if (token == L"CTRL" || token == L"CONTROL") modifiers |= FCONTROL;
        else if (token == L"ALT") modifiers |= FALT;
        else if (token == L"SHIFT") modifiers |= FSHIFT;
        else if (token.size() == 1 && ((token[0] >= L'A' && token[0] <= L'Z') ||
                                      (token[0] >= L'0' && token[0] <= L'9'))) key = static_cast<std::uint16_t>(token[0]);
        else if (token.size() >= 2 && token[0] == L'F') {
            wchar_t* parse_end{}; const auto number = std::wcstoul(token.c_str() + 1, &parse_end, 10);
            if (!parse_end || *parse_end || number < 1 || number > 24) return std::nullopt;
            key = static_cast<std::uint16_t>(VK_F1 + number - 1);
        } else if (token == L"UP") key = VK_UP; else if (token == L"DOWN") key = VK_DOWN;
        else if (token == L"LEFT") key = VK_LEFT; else if (token == L"RIGHT") key = VK_RIGHT;
        else if (token == L"PLUS") key = VK_OEM_PLUS; else if (token == L"MINUS") key = VK_OEM_MINUS;
        else if (token == L"SLASH") key = VK_OEM_2; else return std::nullopt;
        if (end == std::wstring_view::npos) break;
        start = end + 1;
    }
    if (!key) return std::nullopt;
    return ShortcutBinding{command_id, modifiers, key};
}

std::vector<std::pair<std::uint16_t, std::uint16_t>> FindShortcutConflicts(
    const std::vector<ShortcutBinding>& shortcuts) {
    std::vector<std::pair<std::uint16_t, std::uint16_t>> result;
    for (std::size_t i = 0; i < shortcuts.size(); ++i)
        for (std::size_t j = i + 1; j < shortcuts.size(); ++j)
            if (shortcuts[i].key && shortcuts[i].key == shortcuts[j].key &&
                shortcuts[i].modifiers == shortcuts[j].modifiers)
                result.emplace_back(shortcuts[i].command_id, shortcuts[j].command_id);
    return result;
}

std::string SerializeConfiguration(const Configuration& configuration) {
    const auto font = Utf8(configuration.preferences.font_name);
    std::ostringstream out;
    out << "NPCCONFIG\t1\n" << font.size() << '\n'; out.write(font.data(), static_cast<std::streamsize>(font.size())); out << '\n';
    const auto& p = configuration.preferences;
    out << p.font_size << '\t' << p.tab_width << '\t' << static_cast<unsigned>(p.theme) << '\t'
        << p.auto_save << '\t' << p.auto_save_seconds << '\t' << p.trim_trailing_whitespace_on_save << '\t'
        << p.create_backup_before_save << '\t' << p.ensure_final_newline << '\n';
    out << configuration.shortcuts.size() << '\n';
    for (const auto& shortcut : configuration.shortcuts)
        out << shortcut.command_id << '\t' << static_cast<unsigned>(shortcut.modifiers) << '\t' << shortcut.key << '\n';
    return out.str();
}

bool DeserializeConfiguration(std::string_view encoded, Configuration& configuration) {
    std::istringstream input{std::string(encoded)}; std::string line;
    if (!std::getline(input, line) || line != "NPCCONFIG\t1" || !std::getline(input, line)) return false;
    std::uint32_t font_size_bytes = 0; if (!Number(line, font_size_bytes) || font_size_bytes > 4096) return false;
    std::string font(font_size_bytes, '\0'); input.read(font.data(), font.size()); if (input.get() != '\n') return false;
    const auto wide_font = Wide(font); if (!wide_font || !std::getline(input, line)) return false;
    std::vector<std::uint32_t> fields; std::size_t start = 0;
    while (start <= line.size()) { const auto end = line.find('\t', start); std::uint32_t value{};
        if (!Number(std::string_view(line).substr(start, end - start), value)) return false;
        fields.push_back(value); if (end == std::string::npos) break; start = end + 1; }
    if (fields.size() != 8) return false;
    Preferences preferences{*wide_font, fields[0], fields[1], static_cast<ThemePreference>(fields[2]),
        fields[3] != 0, fields[4], fields[5] != 0, fields[6] != 0, fields[7] != 0};
    if (!ValidatePreferences(preferences) || !std::getline(input, line)) return false;
    std::uint32_t count{}; if (!Number(line, count) || count > 1000) return false;
    std::vector<ShortcutBinding> shortcuts;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!std::getline(input, line)) return false;
        std::uint32_t values[3]{}; start = 0;
        for (int field = 0; field < 3; ++field) { const auto end = field == 2 ? line.size() : line.find('\t', start);
            if (end == std::string::npos || !Number(std::string_view(line).substr(start, end - start), values[field])) return false;
            start = end + 1; }
        if (values[0] > UINT16_MAX || values[1] > UINT8_MAX || values[2] > UINT16_MAX) return false;
        shortcuts.push_back({static_cast<std::uint16_t>(values[0]), static_cast<std::uint8_t>(values[1]),
                             static_cast<std::uint16_t>(values[2])});
    }
    if (!FindShortcutConflicts(shortcuts).empty()) return false;
    configuration = {preferences, std::move(shortcuts)}; return true;
}
}  // namespace notepad_colon
