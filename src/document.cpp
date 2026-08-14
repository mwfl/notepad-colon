#include <notepad_colon/document.h>

#include <algorithm>
#include <cwctype>

namespace notepad_colon {

std::wstring Document::DisplayName() const {
    const auto name = HasPath() ? path.filename().wstring() : untitled_name;
    return dirty ? name + L" *" : name;
}

DocumentId Workspace::AddUntitled() {
    Document document;
    document.id = next_id_++;
    document.untitled_name = untitled_counter_ == 1
        ? L"Untitled"
        : L"Untitled " + std::to_wstring(untitled_counter_);
    ++untitled_counter_;
    documents_.push_back(std::move(document));
    active_ = documents_.back().id;
    return *active_;
}

std::optional<DocumentId> Workspace::AddPath(const std::filesystem::path& path) {
    if (path.empty()) return std::nullopt;
    const auto existing = std::find_if(documents_.begin(), documents_.end(),
        [&](const Document& document) { return document.HasPath() && SamePath(document.path, path); });
    if (existing != documents_.end()) {
        active_ = existing->id;
        return existing->id;
    }
    Document document;
    document.id = next_id_++;
    document.path = path;
    documents_.push_back(std::move(document));
    active_ = documents_.back().id;
    return *active_;
}

bool Workspace::Remove(DocumentId id) noexcept {
    const auto found = std::find_if(documents_.begin(), documents_.end(),
                                    [id](const Document& document) { return document.id == id; });
    if (found == documents_.end()) return false;
    const auto index = static_cast<std::size_t>(found - documents_.begin());
    documents_.erase(found);
    if (active_ == id) {
        if (documents_.empty()) active_.reset();
        else active_ = documents_[std::min(index, documents_.size() - 1)].id;
    }
    return true;
}

bool Workspace::Select(DocumentId id) noexcept {
    if (!Find(id)) return false;
    active_ = id;
    return true;
}

Document* Workspace::Find(DocumentId id) noexcept {
    const auto found = std::find_if(documents_.begin(), documents_.end(),
                                    [id](const Document& document) { return document.id == id; });
    return found == documents_.end() ? nullptr : &*found;
}

const Document* Workspace::Find(DocumentId id) const noexcept {
    return const_cast<Workspace*>(this)->Find(id);
}

Document* Workspace::Active() noexcept { return active_ ? Find(*active_) : nullptr; }
const Document* Workspace::Active() const noexcept { return active_ ? Find(*active_) : nullptr; }

bool Workspace::SamePath(const std::filesystem::path& left,
                         const std::filesystem::path& right) noexcept {
    auto a = left.lexically_normal().wstring();
    auto b = right.lexically_normal().wstring();
    std::transform(a.begin(), a.end(), a.begin(), std::towlower);
    std::transform(b.begin(), b.end(), b.begin(), std::towlower);
    return a == b;
}

}  // namespace notepad_colon

