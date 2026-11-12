#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace picow::avrcp {
// AVCTP browsing payload reassembly. No pointer escapes until the whole
// response is present. Storage is bounded; malformed packets never become
// success.
class BrowsingResponse {
public:
  static constexpr size_t Capacity = 4096;
  enum class Result { Rejected, More, Complete };
  void reset() {
    used_ = 0;
    left_ = 0;
  }
  static uint16_t be16(const uint8_t *p) { return uint16_t(p[0]) << 8 | p[1]; }
  Result accept(const uint8_t *p, size_t n, uint8_t label, uint8_t expected) {
    if (!p || !n || (p[0] >> 4) != label || (p[0] & 3) != 2)
      return Result::Rejected;
    unsigned type = (p[0] >> 2) & 3;
    size_t header = 1;
    if (type == 0 || type == 1) {
      header = type == 0 ? 3 : 4;
      if (n < header || (type == 1 && p[1] < 2) || p[header - 2] != 0x11 ||
          p[header - 1] != 0x0e)
        return Result::Rejected;
      if (left_)
        return Result::Rejected;
      used_ = 0;
      left_ = type == 1 ? p[1] : 1;
    } else if (!left_ || (type == 3 ? left_ != 1 : left_ <= 1))
      return Result::Rejected;
    if (n <= header || n - header > Capacity - used_) {
      reset();
      return Result::Rejected;
    }
    memcpy(bytes_.data() + used_, p + header, n - header);
    used_ += n - header;
    --left_;
    if (left_)
      return Result::More;
    if (used_ < 4 || (bytes_[0] != expected && bytes_[0] != 0xa0) ||
        be16(bytes_.data() + 1) != used_ - 3 || !valid()) {
      reset();
      return Result::Rejected;
    }
    return Result::Complete;
  }
  const uint8_t *data() const { return bytes_.data() + 3; }
  size_t size() const { return used_ >= 3 ? used_ - 3 : 0; }

  static bool validPayload(uint8_t pdu, const uint8_t *p, size_t n) {
    if (!p || !n || p[0] > 0x16)
      return false;
    if (p[0] != 4)
      return n == 1; // Browsing success is 0x04.
    size_t pos = 0, count = 0;
    switch (pdu) {
    case 0x70: // set browsed player: status, UID counter, item count, charset,
               // depth
      if (n < 10)
        return false;
      count = p[9];
      pos = 10;
      while (count--) {
        if (pos + 2 > n)
          return false;
        size_t len = be16(p + pos);
        pos += 2;
        if (len > n - pos)
          return false;
        pos += len;
      }
      return pos == n;
    case 0x71: // folder items: status, UID counter, count, item
               // type/length/data
      if (n < 5)
        return false;
      count = be16(p + 3);
      pos = 5;
      while (count--) {
        if (pos + 3 > n || p[pos] < 1 || p[pos] > 3)
          return false;
        size_t len = be16(p + pos + 1);
        pos += 3;
        if (!len || len > n - pos)
          return false;
        pos += len;
      }
      return pos == n;
    case 0x72:
      return n == 5; // change path + item count
    case 0x73: // item attributes: status, count, ID32/charset16/len16/value
      if (n < 2)
        return false;
      count = p[1];
      pos = 2;
      while (count--) {
        if (pos + 8 > n)
          return false;
        size_t len = be16(p + pos + 6);
        pos += 8;
        if (len > n - pos)
          return false;
        pos += len;
      }
      return pos == n;
    case 0x75:
    case 0x80:
      return n == 7; // total items/search: status, UID counter, count
    default:
      return false;
    }
  }

private:
  bool valid() const { return validPayload(bytes_[0], data(), size()); }
  std::array<uint8_t, Capacity> bytes_{};
  size_t used_ = 0;
  uint8_t left_ = 0;
};
} // namespace picow::avrcp
