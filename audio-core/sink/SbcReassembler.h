#pragma once
#include "MediaReceiver.h"

namespace picow::sink {
// A2DP 1.4.1 sections 4.3.3-4.3.4: one SBC frame, remaining-fragment
// countdown, common timestamp, equal nonfinal packet sizes. Additional bounded
// receiver policy: fixed CSRC list/SSRC/PT, no RTP padding or extension, 2..15
// nonempty fragments, maximum MaxSbcFrameBytes bytes; final frame is checked against negotiation.
// Input is borrowed. Completed output is owned here until the next receive().
class Reassembler {
public:
  void reset() { active_ = false; used_ = 0; }
  ParseError receive(const uint8_t *&p, size_t &n, const Config &config, PacketView &view) {
    view = {};
    // Find the SBC header without reading beyond the RTP packet. The existing
    // complete-frame parser remains authoritative for nonfragmented packets.
    if (!p || n < 13) return reject(ParseError::Short);
    const size_t header_size = 12 + 4 * size_t(p[0] & 15);
    if (header_size >= n) return reject(ParseError::Bounds);
    if (p[0] & 0x10) { reset(); return parse(p, n, config, view); }
    const uint8_t header = p[header_size];
    if (!(header & 0xe0)) { reset(); return parse(p, n, config, view); }
    // Reserved SBC bit4 is ignored on the new fragment path (A2DP 1.5.2).
    if ((p[0] >> 6) != 2 || (p[0] & 0x30) || (p[1] & 0x80) || (p[1] & 127) < 96) return reject(ParseError::Rtp);
    const bool first = header & 0x40, last = header & 0x20;
    const uint8_t remaining = header & 15;
    if (!(header & 0x80) || !remaining || last != (remaining == 1) ||
        (first && (last || remaining < 2))) return reject(ParseError::Fragmented);
    if (!config.valid()) return reject(ParseError::Codec);
    const size_t payload_size = n - header_size - 1;
    if (!payload_size || payload_size > MaxSbcFrameBytes) return reject(ParseError::Bounds);
    const uint16_t seq = be16(p + 2);
    const uint32_t timestamp = be32(p + 4), ssrc = be32(p + 8);
    if (first) {
      reset(); active_ = true; remaining_ = remaining; timestamp_ = timestamp;
      ssrc_ = ssrc; pt_ = p[1]; packet_size_ = n; header_size_ = header_size;
      std::memcpy(rtp_.data(), p, header_size);
    } else if (!active_ || remaining + 1 != remaining_ || uint16_t(sequence_ + 1) != seq ||
               timestamp != timestamp_ || ssrc != ssrc_ || p[1] != pt_ ||
               header_size != header_size_ || std::memcmp(p + 12, rtp_.data() + 12, header_size - 12) ||
               (!last && n != packet_size_) || (last && n > packet_size_)) {
      return reject(ParseError::Fragmented);
    }
    if (payload_size > MaxSbcFrameBytes - used_) return reject(ParseError::Bounds);
    std::memcpy(frame_.data() + used_, p + header_size + 1, payload_size);
    used_ += payload_size; remaining_ = remaining; sequence_ = seq;
    view.sequence = seq; view.timestamp = timestamp; view.ssrc = ssrc; view.payload_type = p[1] & 127;
    if (!last) return ParseError::None; // count=0: no PCM/timestamp advancement.
    // Reuse the strict existing whole-frame parser before the real decoder.
    // Synthetic RTP header is internal only, never emitted on a transport.
    complete_.fill(0); complete_[0] = 0x80; complete_[1] = pt_;
    std::memcpy(complete_.data() + 2, p + 2, 10); complete_[12] = 1;
    std::memcpy(complete_.data() + 13, frame_.data(), used_);
    n = used_ + 13; p = complete_.data(); reset();
    return parse(p, n, config, view);
  }
private:
  ParseError reject(ParseError error) { reset(); return error; }
  bool active_ = false;
  uint8_t remaining_ = 0, pt_ = 0;
  uint16_t sequence_ = 0;
  uint32_t timestamp_ = 0, ssrc_ = 0;
  size_t used_ = 0, packet_size_ = 0, header_size_ = 0;
  std::array<uint8_t, MaxSbcFrameBytes> frame_{};
  std::array<uint8_t, 72> rtp_{};
  std::array<uint8_t, MaxSbcFrameBytes + 13> complete_{};
};
} // namespace picow::sink
