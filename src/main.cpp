#include "app/main_window.h"

using notepad_colon::app::MainWindow;
using mwfl::operator""_dip;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    notepad_colon::app::EnableNativeDarkMenuSupport();
    int count{};
    wchar_t** arguments = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    bool self_test = false;
    bool large_file_self_test = false;
    bool activation_test_server = false;
    bool activation_test_client = false;
    std::vector<std::filesystem::path> paths;
    for (int index = 1; arguments && index < count; ++index) {
        const std::wstring_view argument{arguments[index]};
        if (argument == L"--self-test") self_test = true;
        else if (argument == L"--large-file-self-test") large_file_self_test = true;
        else if (argument == L"--activation-test-server") activation_test_server = true;
        else if (argument == L"--activation-test-client") activation_test_client = true;
        else if (!argument.starts_with(L"--")) {
            std::error_code error;
            auto absolute = std::filesystem::absolute(argument, error);
            paths.push_back(error ? std::filesystem::path{argument} : std::move(absolute));
        }
    }
    if (arguments) ::LocalFree(arguments);
    const std::wstring instance_id = (self_test || large_file_self_test)
        ? L"mwfl.notepad-colon.self-test." + std::to_wstring(::GetCurrentProcessId())
        : (activation_test_server || activation_test_client)
            ? L"mwfl.notepad-colon.activation-test" : L"mwfl.notepad-colon.v1";
    mwfl::SingleInstance single_instance{instance_id};
    if (!single_instance.IsPrimary()) {
        std::wstring payload;
        for (const auto& path : paths) {
            if (!payload.empty()) payload.push_back(L'\n');
            payload.append(path.wstring());
        }
        const auto result = single_instance.ForwardActivation(payload);
        if (!result.Delivered()) {
            if (activation_test_client) return 2;
            ::MessageBoxW(nullptr, L"The existing Notepad:: instance did not respond.",
                          L"Notepad::", MB_OK | MB_ICONERROR);
            return 2;
        }
        return 0;
    }
    if (activation_test_client) return 3;
    const auto large_icon = reinterpret_cast<HICON>(::LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    const auto small_icon = reinterpret_cast<HICON>(::LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    const int result = mwfl::RunApplication<MainWindow>(instance,
        (self_test || large_file_self_test) ? SW_HIDE : show_command,
        {.title = L"Notepad::", .initial_bounds = {{}, {900.0_dip, 650.0_dip}},
         .use_default_bounds = false, .icon = large_icon, .small_icon = small_icon},
        {.com_apartment = mwfl::ComApartment::sta},
         single_instance, std::move(paths), self_test, activation_test_server,
         large_file_self_test);
    if (small_icon) ::DestroyIcon(small_icon);
    if (large_icon) ::DestroyIcon(large_icon);
    return result;
}
