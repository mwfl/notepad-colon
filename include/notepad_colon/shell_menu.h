#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace notepad_colon {

// Explorer context-menu entries ("Edit with Notepad::" on files, "Open folder
// in Notepad::" on folders and folder backgrounds). They are per-user,
// explicit and reversible: only keys carrying this application's owner marker
// are ever replaced or removed.
struct ShellMenuSpec {
    std::filesystem::path executable;
    std::wstring file_label = L"Edit with Notepad::";
    std::wstring folder_label = L"Open folder in Notepad::";
};

enum class ShellMenuStatus { success, conflict, failed };

// `classes_key` is the registry path that plays the role of
// HKCU\Software\Classes; tests pass an isolated key.
ShellMenuStatus RegisterShellMenus(HKEY root, std::wstring_view classes_key,
                                   const ShellMenuSpec& spec) noexcept;
ShellMenuStatus RemoveShellMenus(HKEY root, std::wstring_view classes_key) noexcept;
bool ShellMenusRegistered(HKEY root, std::wstring_view classes_key) noexcept;

constexpr std::wstring_view kUserClassesKey = L"Software\\Classes";

}  // namespace notepad_colon
