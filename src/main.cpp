#include <mwfl/mwfl.h>
#include <mwfl/scintilla.h>

#include <filesystem>
#include <stdexcept>
#include <string_view>

using mwfl::operator""_dip;

namespace {
constexpr mwfl::ControlId kOpen{100};
constexpr mwfl::ControlId kSave{101};
constexpr mwfl::ControlId kUndo{102};
constexpr mwfl::ControlId kRedo{103};
constexpr mwfl::ControlId kFind{104};
constexpr mwfl::ControlId kReplace{105};
constexpr mwfl::ControlId kSearch{106};
constexpr mwfl::ControlId kReplacement{107};
constexpr mwfl::ControlId kEditor{108};
constexpr UINT kSelfTestMessage = WM_APP + 0x240;

class MainWindow final : public mwfl::WindowBase {
public:
    void BuildUI() override {
        SetTitle(L"Untitled — Notepad Colon");
        if (!runtime_.LoadAdjacent())
            throw std::runtime_error("Scintilla.dll is not available beside Notepad Colon");

        mwfl::ControlHost ui{*this};
        ui.Add(open_, kOpen, L"Open...");
        ui.Add(save_, kSave, L"Save");
        ui.Add(undo_, kUndo, L"Undo");
        ui.Add(redo_, kRedo, L"Redo");
        ui.Add(search_, kSearch, L"");
        ui.Add(find_, kFind, L"Find");
        ui.Add(replacement_, kReplacement, L"");
        ui.Add(replace_, kReplace, L"Replace");
        ui.AddNative(editor_, kEditor, mwfl::RectDip{}, runtime_);
        ui.Add(status_, L"Ln 1, Col 1 | UTF-8 | CRLF | Plain Text");

        mwfl::Must(editor_.ConfigureCodeEditing(), "configure editor");
        mwfl::Must(mwfl::SetAccessibleName(editor_.GetHwnd(), L"Document editor"), "name editor");
        mwfl::Must(mwfl::SetAccessibleName(search_.GetHwnd(), L"Find text"), "name search");
        mwfl::Must(mwfl::SetAccessibleName(replacement_.GetHwnd(), L"Replacement text"), "name replacement");
        mwfl::Must(mwfl::SetAccessibleName(status_.GetHwnd(), L"Document status"), "name status");

        SetLayout(mwfl::Column().Gap(5.0_dip).Margin(7.0_dip)
            .Add(mwfl::Row().Gap(5.0_dip)
                .Add(open_, mwfl::Fixed(82.0_dip)).Add(save_, mwfl::Fixed(68.0_dip))
                .Add(undo_, mwfl::Fixed(68.0_dip)).Add(redo_, mwfl::Fixed(68.0_dip)), mwfl::Fixed(30.0_dip))
            .Add(mwfl::Row().Gap(5.0_dip)
                .Add(search_, mwfl::Stretch()).Add(find_, mwfl::Fixed(68.0_dip))
                .Add(replacement_, mwfl::Stretch()).Add(replace_, mwfl::Fixed(78.0_dip)), mwfl::Fixed(30.0_dip))
            .Add(editor_, mwfl::Stretch()).Add(status_, mwfl::Auto()));

        editor_.SetSavePoint();
        editor_.Focus();
        if (IsSelfTest() && !::PostMessageW(GetHwnd(), kSelfTestMessage, 0, 0))
            throw std::runtime_error("could not schedule GUI self-test");
    }

    mwfl::EventResult OnCommand(const mwfl::CommandEvent& event) override {
        if (event.IsClicked(open_)) OpenInteractive();
        else if (event.IsClicked(save_)) static_cast<void>(SaveInteractive());
        else if (event.IsClicked(undo_)) editor_.Undo();
        else if (event.IsClicked(redo_)) editor_.Redo();
        else if (event.IsClicked(find_)) FindNext();
        else if (event.IsClicked(replace_)) ReplaceNext();
        else return mwfl::EventResult::Propagate();
        return mwfl::EventResult::Handled();
    }

    mwfl::EventResult OnNotify(const mwfl::NotifyEvent& event) override {
        if (!event.IsFrom(editor_)) return mwfl::EventResult::Propagate();
        const auto notification = editor_.DecodeNotification(event.header);
        if (!notification) return mwfl::EventResult::Propagate();
        if (notification->kind == mwfl::ScintillaNotificationKind::save_point_left) {
            dirty_ = true;
            SyncTitle();
        } else if (notification->kind == mwfl::ScintillaNotificationKind::save_point_reached) {
            dirty_ = false;
            SyncTitle();
        } else if (notification->kind == mwfl::ScintillaNotificationKind::modified &&
                   notification->lines_added != 0) {
            static_cast<void>(editor_.UpdateLineNumberMargin());
        }
        return mwfl::EventResult::Propagate();
    }

    mwfl::EventResult OnMessage(const mwfl::WindowMessage& event) override {
        if (event.id != kSelfTestMessage) return mwfl::EventResult::Propagate();
        RunSelfTest();
        return mwfl::EventResult::Handled();
    }

    mwfl::EventResult OnClose() override {
        if (IsSelfTest() || !dirty_) return mwfl::EventResult::Propagate();
        const int answer = ::MessageBoxW(GetHwnd(), L"Discard unsaved changes?", L"Notepad Colon",
                                         MB_ICONWARNING | MB_YESNO);
        return answer == IDYES ? mwfl::EventResult::Propagate() : mwfl::EventResult::Handled();
    }

private:
    static bool IsSelfTest() noexcept {
        return std::wstring_view{::GetCommandLineW()}.find(L"--self-test") != std::wstring_view::npos;
    }

    bool OpenPath(const std::filesystem::path& path) {
        const auto loaded = mwfl::ReadTextFile(path);
        if (!loaded.Succeeded() || !editor_.SetText(loaded.value->text)) return false;
        path_ = path;
        encoding_ = loaded.value->encoding;
        file_stamp_ = loaded.value->stamp;
        editor_.SetSavePoint();
        SyncTitle();
        return true;
    }

    bool SavePath(const std::filesystem::path& path) {
        const auto text = editor_.GetText();
        if (!text) return false;
        const auto expected = path == path_ ? std::optional<mwfl::FileStamp>{file_stamp_} : std::nullopt;
        const auto saved = mwfl::WriteTextFileAtomic(path, *text, encoding_, expected);
        if (!saved.Succeeded()) return false;
        path_ = path;
        if (!saved.stamp) return false;
        file_stamp_ = *saved.stamp;
        editor_.SetSavePoint();
        SyncTitle();
        return true;
    }

    void OpenInteractive() {
        if (dirty_ && ::MessageBoxW(GetHwnd(), L"Discard changes and open another file?",
                                    L"Notepad Colon", MB_ICONWARNING | MB_YESNO) != IDYES) return;
        const auto selected = mwfl::ShowOpenFileDialog({.owner = GetHwnd(), .title = L"Open",
            .filters = {{L"Text and source files", L"*.txt;*.md;*.cpp;*.h;*.json;*.xml;*.ini;*.yaml;*.yml;*.ps1"},
                        {L"All files", L"*.*"}}});
        if (selected.accepted && !OpenPath(selected.path)) status_.SetText(L"Open failed");
    }

    bool SaveInteractive() {
        auto target = path_;
        if (target.empty()) {
            const auto selected = mwfl::ShowSaveFileDialog({.owner = GetHwnd(), .title = L"Save",
                .filters = {{L"Text files", L"*.txt"}, {L"All files", L"*.*"}}, .default_extension = L"txt"});
            if (!selected.accepted) return false;
            target = selected.path;
        }
        if (!SavePath(target)) status_.SetText(L"Save failed — the file may have changed on disk");
        return !dirty_;
    }

    std::optional<mwfl::ScintillaTextRange> FindNext() {
        const auto query = search_.GetText();
        if (query.empty()) return std::nullopt;
        const auto selection = editor_.GetSelection();
        auto match = editor_.Find(query, selection.end);
        if (!match) match = editor_.Find(query);
        if (match) editor_.SetSelection(*match);
        status_.SetText(match ? L"Match selected" : L"No matches");
        return match;
    }

    void ReplaceNext() {
        const auto match = FindNext();
        if (!match || !editor_.ReplaceTarget(replacement_.GetText())) return;
        status_.SetText(L"Replaced");
    }

    void SyncTitle() {
        const auto name = path_.empty() ? L"Untitled" : path_.filename().wstring();
        SetTitle(name + (dirty_ ? L" * — Notepad Colon" : L" — Notepad Colon"));
    }

    void RunSelfTest() noexcept {
        int result = 0;
        const auto file = std::filesystem::temp_directory_path() /
            (L"notepad-colon-" + std::to_wstring(::GetCurrentProcessId()) + L".txt");
        try {
            if (!mwfl::WriteTextFileAtomic(file, L"hello 世界\n", mwfl::TextEncoding::utf8).Succeeded()) result = 1;
            if (result == 0 && !OpenPath(file)) result = 2;
            search_.SetText(L"世界");
            replacement_.SetText(L"mwfl");
            ReplaceNext();
            if (result == 0 && !editor_.IsModified()) result = 3;
            if (result == 0 && !SavePath(file)) result = 4;
            const auto saved = mwfl::ReadTextFile(file);
            if (result == 0 && (!saved.Succeeded() || saved.value->text != L"hello mwfl\n")) result = 5;
        } catch (...) {
            result = 9;
        }
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        ::PostQuitMessage(result);
    }

    mwfl::ScintillaRuntime runtime_;
    mwfl::ScintillaEditor editor_;
    mwfl::Button open_, save_, undo_, redo_, find_, replace_;
    mwfl::TextBox search_, replacement_;
    mwfl::Label status_;
    std::filesystem::path path_;
    mwfl::TextEncoding encoding_ = mwfl::TextEncoding::utf8;
    mwfl::FileStamp file_stamp_{};
    bool dirty_ = false;
};
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    return mwfl::RunApplication<MainWindow>(instance, show_command, {},
                                             {.com_apartment = mwfl::ComApartment::sta});
}
