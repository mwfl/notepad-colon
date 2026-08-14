#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace notepad_colon {

enum class MacroActionKind : std::uint8_t { command, insert_text, delete_backward };

struct MacroAction {
    MacroActionKind kind = MacroActionKind::command;
    std::uint16_t command_id = 0;
    std::wstring text;
    std::size_t count = 0;
    friend bool operator==(const MacroAction&, const MacroAction&) = default;
};

struct SavedMacro {
    std::wstring name;
    std::vector<MacroAction> actions;
    friend bool operator==(const SavedMacro&, const SavedMacro&) = default;
};

class MacroRecorder final {
public:
    void Start();
    std::vector<MacroAction> Stop();
    void Cancel() noexcept;
    bool IsRecording() const noexcept { return recording_; }
    void RecordCommand(std::uint16_t command_id);
    void RecordText(std::wstring_view text);
    void RecordDeleteBackward(std::size_t count);
    const std::vector<MacroAction>& Actions() const noexcept { return actions_; }

private:
    bool recording_ = false;
    std::vector<MacroAction> actions_;
};

std::string SerializeMacros(const std::vector<SavedMacro>& macros);
bool DeserializeMacros(std::string_view encoded, std::vector<SavedMacro>& macros);
bool SaveMacrosAtomic(const std::filesystem::path& path,
                      const std::vector<SavedMacro>& macros);
bool LoadMacros(const std::filesystem::path& path, std::vector<SavedMacro>& macros);

}  // namespace notepad_colon
