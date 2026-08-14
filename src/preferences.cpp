#include <notepad_colon/preferences.h>

namespace notepad_colon {

bool ValidatePreferences(const Preferences& preferences) noexcept {
    return !preferences.font_name.empty() && preferences.font_name.size() <= LF_FACESIZE - 1 &&
           preferences.font_size >= 8 && preferences.font_size <= 40 &&
           preferences.tab_width >= 1 && preferences.tab_width <= 16 &&
           static_cast<std::uint32_t>(preferences.theme) <=
               static_cast<std::uint32_t>(ThemePreference::dark);
}

Preferences SanitizePreferences(Preferences preferences) noexcept {
    const Preferences defaults;
    if (preferences.font_name.empty() || preferences.font_name.size() > LF_FACESIZE - 1)
        preferences.font_name = defaults.font_name;
    if (preferences.font_size < 8 || preferences.font_size > 40)
        preferences.font_size = defaults.font_size;
    if (preferences.tab_width < 1 || preferences.tab_width > 16)
        preferences.tab_width = defaults.tab_width;
    if (static_cast<std::uint32_t>(preferences.theme) >
        static_cast<std::uint32_t>(ThemePreference::dark))
        preferences.theme = defaults.theme;
    return preferences;
}

}  // namespace notepad_colon
