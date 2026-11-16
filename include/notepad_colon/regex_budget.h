#pragma once

#include <chrono>
#include <compare>
#include <cstddef>
#include <iterator>
#include <stop_token>
#include <string_view>

namespace notepad_colon {

struct RegexInterrupted { bool cancelled; };

// std::regex has no cancellation API. Keep its input iterators wrapped so even
// a single backtracking match checks cancellation and its elapsed-time budget.
// Random access keeps match positions and capture lengths constant-time.
class RegexBudget {
public:
    explicit RegexBudget(std::stop_token stop = {},
                         std::chrono::steady_clock::duration limit = std::chrono::seconds{2})
        : stop_(stop), deadline_(std::chrono::steady_clock::now() + limit) { CheckNow(); }
    void CheckNow() const {
        if (stop_.stop_requested()) throw RegexInterrupted{true};
        if (std::chrono::steady_clock::now() >= deadline_) throw RegexInterrupted{false};
    }
    void Poll() {
        if ((++operations_ & 511) == 0) CheckNow();
    }
private:
    std::stop_token stop_;
    std::chrono::steady_clock::time_point deadline_;
    std::size_t operations_ = 0;
};

template <typename Char>
class RegexInputIterator {
public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = Char;
    using difference_type = std::ptrdiff_t;
    using pointer = const Char*;
    using reference = const Char&;
    RegexInputIterator() = default;
    RegexInputIterator(std::basic_string_view<Char> text, std::size_t offset, RegexBudget& budget)
        : data_(text.data()), offset_(static_cast<difference_type>(offset)), budget_(&budget) {}
    reference operator*() const { Poll(); return data_[offset_]; }
    pointer operator->() const { Poll(); return data_ + offset_; }
    reference operator[](difference_type amount) const { return *(*this + amount); }
    RegexInputIterator& operator++() { Poll(); ++offset_; return *this; }
    RegexInputIterator operator++(int) { auto previous = *this; ++*this; return previous; }
    RegexInputIterator& operator--() { Poll(); --offset_; return *this; }
    RegexInputIterator operator--(int) { auto previous = *this; --*this; return previous; }
    RegexInputIterator& operator+=(difference_type amount) { Poll(); offset_ += amount; return *this; }
    RegexInputIterator& operator-=(difference_type amount) { return *this += -amount; }
    friend RegexInputIterator operator+(RegexInputIterator value, difference_type amount) { return value += amount; }
    friend RegexInputIterator operator+(difference_type amount, RegexInputIterator value) { return value += amount; }
    friend RegexInputIterator operator-(RegexInputIterator value, difference_type amount) { return value -= amount; }
    friend difference_type operator-(const RegexInputIterator& left, const RegexInputIterator& right) {
        left.Poll(); return left.offset_ - right.offset_;
    }
    friend bool operator==(const RegexInputIterator& left, const RegexInputIterator& right) {
        left.Poll(); return left.data_ == right.data_ && left.offset_ == right.offset_;
    }
    friend auto operator<=>(const RegexInputIterator& left, const RegexInputIterator& right) {
        left.Poll(); return left.offset_ <=> right.offset_;
    }
private:
    void Poll() const { if (budget_) budget_->Poll(); }
    const Char* data_ = nullptr;
    difference_type offset_ = 0;
    RegexBudget* budget_ = nullptr;
};

} // namespace notepad_colon
