#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace notepad_colon {

struct RecoverySnapshot {
    std::filesystem::path file;
    std::filesystem::path original_path;
    std::wstring title;
    std::chrono::system_clock::time_point created;
    std::uintmax_t size = 0;
    std::uint64_t document = 0;
};

class RecoveryStore final {
public:
    explicit RecoveryStore(std::filesystem::path directory, std::size_t retention = 20,
                           std::uintmax_t maximum_bytes = 256ull * 1024 * 1024);
    const std::filesystem::path& Directory() const noexcept { return directory_; }
    bool LastSaveHitQuota() const noexcept { return quota_full_.load(); }
    bool Save(std::wstring_view document_key, std::wstring_view title,
              const std::filesystem::path& original_path, std::wstring_view text);
    std::vector<RecoverySnapshot> List() const;
    std::optional<std::wstring> Load(const RecoverySnapshot& snapshot) const;
    bool Remove(const RecoverySnapshot& snapshot) const;
    void Prune() const;

private:
    std::filesystem::path directory_;
    std::size_t retention_;
    std::uintmax_t maximum_bytes_;
    mutable std::recursive_mutex mutex_;
    std::atomic_bool quota_full_{false};
};

}  // namespace notepad_colon
