#include <notepad_colon/shell_menu.h>

#include <array>

namespace notepad_colon {
namespace {
constexpr std::wstring_view kVerb = L"NotepadColon";
constexpr std::wstring_view kOwnerValue = L"NotepadColonOwner";
constexpr std::wstring_view kOwnerId = L"mwfl.notepad-colon";

struct Target {
    std::wstring_view class_path;  // Relative to the classes key.
    std::wstring_view argument;    // Placeholder Explorer substitutes.
    bool folder;
};

constexpr std::array kTargets{
    Target{L"*\\shell", L"%1", false},
    Target{L"Directory\\shell", L"%1", true},
    Target{L"Directory\\Background\\shell", L"%V", true}};

std::wstring VerbKey(std::wstring_view classes_key, const Target& target) {
    std::wstring key{classes_key};
    key += L"\\";
    key += target.class_path;
    key += L"\\";
    key += kVerb;
    return key;
}

bool SetString(HKEY key, const wchar_t* name, const std::wstring& value) noexcept {
    return ::RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

enum class Ownership { absent, owned, foreign };

Ownership QueryOwnership(HKEY root, const std::wstring& verb_key) noexcept {
    HKEY key{};
    if (::RegOpenKeyExW(root, verb_key.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return Ownership::absent;
    wchar_t owner[64]{};
    DWORD size = sizeof(owner) - sizeof(wchar_t);
    DWORD type = 0;
    const auto status = ::RegQueryValueExW(key, std::wstring{kOwnerValue}.c_str(), nullptr, &type,
                                           reinterpret_cast<BYTE*>(owner), &size);
    ::RegCloseKey(key);
    return status == ERROR_SUCCESS && type == REG_SZ && kOwnerId == owner ? Ownership::owned
                                                                         : Ownership::foreign;
}
}  // namespace

ShellMenuStatus RegisterShellMenus(HKEY root, std::wstring_view classes_key,
                                   const ShellMenuSpec& spec) noexcept {
    try {
        if (spec.executable.empty()) return ShellMenuStatus::failed;
        // Refuse before writing anything if another program owns one of the verbs.
        for (const auto& target : kTargets)
            if (QueryOwnership(root, VerbKey(classes_key, target)) == Ownership::foreign)
                return ShellMenuStatus::conflict;
        const auto executable = spec.executable.wstring();
        for (const auto& target : kTargets) {
            const auto verb_key = VerbKey(classes_key, target);
            HKEY verb{};
            if (::RegCreateKeyExW(root, verb_key.c_str(), 0, nullptr, 0, KEY_SET_VALUE | KEY_CREATE_SUB_KEY,
                                  nullptr, &verb, nullptr) != ERROR_SUCCESS)
                return ShellMenuStatus::failed;
            bool written = SetString(verb, nullptr, target.folder ? spec.folder_label : spec.file_label) &&
                           SetString(verb, L"Icon", L"\"" + executable + L"\",0") &&
                           SetString(verb, std::wstring{kOwnerValue}.c_str(), std::wstring{kOwnerId});
            HKEY command{};
            written = written && ::RegCreateKeyExW(verb, L"command", 0, nullptr, 0, KEY_SET_VALUE,
                                                   nullptr, &command, nullptr) == ERROR_SUCCESS;
            if (written) {
                written = SetString(command, nullptr,
                    L"\"" + executable + L"\" \"" + std::wstring{target.argument} + L"\"");
                ::RegCloseKey(command);
            }
            ::RegCloseKey(verb);
            if (!written) return ShellMenuStatus::failed;
        }
        return ShellMenuStatus::success;
    } catch (...) { return ShellMenuStatus::failed; }
}

ShellMenuStatus RemoveShellMenus(HKEY root, std::wstring_view classes_key) noexcept {
    try {
        auto result = ShellMenuStatus::success;
        for (const auto& target : kTargets) {
            const auto verb_key = VerbKey(classes_key, target);
            const auto ownership = QueryOwnership(root, verb_key);
            if (ownership == Ownership::absent) continue;
            if (ownership == Ownership::foreign) { result = ShellMenuStatus::conflict; continue; }
            if (::RegDeleteTreeW(root, verb_key.c_str()) != ERROR_SUCCESS)
                result = ShellMenuStatus::failed;
        }
        return result;
    } catch (...) { return ShellMenuStatus::failed; }
}

bool ShellMenusRegistered(HKEY root, std::wstring_view classes_key) noexcept {
    try {
        for (const auto& target : kTargets)
            if (QueryOwnership(root, VerbKey(classes_key, target)) != Ownership::owned) return false;
        return true;
    } catch (...) { return false; }
}

}  // namespace notepad_colon
