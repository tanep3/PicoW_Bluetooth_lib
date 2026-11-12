#pragma once
#include "BrowsingResponse.h"
#include <algorithm>
namespace picow::avrcp {
class BrowsingCommand {
public:
  using Result = BrowsingResponse::Result;
  void reset() {
    used_ = 0;
    left_ = 0;
  }
  bool assembling() const { return left_ != 0; }
  uint8_t label() const { return label_; }
  uint8_t pdu() const { return used_ ? bytes_[0] : 0; }
  const uint8_t *data() const { return bytes_.data() + 3; }
  size_t size() const { return used_ >= 3 ? used_ - 3 : 0; }
  Result accept(const uint8_t *p, size_t n) {
    if (!p || !n || (p[0] & 3))
      return Result::Rejected;
    unsigned type = (p[0] >> 2) & 3;
    size_t header = 1;
    if (type < 2) {
      header = type ? 4 : 3;
      if (left_ || n < header || (type && p[1] < 2) || p[header - 2] != 0x11 ||
          p[header - 1] != 0x0e)
        return Result::Rejected;
      label_ = p[0] >> 4;
      used_ = 0;
      left_ = type ? p[1] : 1;
    } else if (!left_ || (p[0] >> 4) != label_ ||
               (type == 3 ? left_ != 1 : left_ <= 1))
      return Result::Rejected;
    const auto saved_used = used_;
    const auto saved_left = left_;
    if (n <= header || n - header > bytes_.size() - used_) {
      if (type < 2)
        reset();
      return Result::Rejected;
    }
    memcpy(bytes_.data() + used_, p + header, n - header);
    used_ += n - header;
    --left_;
    if (left_)
      return Result::More;
    if (used_ < 3 || BrowsingResponse::be16(bytes_.data() + 1) != size() ||
        !valid(pdu(), data(), size())) {
      // Reject without consuming an admitted fragmented request.
      if (type >= 2) {
        used_ = saved_used;
        left_ = saved_left;
      } else
        reset();
      return Result::Rejected;
    }
    return Result::Complete;
  }
  static bool valid(uint8_t pdu, const uint8_t *p, size_t n) {
    if (!p)
      return false;
    switch (pdu) {
    case 0x70:
      return n == 2;
    case 0x71: {
      if (n < 10 || p[0] > 3 || be32(p + 1) > be32(p + 5))
        return false;
      unsigned count = p[9];
      if (count == 255)
        return n == 10;
      return attributes(p + 10, n - 10, count);
    }
    case 0x72:
      return n == 11 && p[2] <= 1;
    case 0x73:
      return n >= 12 && p[0] >= 1 && p[0] <= 3 &&
             attributes(p + 12, n - 12, p[11]);
    case 0x75:
      return n == 1 && p[0] <= 3;
    case 0x80:
      return n >= 5 && BrowsingResponse::be16(p + 2) == n - 4;
    default:
      return false;
    }
  }

private:
  static uint32_t be32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 |
           p[3];
  }
  static bool attributes(const uint8_t *p, size_t n, unsigned count) {
    if (count > 8 || n != count * 4)
      return false;
    unsigned seen = 0;
    for (unsigned i = 0; i < count; ++i) {
      auto id = be32(p + i * 4);
      if (id < 1 || id > 8 || (seen & (1u << id)))
        return false;
      seen |= 1u << id;
    }
    return true;
  }
  std::array<uint8_t, 4096> bytes_{};
  size_t used_ = 0;
  uint8_t left_ = 0, label_ = 0;
};
class BrowsingReply {
public:
  bool prepare(uint8_t label, uint8_t pdu, uint8_t status, const uint8_t *p,
               size_t n) {
    return prepareParts(label, pdu, status, nullptr, 0, p, n);
  }
  bool prepareParts(uint8_t label, uint8_t pdu, uint8_t status,
                    const uint8_t *prefix, size_t pn, const uint8_t *p,
                    size_t n) {
    if (active() || label > 15 || pn > bytes_.size() - 4 ||
        n > bytes_.size() - 4 - pn || (!p && n) || (!prefix && pn))
      return false;
    const size_t total = pn + n;
    bytes_[0] = pdu;
    bytes_[1] = uint8_t((total + 1) >> 8);
    bytes_[2] = uint8_t(total + 1);
    bytes_[3] = status;
    if (pn)
      memcpy(bytes_.data() + 4, prefix, pn);
    if (n)
      memcpy(bytes_.data() + 4 + pn, p, n);
    if (!BrowsingResponse::validPayload(pdu, bytes_.data() + 3, total + 1))
      return false;
    label_ = label;
    used_ = total + 4;
    offset_ = 0;
    return true;
  }
  size_t frame(uint16_t mtu, uint8_t *out, size_t capacity) const {
    if (!active() || mtu < 48 || capacity < mtu)
      return 0;
    size_t header = 1;
    uint8_t type = 2;
    if (!offset_) {
      if (used_ <= size_t(mtu - 3)) {
        header = 3;
        type = 0;
      } else {
        header = 4;
        type = 1;
      }
    } else if (used_ - offset_ <= size_t(mtu - 1))
      type = 3;
    out[0] = uint8_t(label_ << 4 | type << 2 | 2);
    if (type == 0) {
      out[1] = 0x11;
      out[2] = 0x0e;
    }
    if (type == 1) {
      size_t count = 1 + (used_ - (mtu - 4) + (mtu - 2)) / (mtu - 1);
      if (count > 255)
        return 0;
      out[1] = uint8_t(count);
      out[2] = 0x11;
      out[3] = 0x0e;
    }
    size_t amount = std::min(used_ - offset_, size_t(mtu) - header);
    memcpy(out + header, bytes_.data() + offset_, amount);
    return header + amount;
  }
  void consume(const uint8_t *frame, size_t n) {
    unsigned type = (frame[0] >> 2) & 3;
    size_t h = type == 0 ? 3 : type == 1 ? 4 : 1;
    if (n <= h || n - h > used_ - offset_)
      return;
    offset_ += n - h;
    if (offset_ == used_)
      reset();
  }
  void reset() { used_ = offset_ = 0; }
  bool active() const { return used_ != 0; }

private:
  std::array<uint8_t, 4096> bytes_{};
  size_t used_ = 0, offset_ = 0;
  uint8_t label_ = 0;
};
} // namespace picow::avrcp
