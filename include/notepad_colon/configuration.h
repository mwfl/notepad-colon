#pragma once

#include <notepad_colon/preferences.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace notepad_colon {

struct ShortcutBinding {
    std::uint16_t command_id = 0;
    std::uint8_t modifiers = 0;
    std::uint16_t key = 0;
    friend bool operator==(const ShortcutBinding&, const ShortcutBinding&) = default;
};

struct Configuration {
    Preferences preferences;
    std::vector<ShortcutBinding> shortcuts;
};

std::wstring FormatShortcut(const ShortcutBinding& binding);
std::optional<ShortcutBinding> ParseShortcut(std::uint16_t command_id,
                                              std::wstring_view text);
std::vector<std::pair<std::uint16_t, std::uint16_t>> FindShortcutConflicts(
    const std::vector<ShortcutBinding>& shortcuts);
std::string SerializeConfiguration(const Configuration& configuration);
bool DeserializeConfiguration(std::string_view encoded, Configuration& configuration);

}  // namespace notepad_colon
