#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace avrcp_sample {
// Single-loop sample utility, not a profile transport or an IRQ-safe queue.
// Record must contain owned bytes and size. The caller must clear any other
// borrowed pointers before push: copying metadata cannot extend its lifetime.
template<class Record, size_t Capacity> class OwnedRecordQueue {
  static_assert(Capacity > 0, "Queue must have storage");
  std::array<Record, Capacity> records_{};
  size_t head_ = 0, count_ = 0;
  bool failed_ = false;
public:
  bool push(Record record, const uint8_t *bytes = nullptr, uint16_t size = 0) {
    if (count_ == Capacity || size > record.bytes.size() || (size && !bytes)) {
      failed_ = true;
      return false;
    }
    record.size = size;
    if (size) std::memcpy(record.bytes.data(), bytes, size);
    records_[(head_ + count_) % Capacity] = record;
    ++count_;
    return true;
  }
  bool pop(Record &record) {
    if (!count_) return false;
    record = records_[head_];
    head_ = (head_ + 1) % Capacity;
    --count_;
    return true;
  }
  size_t size() const { return count_; }
  bool failed() const { return failed_; } // Sticky: main loop stops on loss.
};
} // namespace avrcp_sample
