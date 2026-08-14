#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace notepad_colon {

enum class ThemePreference : std::uint32_t { system, light, dark };

struct Preferences {
    std::wstring font_name = L"Consolas";
    std::uint32_t font_size = 11;
    std::uint32_t tab_width = 4;
    ThemePreference theme = ThemePreference::system;
    bool auto_save = false;
    std::uint32_t auto_save_seconds = 30;
    bool trim_trailing_whitespace_on_save = false;
    bool create_backup_before_save = true;
    bool ensure_final_newline = false;
    friend bool operator==(const Preferences&, const Preferences&) = default;
};

bool ValidatePreferences(const Preferences& preferences) noexcept;
Preferences SanitizePreferences(Preferences preferences) noexcept;

}  // namespace notepad_colon
