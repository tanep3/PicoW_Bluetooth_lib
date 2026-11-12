#pragma once
#include "../../spp-core/SppCore.h"

namespace framed_sample {
// Application protocol only. No change to the library's RAW byte streams.
enum class Role : uint8_t { Control, Data, Unknown };
enum class Mode : uint8_t { Unselected, Outgoing, Incoming };
constexpr size_t Capacity = 64;
struct RouteState {
  spp::Handle handle{};
  Role role = Role::Unknown;
  std::array<uint8_t, Capacity> tx{}, rx{};
  size_t tx_size = 0, tx_offset = 0, rx_size = 0;
  bool invalid = false;
  uint32_t controls = 0, temperatures = 0, vectors = 0;
  uint16_t last_command = 0, last_temperature = 0;
  std::array<uint16_t, 3> last_vector{};
};
inline uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
// v1, type, payload length LE16; deliberately tiny, fixed application messages.
inline constexpr uint8_t ControlFrame[]{1, 1, 2, 0, 7, 0}; // marker command 7
inline constexpr uint8_t DataFrames[]{1, 2, 2, 0, 0xc4, 9, // 2500 centi-degrees
                                     1, 3, 6, 0, 10, 0, 20, 0, 30, 0};
template<class ClientLike> class Workflow {
  ClientLike &client_;
  uint8_t control_channel_, data_channel_;
  Mode mode_ = Mode::Unselected;
  uint8_t turn_ = 0;
  std::array<RouteState, spp::RouteCount> routes_{};
  std::array<uint32_t, spp::RouteCount> stopped_generation_{};
  uint64_t discarded_tx_ = 0, discarded_rx_ = 0, invalid_frames_ = 0;
  void clear(RouteState &r) {
    discarded_tx_ += r.tx_size - r.tx_offset;
    discarded_rx_ += r.rx_size;
    r = RouteState{};
  }
  void parse(RouteState &r, uint8_t byte) {
    if (r.invalid) return;
    r.rx[r.rx_size++] = byte;
    if (r.rx_size < 4) return;
    const auto type = r.rx[1]; const auto length = u16(r.rx.data() + 2);
    const bool valid = r.rx[0] == 1 &&
      ((r.role == Role::Control && type == 1 && length == 2) ||
       (r.role == Role::Data && ((type == 2 && length == 2) || (type == 3 && length == 6))));
    if (!valid) { r.invalid = true; ++invalid_frames_; return; }
    if (r.rx_size != 4u + length) return;
    if (type == 1) { r.last_command = u16(r.rx.data()+4); ++r.controls; }
    if (type == 2) { r.last_temperature = u16(r.rx.data()+4); ++r.temperatures; }
    if (type == 3) {
      for (size_t i=0; i<3; ++i) r.last_vector[i] = u16(r.rx.data()+4+2*i);
      ++r.vectors;
    }
    r.rx_size = 0;
  }
public:
  Workflow(ClientLike &client, uint8_t control_channel=1, uint8_t data_channel=2)
    : client_(client), control_channel_(control_channel), data_channel_(data_channel) {}
  bool selectMode(Mode mode) {
    if (mode == Mode::Unselected || !spp::validChannel(control_channel_) ||
        !spp::validChannel(data_channel_) || control_channel_ == data_channel_) return false;
    if (mode_ != Mode::Unselected && mode_ != mode) return false;
    mode_ = mode; return true;
  }
  Mode mode() const { return mode_; }
  // Call after each client.poll(), before handling user commands or I/O.
  void observe() {
    const auto s = client_.snapshot();
    for (uint8_t i=0; i<spp::RouteCount; ++i) {
      auto &r=routes_[i]; const auto &actual=s.routes[i];
      if (r.handle.generation != actual.generation || actual.phase != spp::Phase::Open) clear(r);
      if (actual.phase != spp::Phase::Open || mode_ == Mode::Unselected) continue;
      r.handle=spp::handleOf(s,i);
      if(stopped_generation_[i] == actual.generation) r.invalid=true;
      // Mode is exclusive for the entire process. Thus channel is unambiguously
      // remote in Outgoing and local in Incoming; endpoint index is irrelevant.
      r.role=actual.channel == control_channel_ ? Role::Control :
             actual.channel == data_channel_ ? Role::Data : Role::Unknown;
    }
  }
  bool enqueue(Role role) {
    for (auto &r:routes_) if (r.role == role && !r.invalid && r.handle.generation) {
      if (r.tx_offset != r.tx_size) return false;
      const auto *p = role == Role::Control ? ControlFrame : DataFrames;
      const auto n = role == Role::Control ? sizeof(ControlFrame) : sizeof(DataFrames);
      std::copy(p,p+n,r.tx.begin()); r.tx_size=n; r.tx_offset=0; return true;
    }
    return false; // No queued replay for a future connection.
  }
  void discardApplication() {
    const auto s=client_.snapshot();
    for(uint8_t i=0;i<spp::RouteCount;++i) {
      if(s.routes[i].phase != spp::Phase::Closed)
        stopped_generation_[i]=s.routes[i].generation;
      clear(routes_[i]);
    }
    // Abort is latched to the generation even if disconnect cannot be queued,
    // is delayed, or fails. Never resume midway through a frame on that stream.
  }
  void io() {
    // At most one read and write per route per outer poll. No busy retry loops.
    const uint8_t first=turn_ & 1;
    const bool write_first=(turn_ & 2) != 0;
    turn_=uint8_t((turn_+1) & 3);
    for (uint8_t index=0; index<spp::RouteCount; ++index) {
      auto &r=routes_[(first+index)%spp::RouteCount];
      if (r.role == Role::Unknown || r.invalid) continue;
      auto write=[&]() {
        if (r.invalid || r.tx_offset == r.tx_size) return;
        const auto sent=client_.tryWrite(r.handle,r.tx.data()+r.tx_offset,r.tx_size-r.tx_offset);
        r.tx_offset += sent.bytes; // Only accepted bytes advance.
        if (sent.error != spp::Error::None && sent.error != spp::Error::Busy &&
            sent.error != spp::Error::WouldBlock) r.invalid=true;
      };
      auto read=[&]() {
        if(r.invalid) return;
        std::array<uint8_t,32> bytes{};
        const auto got=client_.read(r.handle,bytes.data(),bytes.size());
        for(size_t i=0; i<got.bytes; ++i) parse(r,bytes[i]);
        if(got.error != spp::Error::None && got.error != spp::Error::Busy &&
           got.error != spp::Error::WouldBlock) r.invalid=true;
      };
      // Credit/send callbacks can make subsequent calls Busy. Rotate both
      // the first route and first direction over four polls, so neither has
      // a permanently privileged position. No radio fairness guarantee.
      if(write_first) { write(); read(); } else { read(); write(); }
    }
  }

  const auto &routes() const { return routes_; }
  uint64_t discardedTx() const { return discarded_tx_; }
  uint64_t discardedRx() const { return discarded_rx_; }
  uint64_t invalidFrames() const { return invalid_frames_; }
};
} // namespace framed_sample
