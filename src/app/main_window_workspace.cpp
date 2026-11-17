#include "main_window.h"

namespace notepad_colon::app {

void MainWindow::RefreshGitChangeMarkers() {
    auto* document = ActiveDocument();
    const auto* metadata = document ? workspace_.Find(document->id) : nullptr;
    if (!document || !metadata || metadata->path.empty()) {
        status_.SetText(Tr(L"Save the document before reading Git changes")); return;
    }
    const auto changes = notepad_colon::QueryGitChangedLines(metadata->path);
    document->editor->Send(SCI_MARKERDEFINE, 3, SC_MARK_LEFTRECT);
    document->editor->Send(SCI_MARKERSETBACK, 3, RGB(70, 180, 90));
    document->editor->Send(SCI_MARKERDELETEALL, 3);
    for (const auto line : changes.added_or_modified)
        if (line > 0) document->editor->Send(SCI_MARKERADD, line - 1, 3);
    status_.SetText(changes.repository
        ? Tr(L"Git markers: ") + std::to_wstring(changes.added_or_modified.size()) + Tr(L" changed line(s)")
        : Tr(L"Git is unavailable or the file is not in a repository"));
}

void MainWindow::OpenWorkspaceTerminal() {
    auto directory = workspace_root_;
    if (const auto* document = ActiveDocument())
        if (const auto* metadata = workspace_.Find(document->id); metadata && !metadata->path.empty())
            directory = metadata->path.parent_path();
    if (directory.empty()) {
        status_.SetText(Tr(L"Open a folder or save the active document before opening a terminal"));
        if (!IsTestMode())
            ::MessageBoxW(GetHwnd(),
                Tr(L"Open a folder or save the active document first.").c_str(),
                Tr(L"Open Terminal Here").c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    const auto path = directory.wstring();
    auto parameters = L"-d \"" + path + L"\"";
    SHELLEXECUTEINFOW shell{sizeof(shell)};
    shell.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    shell.hwnd = GetHwnd();
    shell.lpVerb = L"open";
    shell.lpFile = L"wt.exe";
    shell.lpParameters = parameters.c_str();
    shell.lpDirectory = path.c_str();
    shell.nShow = SW_SHOWNORMAL;
    if (::ShellExecuteExW(&shell) != FALSE) {
        if (shell.hProcess) ::CloseHandle(shell.hProcess);
        status_.SetText(Tr(L"Terminal opened at ") + path);
        return;
    }

    const auto executable = CommandProcessorPath();
    if (executable.empty()) {
        status_.SetText(Tr(L"Could not locate a terminal executable"));
        return;
    }
    auto command = L"\"" + executable.wstring() + L"\" /K cd /d \"" + path + L"\"";
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                         CREATE_NEW_CONSOLE, nullptr, path.c_str(), &startup, &process)) {
        ::CloseHandle(process.hThread); ::CloseHandle(process.hProcess);
        status_.SetText(Tr(L"Command Prompt opened at ") + path);
    } else {
        status_.SetText(Tr(L"Could not open terminal (Windows error ") +
                        std::to_wstring(::GetLastError()) + L")");
    }
}

std::optional<std::filesystem::path> MainWindow::SelectedWorkspacePath() const {
    const auto item = TreeView_GetSelection(tree_.GetHwnd());
    if (!item) return std::nullopt;
    TVITEMW details{};
    details.mask = TVIF_PARAM;
    details.hItem = item;
    if (!TreeView_GetItem(tree_.GetHwnd(), &details)) return std::nullopt;
    const auto found = tree_paths_.find(static_cast<std::uint64_t>(details.lParam));
    return found == tree_paths_.end() ? std::nullopt
                                     : std::optional<std::filesystem::path>{found->second};
}

std::optional<std::wstring> MainWindow::PromptWorkspaceName(std::wstring_view title,
                                                 std::wstring_view initial,
                                                 bool validate_name) {
    mwfl::TextBox name; mwfl::Button accept, cancel; std::optional<std::wstring> result;
    mwfl::Dialog* pointer = nullptr;
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = std::wstring(title),
        .initial_client_size = {400.0_dip, 104.0_dip}, .resizable = false,
        .callbacks = {
            .initialize = [&](HWND window) { DarkenDialog(window);
                mwfl::ControlHost ui{window}; ui.Add(name, {731}, std::wstring(initial));
                ui.Add(accept, {IDOK}, Tr(L"OK")); ui.Add(cancel, {IDCANCEL}, Tr(L"Cancel"));
                mwfl::SetAccessibleName(name.GetHwnd(), Tr(L"Workspace item name").c_str());
                return pointer->SetLayout(mwfl::Column().Margin(10.0_dip).Gap(8.0_dip)
                    .Add(name, mwfl::Fixed(28.0_dip))
                    .Add(mwfl::Row().Gap(6.0_dip).Add(mwfl::Column(), mwfl::Stretch())
                        .Add(accept, mwfl::Fixed(76.0_dip)).Add(cancel, mwfl::Fixed(76.0_dip)),
                        mwfl::Fixed(28.0_dip)));
            },
            .command = [&](HWND, WORD id, WORD) {
                if (id != IDOK) return false;
                const auto value = name.GetText();
                if (validate_name && !notepad_colon::IsValidWorkspaceName(value)) {
                    ::MessageBoxW(pointer->GetHwnd(), Tr(L"Enter a valid Windows file or folder name.").c_str(),
                                  Tr(L"Invalid name").c_str(), MB_OK | MB_ICONWARNING);
                    return true;
                }
                result = value; pointer->Accept(); return true;
            }}});
    pointer = &dialog; static_cast<void>(dialog.ShowModal()); return result;
}

void MainWindow::SaveWorkspaceCatalog() {
    if (!workspace_catalog_path_.empty())
        static_cast<void>(notepad_colon::SaveWorkspaceCatalogAtomic(
            workspace_catalog_path_, workspace_catalog_));
}

void MainWindow::ToggleFavoriteWorkspace() {
    if (workspace_root_.empty()) { status_.SetText(Tr(L"Open a workspace first")); return; }
    const bool favorite = !std::ranges::any_of(workspace_catalog_.Favorites(),
        [&](const auto& path) { return SamePath(path, workspace_root_); });
    workspace_catalog_.SetFavorite(workspace_root_, favorite); SaveWorkspaceCatalog();
    status_.SetText(favorite ? Tr(L"Workspace added to favorites") : Tr(L"Workspace removed from favorites"));
}

void MainWindow::ShowWorkspaceManager() {
    std::vector<std::filesystem::path> paths;
    for (const auto& path : workspace_catalog_.Favorites()) paths.push_back(path);
    for (const auto& path : workspace_catalog_.Recent())
        if (!std::ranges::any_of(paths, [&](const auto& existing) { return SamePath(existing, path); }))
            paths.push_back(path);
    mwfl::ListBox list; mwfl::Button open, favorite, close; mwfl::Dialog* pointer = nullptr;
    mwfl::Dialog dialog({.owner = GetHwnd(), .title = Tr(L"Recent and Favorite Workspaces"),
        .initial_client_size = {620.0_dip, 330.0_dip}, .resizable = true,
        .callbacks = {
            .initialize = [&](HWND window) { DarkenDialog(window);
                mwfl::ControlHost ui{window}; ui.Add(list, {781}, {});
                ui.Add(open, {782}, Tr(L"Open")); ui.Add(favorite, {783}, Tr(L"Favorite / Unfavorite"));
                ui.Add(close, {IDCANCEL}, Tr(L"Close"));
                for (const auto& path : paths) {
                    const bool starred = std::ranges::any_of(workspace_catalog_.Favorites(),
                        [&](const auto& item) { return SamePath(item, path); });
                    list.AddItem((starred ? L"★  " : L"   ") + path.wstring());
                }
                if (!paths.empty()) list.SetSelection(0);
                return pointer->SetLayout(mwfl::Column().Margin(8.0_dip).Gap(6.0_dip)
                    .Add(list, mwfl::Stretch())
                    .Add(mwfl::Row().Gap(6.0_dip).Add(open, mwfl::Fixed(80.0_dip))
                        .Add(favorite, mwfl::Fixed(150.0_dip)).Add(mwfl::Column(), mwfl::Stretch())
                        .Add(close, mwfl::Fixed(80.0_dip)), mwfl::Fixed(28.0_dip)));
            },
            .command = [&](HWND, WORD id, WORD) {
                const auto index = list.GetSelection();
                if (index == LB_ERR || static_cast<std::size_t>(index) >= paths.size()) return false;
                const auto path = paths[static_cast<std::size_t>(index)];
                if (id == 782) {
                    if (std::filesystem::is_directory(path)) { pointer->Accept(); StartWorkspaceScan(path); }
                    else ::MessageBoxW(pointer->GetHwnd(), Tr(L"This workspace folder no longer exists.").c_str(),
                                       Tr(L"Workspace").c_str(), MB_OK | MB_ICONWARNING);
                    return true;
                }
                if (id == 783) {
                    const bool is_favorite = std::ranges::any_of(workspace_catalog_.Favorites(),
                        [&](const auto& item) { return SamePath(item, path); });
                    workspace_catalog_.SetFavorite(path, !is_favorite); SaveWorkspaceCatalog();
                    pointer->Accept(); return true;
                }
                return false;
            }}});
    pointer = &dialog; static_cast<void>(dialog.ShowModal());
}

void MainWindow::CreateWorkspaceSelection(bool directory) {
    auto selected = SelectedWorkspacePath();
    if (!selected) { status_.SetText(Tr(L"Select a workspace folder first")); return; }
    std::error_code error;
    auto parent = std::filesystem::is_directory(*selected, error) ? *selected : selected->parent_path();
    const auto name = PromptWorkspaceName(directory ? Tr(L"New Folder") : Tr(L"New File"));
    if (!name) return;
    if (!notepad_colon::CreateWorkspaceItem(parent, *name, directory, workspace_catalog_.Roots())) {
        ::MessageBoxW(GetHwnd(), Tr(L"The item could not be created safely.").c_str(), Tr(L"Workspace").c_str(),
                      MB_OK | MB_ICONERROR); return;
    }
    RefreshWorkspace();
    if (!directory) static_cast<void>(OpenPath(parent / *name));
}

void MainWindow::RenameWorkspaceSelection() {
    const auto selected = SelectedWorkspacePath();
    if (!selected) return;
    const auto name = PromptWorkspaceName(Tr(L"Rename Workspace Item"), selected->filename().wstring());
    if (!name) return;
    if (!notepad_colon::RenameWorkspaceItem(*selected, *name, workspace_catalog_.Roots()))
        ::MessageBoxW(GetHwnd(), Tr(L"Workspace roots cannot be renamed, and the new name must be unused.").c_str(),
                      Tr(L"Rename").c_str(), MB_OK | MB_ICONWARNING);
    else RefreshWorkspace();
}

void MainWindow::RecycleWorkspaceSelection() {
    const auto selected = SelectedWorkspacePath();
    if (!selected) return;
    const auto prompt = Tr(L"Move this item to the Recycle Bin?") + L"\n\n" + selected->wstring();
    if (::MessageBoxW(GetHwnd(), prompt.c_str(), Tr(L"Workspace").c_str(), MB_YESNO | MB_ICONWARNING) != IDYES) return;
    if (!notepad_colon::RecycleWorkspaceItem(*selected, workspace_catalog_.Roots()))
        ::MessageBoxW(GetHwnd(), Tr(L"Workspace roots cannot be deleted, or the operation failed.").c_str(),
                      Tr(L"Workspace").c_str(), MB_OK | MB_ICONERROR);
    else RefreshWorkspace();
}

void MainWindow::RevealWorkspaceSelection() {
    const auto selected = SelectedWorkspacePath();
    if (!selected) return;
    const auto parameters = L"/select,\"" + selected->wstring() + L"\"";
    ::ShellExecuteW(GetHwnd(), L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
}

void MainWindow::CopyWorkspacePath(bool relative) {
    const auto selected = SelectedWorkspacePath();
    if (!selected) return;
    auto value = *selected;
    if (relative) for (const auto& root : workspace_catalog_.Roots())
        if (notepad_colon::IsWithinWorkspaceRoots(*selected, {root})) {
            value = selected->lexically_relative(root); break;
        }
    status_.SetText(mwfl::SetClipboardText(GetHwnd(), value.wstring())
        ? Tr(L"Path copied") : Tr(L"Could not access clipboard"));
}

void MainWindow::RemoveWorkspaceRoot() {
    const auto selected = SelectedWorkspacePath();
    if (!selected || !std::ranges::any_of(workspace_catalog_.Roots(),
        [&](const auto& root) { return SamePath(root, *selected); })) {
        status_.SetText(Tr(L"Select a workspace root to remove")); return;
    }
    static_cast<void>(workspace_catalog_.RemoveRoot(*selected));
    if (workspace_catalog_.Roots().empty()) {
        TreeView_DeleteAllItems(tree_.GetHwnd()); tree_paths_.clear(); workspace_visible_ = false;
        ApplyCompactLayout();
    } else RefreshWorkspace();
    static_cast<void>(SaveSessionSnapshot());
    SaveWorkspaceCatalog();
}

void MainWindow::OpenFolderInteractive() {
    const auto selected = mwfl::ShowFolderDialog({GetHwnd(), Tr(L"Choose a workspace folder")});
    if (selected.accepted) StartWorkspaceScan(selected.path);
}

void MainWindow::StartWorkspaceScan(std::filesystem::path root) {
    ReapCompletedWorkers(workspace_workers_);
    for (auto& worker : workspace_workers_) worker.thread.request_stop();
    workspace_operations_.Begin();
    static_cast<void>(workspace_catalog_.AddRoot(root));
    SaveWorkspaceCatalog();
    workspace_root_ = std::move(root);
    workspace_scans_.clear();
    workspace_lazy_ = true;
    RenderLazyWorkspaceRoots();
    workspace_visible_ = true;
    ApplyCompactLayout();
    status_.SetText(Tr(L"Workspace opened (folders load on demand)"));
}

void MainWindow::RenderLazyWorkspaceRoots() {
    TreeView_DeleteAllItems(tree_.GetHwnd());
    tree_paths_.clear();
    workspace_loaded_nodes_.clear();
    next_tree_id_ = 1;
    for (const auto& root : workspace_catalog_.Roots()) {
        const mwfl::TreeItemId id{next_tree_id_++};
        tree_.AddItem(id, root.filename().empty() ? root.wstring() : root.filename().wstring());
        tree_paths_[id.value] = root;
        LoadWorkspaceChildren(id);
        tree_.Expand(id);
    }
}

HTREEITEM MainWindow::FindTreeItem(mwfl::TreeItemId id, HTREEITEM item) const {
    if (!item) item = TreeView_GetRoot(tree_.GetHwnd());
    while (item) {
        TVITEMW value{}; value.mask = TVIF_PARAM; value.hItem = item;
        if (TreeView_GetItem(tree_.GetHwnd(), &value) &&
            static_cast<std::uint64_t>(value.lParam) == id.value) return item;
        if (auto child = TreeView_GetChild(tree_.GetHwnd(), item))
            if (auto found = FindTreeItem(id, child)) return found;
        item = TreeView_GetNextSibling(tree_.GetHwnd(), item);
    }
    return nullptr;
}

void MainWindow::LoadWorkspaceChildren(mwfl::TreeItemId parent) {
    if (workspace_loaded_nodes_.contains(parent.value)) return;
    const auto found = tree_paths_.find(parent.value);
    if (found == tree_paths_.end()) return;
    if (auto handle = FindTreeItem(parent)) {
        while (auto child = TreeView_GetChild(tree_.GetHwnd(), handle))
            TreeView_DeleteItem(tree_.GetHwnd(), child);
    }
    std::error_code error;
    if (!std::filesystem::is_directory(found->second, error) || error) return;
    struct Child { std::filesystem::path path; bool directory; };
    std::vector<Child> children;
    for (std::filesystem::directory_iterator iterator(found->second,
             std::filesystem::directory_options::skip_permission_denied, error), end;
         iterator != end && !error && children.size() < 2000; iterator.increment(error)) {
        const bool directory = iterator->is_directory(error);
        if (error) { error.clear(); continue; }
        if (directory && notepad_colon::IsExcludedWorkspaceDirectory(
                iterator->path().filename().wstring())) continue;
        children.push_back({iterator->path(), directory});
    }
    std::ranges::sort(children, [](const auto& left, const auto& right) {
        if (left.directory != right.directory) return left.directory;
        return ::CompareStringOrdinal(left.path.filename().c_str(), -1,
            right.path.filename().c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    auto filter = workspace_filter_.GetText();
    std::ranges::transform(filter, filter.begin(), ::towlower);
    for (const auto& child : children) {
        auto name = child.path.filename().wstring();
        auto folded = name; std::ranges::transform(folded, folded.begin(), ::towlower);
        if (!child.directory && !filter.empty() && folded.find(filter) == std::wstring::npos)
            continue;
        const mwfl::TreeItemId id{next_tree_id_++};
        tree_.AddChild(id, name, parent);
        tree_paths_[id.value] = child.path;
        if (child.directory) {
            const mwfl::TreeItemId placeholder{next_tree_id_++};
            tree_.AddChild(placeholder, L"…", id);
        }
    }
    workspace_loaded_nodes_.insert(parent.value);
}

void MainWindow::CompleteWorkspaceScan() {
    auto completed = workspace_operations_.TakeCurrent();
    if (!completed) return;
    auto scans = std::move(*completed);
    if (scans.empty()) return;
    workspace_lazy_ = false;
    workspace_scans_ = std::move(scans);
    RenderWorkspaceTree();
    std::size_t entries = 0; bool truncated = false;
    for (const auto& [root, scan] : workspace_scans_) {
        static_cast<void>(root); entries += scan.entries.size(); truncated = truncated || scan.truncated;
    }
    workspace_visible_ = true;
    ApplyCompactLayout();
    status_.SetText(Tr(L"Workspace: ") + std::to_wstring(workspace_scans_.size()) + Tr(L" root(s) | ") +
        std::to_wstring(entries) + Tr(L" entries") + (truncated ? Tr(L" (truncated)") : L""));
}

void MainWindow::RenderWorkspaceTree() {
    TreeView_DeleteAllItems(tree_.GetHwnd());
    tree_paths_.clear();
    std::uint64_t next = 1;
    auto filter = workspace_filter_.GetText();
    std::ranges::transform(filter, filter.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    for (const auto& [root, scan] : workspace_scans_) {
        const mwfl::TreeItemId root_id{next++};
        const auto label = root.filename().empty() ? root.wstring() : root.filename().wstring();
        tree_.AddItem(root_id, label);
        tree_paths_[root_id.value] = root;
        std::unordered_map<std::wstring, mwfl::TreeItemId> directory_ids;
        directory_ids[L""] = root_id;
        for (const auto& entry : scan.entries) {
            if (!entry.directory && !filter.empty() &&
                entry.search_key.find(filter) == std::wstring::npos) continue;
            const mwfl::TreeItemId id{next++};
            const auto parent_text = entry.relative_path.parent_path().wstring();
            const auto parent = directory_ids.contains(parent_text) ? directory_ids[parent_text] : root_id;
            tree_.AddChild(id, entry.relative_path.filename().wstring(), parent);
            tree_paths_[id.value] = root / entry.relative_path;
            if (entry.directory) directory_ids[entry.relative_path.wstring()] = id;
        }
        tree_.Expand(root_id);
    }
}

}  // namespace notepad_colon::app
