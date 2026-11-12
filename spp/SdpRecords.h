#pragma once
#include "../spp-core/SppCore.h"
namespace spp {
// Parses complete SDP attribute VALUE events, not truncated service-name
// events. record_id is the query-local index; Candidate.record is attribute
// 0x0000. No result is published until finish(), so attribute order is
// immaterial.
class SdpRecords {
  struct Record {
    uint16_t index = 0;
    uint32_t handle = 0;
    uint8_t channel = 0;
    bool used = false, has_handle = false, has_protocol = false;
  };
  std::array<Record, CandidateCapacity> records_{};
  Error error_ = Error::None;
  std::array<uint8_t, 256> attribute_buffer_{};
  uint16_t assembling_index_ = 0, assembling_id_ = 0, assembling_size_ = 0,
           next_ = 0;
  struct Element {
    uint8_t type;
    const uint8_t *data;
    size_t size, total;
  };
  static bool element(const uint8_t *p, size_t n, Element &e) {
    if (!p || !n)
      return false;
    const uint8_t tag = p[0], size = tag & 7;
    size_t h = 1, len = 0;
    if (size < 5)
      len = size_t(1) << size;
    else {
      size_t count = size_t(1) << (size - 5);
      if (n < 1 + count)
        return false;
      h += count;
      for (size_t i = 1; i < h; ++i)
        len = (len << 8) | p[i];
    }
    if ((tag >> 3) == 0) {
      if (tag != 0)
        return false;
      len = 0;
    }
    if (len > n - h)
      return false;
    e = {uint8_t(tag >> 3), p + h, len, h + len};
    return true;
  }
  static bool protocol(const uint8_t *p, size_t n, uint8_t &channel) {
    Element outer{};
    if (!element(p, n, outer) || outer.type != 6 || outer.total != n)
      return false;
    size_t at = 0;
    bool found = false;
    while (at < outer.size) {
      Element desc{}, uuid{};
      if (!element(outer.data + at, outer.size - at, desc) || desc.type != 6 ||
          !element(desc.data, desc.size, uuid) || uuid.type != 3)
        return false;
      bool rfcomm =
          (uuid.size == 2 && uuid.data[0] == 0 && uuid.data[1] == 3) ||
          (uuid.size == 4 && uuid.data[0] == 0 && uuid.data[1] == 0 &&
           uuid.data[2] == 0 && uuid.data[3] == 3);
      if (uuid.size != 2 && uuid.size != 4 && uuid.size != 16)
        return false;
      if (uuid.size == 16) {
        const auto u = Uuid::from16(3);
        rfcomm = std::memcmp(uuid.data, u.bytes.data(), 16) == 0;
      }
      size_t pos = uuid.total;
      if (rfcomm) {
        Element value{};
        if (found || !element(desc.data + pos, desc.size - pos, value) ||
            value.type != 1 || value.size != 1 || !validChannel(value.data[0]))
          return false;
        channel = value.data[0];
        found = true;
        pos += value.total;
      }
      while (pos < desc.size) {
        Element value{};
        if (!element(desc.data + pos, desc.size - pos, value))
          return false;
        pos += value.total;
      }
      at += desc.total;
    }
    return true; // A well-formed non-RFCOMM record is not a candidate.
  }

public:
  void reset() {
    records_ = {};
    error_ = Error::None;
    assembling_size_ = next_ = 0;
  }
  // BTstack 1.6.2 emits VALUE using the legacy 11-byte BYTE layout.
  // Validate callback size before calling; do not use VALUE value getter.
  bool byte(uint16_t index, uint16_t id, uint16_t size, uint16_t offset,
            uint8_t value) {
    if (error_ != Error::None)
      return false;
    if (id != 0 && id != 4)
      return true;
    if (size > attribute_buffer_.size() || offset >= attribute_buffer_.size() ||
        (size && offset >= size)) {
      error_ = Error::InvalidInput;
      return false;
    }
    if (!offset) {
      if (next_) {
        error_ = Error::InvalidInput;
        return false;
      }
      assembling_index_ = index;
      assembling_id_ = id;
      assembling_size_ = size;
    }
    if (index != assembling_index_ || id != assembling_id_ ||
        (size && assembling_size_ && size != assembling_size_) || offset != next_) {
      error_ = Error::InvalidInput;
      return false;
    }
    attribute_buffer_[next_++] = value;
    // This SDK emits DE header bytes before it knows the total attribute size.
    // They carry size=0. Derive the total from our bounded header instead of
    // treating the first callback's unknown size as a malformed attribute.
    const uint8_t tag = attribute_buffer_[0], bits = tag & 7;
    const size_t header = bits < 5 ? 1 : 1 + (size_t(1) << (bits - 5));
    if (!size && offset >= header) {
      error_ = Error::InvalidInput;
      return false;
    }
    if (next_ >= header) {
      size_t payload = bits < 5 ? size_t(1) << bits : 0;
      if (bits >= 5)
        for (size_t i = 1; i < header; ++i)
          payload = (payload << 8) | attribute_buffer_[i];
      if ((tag >> 3) == 0)
        payload = 0;
      if (payload > attribute_buffer_.size() - header ||
          (size && size != header + payload) ||
          (assembling_size_ && assembling_size_ != header + payload)) {
        error_ = Error::InvalidInput;
        return false;
      }
      assembling_size_ = static_cast<uint16_t>(header + payload);
    }
    if (assembling_size_ && next_ == assembling_size_) {
      const auto complete_size = assembling_size_;
      next_ = 0;
      assembling_size_ = 0;
      return attribute(index, id, attribute_buffer_.data(), complete_size);
    }
    return true;
  }
  Error error() const { return error_; }
  bool attribute(uint16_t index, uint16_t id, const uint8_t *p, size_t n) {
    if (error_ != Error::None)
      return false;
    if (id != 0 && id != 4)
      return true;
    Record *r = nullptr;
    for (auto &v : records_)
      if (v.used && v.index == index) {
        r = &v;
        break;
      }
    if (!r)
      for (auto &v : records_)
        if (!v.used) {
          v.used = true;
          v.index = index;
          r = &v;
          break;
        }
    if (!r) {
      error_ = Error::CandidateOverflow;
      return false;
    }
    bool ok = false;
    if (id == 0) {
      Element e{};
      ok = !r->has_handle && element(p, n, e) && e.total == n && e.type == 1 &&
           e.size == 4;
      if (ok) {
        r->handle = 0;
        for (size_t i = 0; i < 4; ++i)
          r->handle = (r->handle << 8) | e.data[i];
        r->has_handle = true;
      }
    } else {
      ok = !r->has_protocol && protocol(p, n, r->channel);
      if (ok)
        r->has_protocol = true;
    }
    if (!ok)
      error_ = Error::InvalidInput;
    return ok;
  }
  Error finish(std::array<Candidate, CandidateCapacity> &out,
               uint8_t &count) const {
    count = 0;
    if (error_ != Error::None)
      return error_;
    if (next_)
      return Error::InvalidInput;
    for (const auto &r : records_)
      if (r.used) {
        if (!r.has_handle || !r.has_protocol)
          return Error::InvalidInput;
        if (r.channel)
          out[count++] = {r.handle, r.channel};
      }
    return Error::None;
  }
};
} // namespace spp
