#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace notepad_colon {

class WorkspaceCatalog final {
public:
    bool AddRoot(const std::filesystem::path& root);
    bool RemoveRoot(const std::filesystem::path& root);
    void AddRecent(const std::filesystem::path& root, std::size_t maximum = 12);
    void SetFavorite(const std::filesystem::path& root, bool favorite);
    const std::vector<std::filesystem::path>& Roots() const noexcept { return roots_; }
    const std::vector<std::filesystem::path>& Recent() const noexcept { return recent_; }
    const std::vector<std::filesystem::path>& Favorites() const noexcept { return favorites_; }

private:
    std::vector<std::filesystem::path> roots_, recent_, favorites_;
};

bool IsWithinWorkspaceRoots(const std::filesystem::path& path,
                            const std::vector<std::filesystem::path>& roots) noexcept;
bool IsValidWorkspaceName(std::wstring_view name) noexcept;
bool CreateWorkspaceItem(const std::filesystem::path& parent, std::wstring_view name,
                         bool directory, const std::vector<std::filesystem::path>& roots);
bool RenameWorkspaceItem(const std::filesystem::path& source, std::wstring_view new_name,
                         const std::vector<std::filesystem::path>& roots);
bool RecycleWorkspaceItem(const std::filesystem::path& path,
                          const std::vector<std::filesystem::path>& roots) noexcept;
std::string SerializeWorkspaceCatalog(const WorkspaceCatalog& catalog);
bool DeserializeWorkspaceCatalog(std::string_view encoded, WorkspaceCatalog& catalog);
bool SaveWorkspaceCatalogAtomic(const std::filesystem::path& path,
                                const WorkspaceCatalog& catalog) noexcept;
bool LoadWorkspaceCatalog(const std::filesystem::path& path,
                          WorkspaceCatalog& catalog) noexcept;

}  // namespace notepad_colon
