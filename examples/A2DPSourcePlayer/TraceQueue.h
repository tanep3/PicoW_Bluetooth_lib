#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Single main-loop owner. Bounded memory and work; never waits for the reader.
// Full queues drop new records, preserving the event sequence already captured.
template <size_t Count = 64, size_t LineSize = 640>
class TraceQueue {
public:
    bool push(const char* data, size_t length) {
        if (count_ == Count || length > LineSize) { ++dropped_; return false; }
        auto& entry = entries_[(head_ + count_) % Count];
        std::memcpy(entry.data.data(), data, length);
        entry.size = length;
        ++count_;
        return true;
    }
    const char* front() const { return count_ ? entries_[head_].data.data() + offset_ : nullptr; }
    size_t remaining() const { return count_ ? entries_[head_].size - offset_ : 0; }
    void consume(size_t bytes) {
        if (!count_ || bytes > remaining()) return;
        offset_ += bytes;
        if (offset_ == entries_[head_].size) { head_ = (head_ + 1) % Count; --count_; offset_ = 0; }
    }
    size_t pending() const { return count_; }
    uint32_t dropped() const { return dropped_; }
private:
    struct Entry { std::array<char, LineSize> data{}; size_t size = 0; };
    std::array<Entry, Count> entries_{};
    size_t head_ = 0, count_ = 0, offset_ = 0;
    uint32_t dropped_ = 0;
};
