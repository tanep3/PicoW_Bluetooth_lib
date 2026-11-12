#pragma once
#include <cstddef>
#include <cstdint>

namespace picow::avrcp {
// Repository-authored bounded equivalent of the SDK media attribute iterator.
// Input consists only of repeated ID32/charset16/length16/value records, in
// network byte order. The caller owns immutable readable input throughout use.
class MediaAttributeIterator {
public:
  enum class Result { Item, End, Malformed };
  struct Attribute {
    uint32_t id = 0;
    uint16_t charset = 0, length = 0;
    const uint8_t *value = nullptr; // Borrowed bytes, not NUL-terminated text.
  };
  MediaAttributeIterator(const uint8_t *data, size_t size)
      : data_(data), size_(size), malformed_(!data && size) {}
  Result next(Attribute &out) {
    out = {};
    if (malformed_)
      return Result::Malformed;
    if (offset_ == size_)
      return Result::End;
    const size_t remaining = size_ - offset_;
    if (remaining < 8) {
      malformed_ = true;
      return Result::Malformed;
    }
    const auto *p = data_ + offset_;
    const uint16_t length = be16(p + 6);
    if (length > remaining - 8) {
      malformed_ = true;
      return Result::Malformed;
    }
    out = {(uint32_t(be16(p)) << 16) | be16(p + 2), be16(p + 4),
           length, p + 8};
    // Proven <= remaining, so neither the addition nor offset can wrap.
    offset_ += size_t(8) + length;
    return Result::Item;
  }
  size_t offset() const { return offset_; }
  bool malformed() const { return malformed_; }
private:
  static uint16_t be16(const uint8_t *p) {
    return uint16_t(uint16_t(p[0]) << 8) | p[1];
  }
  const uint8_t *data_;
  size_t size_, offset_ = 0;
  bool malformed_;
};
} // namespace picow::avrcp
