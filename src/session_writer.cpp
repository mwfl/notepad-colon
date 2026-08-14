#include <notepad_colon/session_writer.h>

namespace notepad_colon {

SessionWriter::SessionWriter() : worker_([this](std::stop_token stop) { Run(stop); }) {}

SessionWriter::~SessionWriter() noexcept {
    static_cast<void>(Flush());
    worker_.request_stop();
    changed_.notify_all();
}

void SessionWriter::Queue(std::filesystem::path path, Session session) {
    {
        std::scoped_lock lock{mutex_};
        pending_ = Request{std::move(path), std::move(session)};
    }
    changed_.notify_all();
}

bool SessionWriter::Flush() noexcept {
    try {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] { return !pending_ && !writing_; });
        return last_result_;
    } catch (...) { return false; }
}

void SessionWriter::Run(std::stop_token stop) noexcept {
    for (;;) {
        std::optional<Request> request;
        {
            std::unique_lock lock{mutex_};
            changed_.wait(lock, stop, [this] { return pending_.has_value(); });
            if (!pending_ && stop.stop_requested()) return;
            request = std::move(pending_);
            pending_.reset();
            writing_ = true;
        }
        const bool result = request && SaveSessionAtomic(request->path, request->session);
        {
            std::scoped_lock lock{mutex_};
            last_result_ = result;
            writing_ = false;
        }
        changed_.notify_all();
    }
}

}  // namespace notepad_colon
