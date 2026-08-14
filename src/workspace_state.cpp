#include <notepad_colon/workspace_state.h>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>

namespace notepad_colon {
namespace {
bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    const auto a = std::filesystem::absolute(left).lexically_normal().wstring();
    const auto b = std::filesystem::absolute(right).lexically_normal().wstring();
    return ::CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                  b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
bool Contains(const std::vector<std::filesystem::path>& values, const std::filesystem::path& path) {
    return std::ranges::any_of(values, [&](const auto& value) { return SamePath(value, path); });
}
std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.wstring();
    const auto size = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                          result.data(), size, nullptr, nullptr);
    return result;
}
std::optional<std::filesystem::path> PathFromUtf8(std::string_view value) {
    if (value.empty()) return std::filesystem::path{};
    const auto size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return std::nullopt;
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                          wide.data(), size);
    return std::filesystem::path{wide};
}
}  // namespace

bool WorkspaceCatalog::AddRoot(const std::filesystem::path& root) {
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error || Contains(roots_, root)) return false;
    roots_.push_back(std::filesystem::absolute(root).lexically_normal()); AddRecent(root); return true;
}
bool WorkspaceCatalog::RemoveRoot(const std::filesystem::path& root) {
    const auto before = roots_.size();
    std::erase_if(roots_, [&](const auto& value) { return SamePath(value, root); });
    return roots_.size() != before;
}
void WorkspaceCatalog::AddRecent(const std::filesystem::path& root, std::size_t maximum) {
    std::erase_if(recent_, [&](const auto& value) { return SamePath(value, root); });
    recent_.insert(recent_.begin(), std::filesystem::absolute(root).lexically_normal());
    if (recent_.size() > maximum) recent_.resize(maximum);
}
void WorkspaceCatalog::SetFavorite(const std::filesystem::path& root, bool favorite) {
    std::erase_if(favorites_, [&](const auto& value) { return SamePath(value, root); });
    if (favorite) favorites_.push_back(std::filesystem::absolute(root).lexically_normal());
}

bool IsWithinWorkspaceRoots(const std::filesystem::path& path,
                            const std::vector<std::filesystem::path>& roots) noexcept {
    try {
        const auto candidate = std::filesystem::absolute(path).lexically_normal().wstring();
        for (const auto& root_path : roots) {
            auto root = std::filesystem::absolute(root_path).lexically_normal().wstring();
            while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
            if (candidate.size() < root.size() || ::CompareStringOrdinal(candidate.c_str(),
                static_cast<int>(root.size()), root.c_str(), static_cast<int>(root.size()), TRUE) != CSTR_EQUAL) continue;
            if (candidate.size() == root.size() || candidate[root.size()] == L'\\' || candidate[root.size()] == L'/') return true;
        }
    } catch (...) {}
    return false;
}

bool IsValidWorkspaceName(std::wstring_view name) noexcept {
    if (name.empty() || name == L"." || name == L".." || name.back() == L' ' || name.back() == L'.') return false;
    constexpr std::wstring_view invalid = L"<>:\"/\\|?*";
    return name.find_first_of(invalid) == std::wstring_view::npos &&
           std::ranges::none_of(name, [](wchar_t c) { return c < 32; });
}

bool CreateWorkspaceItem(const std::filesystem::path& parent, std::wstring_view name,
                         bool directory, const std::vector<std::filesystem::path>& roots) {
    if (!IsValidWorkspaceName(name) || !IsWithinWorkspaceRoots(parent, roots)) return false;
    const auto target = parent / name;
    if (!IsWithinWorkspaceRoots(target, roots) || std::filesystem::exists(target)) return false;
    std::error_code error;
    if (directory) return std::filesystem::create_directory(target, error) && !error;
    std::ofstream output(target, std::ios::binary | std::ios::app);
    return static_cast<bool>(output);
}

bool RenameWorkspaceItem(const std::filesystem::path& source, std::wstring_view new_name,
                         const std::vector<std::filesystem::path>& roots) {
    if (!IsValidWorkspaceName(new_name) || !IsWithinWorkspaceRoots(source, roots)) return false;
    const auto target = source.parent_path() / new_name;
    if (!IsWithinWorkspaceRoots(target, roots) || std::filesystem::exists(target)) return false;
    std::error_code error; std::filesystem::rename(source, target, error); return !error;
}

bool RecycleWorkspaceItem(const std::filesystem::path& path,
                          const std::vector<std::filesystem::path>& roots) noexcept {
    if (!IsWithinWorkspaceRoots(path, roots)) return false;
    try {
        std::wstring from = std::filesystem::absolute(path).wstring(); from.push_back(L'\0'); from.push_back(L'\0');
        SHFILEOPSTRUCTW operation{}; operation.wFunc = FO_DELETE; operation.pFrom = from.c_str();
        operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        return ::SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
    } catch (...) { return false; }
}

std::string SerializeWorkspaceCatalog(const WorkspaceCatalog& catalog) {
    std::ostringstream out; out << "NPCWORKSPACE\t1\n";
    const auto write = [&](char kind, const auto& values) { for (const auto& path : values) {
        const auto bytes = Utf8(path); out << kind << '\t' << bytes.size() << '\n';
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); out << '\n'; }};
    write('R', catalog.Roots()); write('H', catalog.Recent()); write('F', catalog.Favorites()); return out.str();
}

bool DeserializeWorkspaceCatalog(std::string_view encoded, WorkspaceCatalog& catalog) {
    std::istringstream input{std::string(encoded)}; std::string line;
    if (!std::getline(input, line) || line != "NPCWORKSPACE\t1") return false;
    WorkspaceCatalog parsed;
    while (std::getline(input, line)) {
        if (line.size() < 3 || line[1] != '\t') return false;
        std::size_t size = 0; try { size = std::stoull(line.substr(2)); } catch (...) { return false; }
        if (size > 32768) return false;
        std::string bytes(size, '\0'); input.read(bytes.data(), static_cast<std::streamsize>(size));
        if (input.get() != '\n') return false;
        const auto path = PathFromUtf8(bytes); if (!path) return false;
        if (line[0] == 'R') static_cast<void>(parsed.AddRoot(*path));
        else if (line[0] == 'H') parsed.AddRecent(*path);
        else if (line[0] == 'F') parsed.SetFavorite(*path, true);
        else return false;
    }
    catalog = std::move(parsed); return true;
}
}  // namespace notepad_colon
