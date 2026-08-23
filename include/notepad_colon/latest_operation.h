#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace notepad_colon {

// Thread-safe handoff for replaceable background work. Starting a new
// generation invalidates every older producer, so a late completion can never
// overwrite newer UI state.
template <typename T>
class LatestOperation {
public:
    std::uint64_t Begin() noexcept {
        std::scoped_lock lock{mutex_};
        pending_.reset();
        return ++generation_;
    }

    std::uint64_t CurrentGeneration() const noexcept {
        std::scoped_lock lock{mutex_};
        return generation_;
    }

    bool Publish(std::uint64_t generation, T value) {
        std::scoped_lock lock{mutex_};
        if (generation != generation_) return false;
        pending_.emplace(std::move(value));
        return true;
    }

    std::optional<T> TakeCurrent() {
        std::scoped_lock lock{mutex_};
        if (!pending_) return std::nullopt;
        auto result = std::move(pending_);
        pending_.reset();
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::uint64_t generation_ = 0;
    std::optional<T> pending_;
};

}  // namespace notepad_colon
