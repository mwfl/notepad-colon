#pragma once
#include <notepad_colon/recovery.h>
#include <condition_variable>
#include <map>
#include <memory>
#include <stop_token>
#include <thread>

namespace notepad_colon {
// One worker, coalesced per document, with a 128 MiB limit on queued and active
// UTF-16 inputs. UI callers only copy their current editor text; encoding,
// flushing, metadata reads and retention all happen here.
struct RecoveryWriteResult { bool success = true; bool history_full = false; };
class RecoveryWriter final {
public:
    explicit RecoveryWriter(std::size_t maximum_pending_bytes = 128 * 1024 * 1024);
    ~RecoveryWriter() noexcept;
    RecoveryWriter(const RecoveryWriter&) = delete;
    RecoveryWriter& operator=(const RecoveryWriter&) = delete;
    bool Queue(std::shared_ptr<RecoveryStore> store, std::wstring key, std::wstring title,
               std::filesystem::path original_path, std::wstring text);
    bool Flush() noexcept;
    std::optional<RecoveryWriteResult> TakeResult();
    std::size_t PendingBytes() const;
private:
    struct Request {
        std::shared_ptr<RecoveryStore> store;
        std::wstring key, title;
        std::filesystem::path original_path;
        std::wstring text;
        std::size_t bytes = 0;
    };
    void Run(std::stop_token stop) noexcept;
    mutable std::mutex mutex_;
    std::condition_variable_any changed_;
    std::map<std::wstring, Request> pending_;
    std::size_t bytes_ = 0;
    bool writing_ = false, last_result_ = true;
    std::optional<RecoveryWriteResult> completed_result_;
    std::size_t maximum_pending_;
    std::jthread worker_;
};
} // namespace notepad_colon
