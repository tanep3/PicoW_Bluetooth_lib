// Promoted from the independently tested sink probe; probe remains unchanged.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace picow::sink {
// No interrupt or second-core access. BTstack callbacks run inside poll(),
// copy into fixed storage, then main consumes outside that callback stack.
template<size_t Slots, size_t Capacity> struct CopyQueue {
  struct Packet { std::array<uint8_t, Capacity> data{}; size_t size = 0; uint8_t tag = 0; uint32_t epoch = 0; uint64_t order = 0, loss = 0; };
  std::array<Packet, Slots> packets{};
  size_t head = 0, count = 0, high_water = 0;
  uint64_t overflow = 0, oversize = 0, discarded = 0;
  bool push(const uint8_t *p, size_t n, uint8_t tag = 0, uint32_t epoch = 0, uint64_t order = 0, uint64_t loss = 0) {
    if (!p || n > Capacity) { ++oversize; return false; }
    if (count == Slots) { ++overflow; return false; }
    auto &q = packets[(head + count) % Slots];
    std::memcpy(q.data.data(), p, n); q.size = n; q.tag = tag; q.epoch = epoch; q.order = order; q.loss = loss;
    ++count; if (count > high_water) high_water = count;
    return true;
  }
  const Packet &front() const { return packets[head]; }
  void pop() { if (count) { head = (head + 1) % Slots; --count; } }
  void clear() { discarded += count; count = head = 0; }
};
// A2DP 1.4.1 4.3.2 and Appendix B.9. Wire modes: mono/dual/stereo/joint.
inline constexpr size_t MaxSbcFrameBytes = 524; // 16 blocks, dual, 8 bands, pool128.
struct Config {
  uint32_t rate = 0; uint8_t mode = 0, min_pool = 2, max_pool = 53;
  uint8_t blocks = 16, subbands = 8, allocation = 0; // 0 loudness, 1 SNR
  uint8_t channels() const { return mode == 0 ? 1 : 2; }
  uint16_t samples() const { return uint16_t(blocks) * subbands; }
  bool valid() const {
    return (rate == 44100 || rate == 48000) && mode <= 3 &&
      (blocks == 4 || blocks == 8 || blocks == 12 || blocks == 16) &&
      (subbands == 4 || subbands == 8) && allocation <= 1 &&
      min_pool >= 2 && max_pool <= 86 && min_pool <= max_pool && legalPool(min_pool);
  }
  size_t frameBytes(uint8_t pool) const {
    const size_t bits = mode < 2 ? size_t(blocks) * channels() * pool :
      size_t(blocks) * pool + (mode == 3 ? subbands : 0);
    return 4 + size_t(subbands) * channels() / 2 + (bits + 7) / 8;
  }
  bool legalPool(uint8_t pool) const {
    return pool >= 2 && pool <= 250 &&
      pool <= (mode < 2 ? 16 : 32) * subbands &&
      uint64_t(frameBytes(pool)) * 8 * rate <= uint64_t(mode == 0 ? 320000 : 512000) * samples();
  }
  bool acceptsPool(uint8_t pool) const { return valid() && pool >= min_pool && pool <= max_pool && legalPool(pool); }
  bool matches(const uint8_t *f) const {
    constexpr uint32_t rates[]{16000,32000,44100,48000};
    return f[0] == 0x9c && rates[f[1] >> 6] == rate &&
      ((f[1] >> 2) & 3) == mode && 4 * (((f[1] >> 4) & 3) + 1) == blocks &&
      (f[1] & 1 ? 8 : 4) == subbands && ((f[1] >> 1) & 1) == allocation && acceptsPool(f[2]);
  }
};
struct Frame { size_t offset = 0, size = 0; };
struct PacketView {
  uint16_t sequence = 0; uint32_t timestamp = 0, ssrc = 0;
  uint8_t count = 0, payload_type = 0;
  uint16_t samples_per_frame = 128;
  std::array<Frame, 15> frames{};
};
enum class ParseError : uint8_t { None, Short, Rtp, Fragmented, Count, Codec, Bounds, Trailing, CountValue };
inline uint16_t be16(const uint8_t *p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
// Whole-packet prevalidation before any frame is decoded. This stateless parser
// rejects fragment headers; the production Reassembler wraps it for fragments.
inline ParseError parse(const uint8_t *p, size_t n, const Config &c, PacketView &out) {
  out = {};
  if (!p || n < 13) return ParseError::Short;
  if ((p[0] >> 6) != 2) return ParseError::Rtp;
  size_t pos = 12 + 4 * (p[0] & 15);
  if (pos >= n) return ParseError::Bounds;
  if (p[0] & 0x10) {
    if (n - pos < 4) return ParseError::Bounds;
    size_t extension = 4 + size_t(be16(p + pos + 2)) * 4;
    if (extension >= n - pos) return ParseError::Bounds;
    pos += extension;
  }
  if (p[0] & 0x20) {
    size_t padding = p[n - 1];
    if (!padding || padding >= n - pos) return ParseError::Bounds;
    n -= padding;
  }
  const uint8_t header = p[pos++];
  if (header & 0xe0) return ParseError::Fragmented;
  if (header & 0x10) return ParseError::Rtp;
  out.count = header & 15;
  if (!out.count) return ParseError::Count;
  if (!c.valid()) return ParseError::Codec;
  out.sequence = be16(p + 2); out.timestamp = be32(p + 4); out.ssrc = be32(p + 8); out.payload_type = p[1] & 127;
  for (unsigned i = 0; i < out.count; ++i) {
    if (n - pos < 4) return ParseError::Bounds;
    const auto *f = p + pos;
    if (!c.matches(f)) return ParseError::Codec;
    size_t length = c.frameBytes(f[2]);
    out.samples_per_frame = c.samples();
    if (length > n - pos) return ParseError::Bounds;
    out.frames[i] = {pos, length}; pos += length;
  }
  return pos == n ? ParseError::None : ParseError::Trailing;
}
struct Sequence {
  bool valid = false; uint16_t sequence = 0; uint32_t timestamp = 0, expected_timestamp = 0, ssrc = 0;
  uint64_t missing = 0, reordered = 0, timestamp_discontinuities = 0, source_changes = 0;
  bool accept(const PacketView &p) {
    if (valid) {
      if (p.ssrc != ssrc) { ++source_changes; return false; }
      uint16_t delta = uint16_t(p.sequence - sequence);
      if (!delta || delta >= 0x8000) { ++reordered; return false; }
      missing += delta - 1;
      if (p.timestamp != expected_timestamp) ++timestamp_discontinuities;
    }
    valid = true; sequence = p.sequence; timestamp = p.timestamp; ssrc = p.ssrc;
    expected_timestamp = p.timestamp + uint32_t(p.count) * p.samples_per_frame;
    return true;
  }
  void resetStream() { valid = false; }
};
} // namespace picow::sink
