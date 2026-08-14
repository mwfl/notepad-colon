#pragma once

#include <notepad_colon/session.h>

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

namespace notepad_colon {

// Coalesces session snapshots and performs the durable write away from the UI thread.
class SessionWriter {
public:
    SessionWriter();
    ~SessionWriter() noexcept;
    SessionWriter(const SessionWriter&) = delete;
    SessionWriter& operator=(const SessionWriter&) = delete;

    void Queue(std::filesystem::path path, Session session);
    bool Flush() noexcept;

private:
    struct Request { std::filesystem::path path; Session session; };
    void Run(std::stop_token stop) noexcept;

    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::optional<Request> pending_;
    bool writing_ = false;
    bool last_result_ = true;
    std::jthread worker_;
};

}  // namespace notepad_colon
