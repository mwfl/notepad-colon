#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace notepad_colon {

struct RecoverySnapshot {
    std::filesystem::path file;
    std::filesystem::path original_path;
    std::wstring title;
    std::chrono::system_clock::time_point created;
    std::uintmax_t size = 0;
};

class RecoveryStore final {
public:
    explicit RecoveryStore(std::filesystem::path directory, std::size_t retention = 20);
    bool Save(std::wstring_view document_key, std::wstring_view title,
              const std::filesystem::path& original_path, std::wstring_view text);
    std::vector<RecoverySnapshot> List() const;
    std::optional<std::wstring> Load(const RecoverySnapshot& snapshot) const;
    bool Remove(const RecoverySnapshot& snapshot) const;
    void Prune() const;

private:
    std::filesystem::path directory_;
    std::size_t retention_;
};

}  // namespace notepad_colon
