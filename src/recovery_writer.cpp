#include <notepad_colon/recovery_writer.h>
#include <utility>
namespace notepad_colon {
RecoveryWriter::RecoveryWriter(std::size_t maximum_pending_bytes) : maximum_pending_(maximum_pending_bytes), worker_([this](std::stop_token stop) { Run(stop); }) {}
RecoveryWriter::~RecoveryWriter() noexcept {
    static_cast<void>(Flush());
    worker_.request_stop(); changed_.notify_all();
}
bool RecoveryWriter::Queue(std::shared_ptr<RecoveryStore> store, std::wstring key,
    std::wstring title, std::filesystem::path original_path, std::wstring text) {
    if (!store || text.size() > maximum_pending_ / sizeof(wchar_t)) return false;
    const auto bytes = text.size() * sizeof(wchar_t);
    const auto id = store->Directory().wstring() + L"\n" + key;
    {
        std::scoped_lock lock{mutex_};
        const auto previous = pending_.find(id);
        const auto old_bytes = previous == pending_.end() ? 0 : previous->second.bytes;
        if (bytes > maximum_pending_ - (bytes_ - old_bytes) ||
            (previous == pending_.end() && pending_.size() >= 256)) return false;
        pending_[id] = {std::move(store), std::move(key), std::move(title),
            std::move(original_path), std::move(text), bytes};
        bytes_ = bytes_ - old_bytes + bytes;
    }
    changed_.notify_all();
    return true;
}
bool RecoveryWriter::Flush() noexcept {
    try {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] { return pending_.empty() && !writing_; });
        return std::exchange(last_result_, true);
    } catch (...) { return false; }
}
std::optional<RecoveryWriteResult> RecoveryWriter::TakeResult() {
    std::scoped_lock lock{mutex_};
    return std::exchange(completed_result_, std::nullopt);
}
std::size_t RecoveryWriter::PendingBytes() const {
    std::scoped_lock lock{mutex_}; return bytes_;
}
void RecoveryWriter::Run(std::stop_token stop) noexcept {
    for (;;) {
        Request request;
        {
            std::unique_lock lock{mutex_};
            changed_.wait(lock, stop, [this] { return !pending_.empty(); });
            if (pending_.empty() && stop.stop_requested()) return;
            auto item = pending_.extract(pending_.begin());
            request = std::move(item.mapped());
            writing_ = true;
        }
        bool result = false;
        try { result = request.store->Save(request.key, request.title, request.original_path, request.text); }
        catch (...) { result = false; }
        {
            std::scoped_lock lock{mutex_};
            bytes_ -= request.bytes;
            last_result_ = last_result_ && result;
            auto completed = completed_result_.value_or(RecoveryWriteResult{});
            completed.success = completed.success && result;
            completed.history_full = completed.history_full || (!result && request.store->LastSaveHitQuota());
            completed_result_ = completed;
            writing_ = false;
        }
        changed_.notify_all();
    }
}
} // namespace notepad_colon
