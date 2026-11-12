#pragma once
#include "../spp-core/SppCore.h"
#include "btstack.h"
#include <array>
#include <limits>

namespace spp {
// Caller-owned memory, process-lifetime. One lease is one RFCOMM multiplexer,
// not one DLC: two routes to a peer usually share one lease.
class ErtmPool {
public:
  static constexpr size_t Capacity = 2;
  struct Buffer { uint8_t *data = nullptr; uint32_t size = 0; };
  struct Config {
    l2cap_ertm_config_t link{0, 8, 2000, 12000, 1000, 4, 4, 0};
    std::array<Buffer, Capacity> buffers{};
    uint8_t count = 0;
  };
  struct Status {
    uint32_t requests = 0, granted = 0, released = 0, unavailable = 0;
    uint32_t foreign = 0, unexpected_release = 0;
    uint16_t last_requested_id = 0;
    uint8_t active = 0;
    bool configured = false;
  };
private:
  Config config_{};
  std::array<uint16_t, Capacity> owners_{};
  std::array<uint32_t, Capacity> usable_{};
  Status status_{};
public:
  static uint32_t safeSize(const l2cap_ertm_config_t &c, Buffer b) {
    if (!b.data || !b.size || !c.max_transmit || c.ertm_mandatory > 1 ||
        c.retransmission_timeout_ms < 2000 || c.monitor_timeout_ms < 12000 ||
        c.local_mtu < 48 || c.local_mtu > MaxFrame + 5 || c.fcs_option > 2 ||
        !c.num_rx_buffers || c.num_rx_buffers > 63 ||
        !c.num_tx_buffers || c.num_tx_buffers > 63 || HCI_ACL_PAYLOAD_SIZE < 56)
      return 0;
    // The pinned SDK always advances by 1..16 bytes, including an already
    // aligned pointer. Its setup must not underflow unsigned buffer_space.
    const uint32_t alignment = 16 - (reinterpret_cast<uintptr_t>(b.data) & 15);
    const uint32_t states = c.num_rx_buffers * sizeof(l2cap_ertm_rx_packet_state_t) +
                            c.num_tx_buffers * sizeof(l2cap_ertm_tx_packet_state_t);
    const uint32_t fixed = alignment + states + c.local_mtu;
    const uint32_t slots = c.num_rx_buffers + c.num_tx_buffers;
    if (b.size < fixed + slots * 48) return 0;
    // Bound information-frame writes to the actual shared HCI packet buffer:
    // L2CAP header 4 + enhanced control 2 + optional FCS 2.
    const uint32_t mps = std::min<uint32_t>((b.size - fixed) / slots,
                                           HCI_ACL_PAYLOAD_SIZE - 8);
    return fixed + slots * mps;
  }
  bool configure(const Config &c) {
    if (status_.configured || status_.active || !c.count || c.count > Capacity)
      return false;
    std::array<uint32_t, Capacity> sizes{};
    for (size_t i = 0; i < c.count; ++i) {
      sizes[i] = safeSize(c.link, c.buffers[i]);
      if (!sizes[i]) return false;
      const auto begin = reinterpret_cast<uintptr_t>(c.buffers[i].data);
      if (c.buffers[i].size > std::numeric_limits<uintptr_t>::max() - begin)
        return false;
      const auto end = begin + c.buffers[i].size;
      for (size_t j = 0; j < i; ++j) {
        const auto other = reinterpret_cast<uintptr_t>(c.buffers[j].data);
        if (begin < other + c.buffers[j].size && other < end) return false;
      }
    }
    config_ = c; usable_ = sizes; status_.configured = true;
    return true;
  }
  bool request(rfcomm_ertm_request_t &r, const Address &allowed) {
    ++status_.requests; status_.last_requested_id = r.ertm_id;
    Address peer{}; std::copy(r.addr, r.addr + 6, peer.begin());
    if (peer != allowed) { ++status_.foreign; return false; }
    if (!status_.configured || !r.ertm_id) { ++status_.unavailable; return false; }
    for (auto owner : owners_) if (owner == r.ertm_id) {
      ++status_.unavailable; return false;
    }
    for (size_t i = 0; i < config_.count; ++i) if (!owners_[i]) {
      owners_[i] = r.ertm_id; ++status_.active; ++status_.granted;
      r.ertm_config = &config_.link;
      r.ertm_buffer = config_.buffers[i].data; r.ertm_buffer_size = usable_[i];
      return true;
    }
    ++status_.unavailable; return false;
  }
  bool release(uint16_t id) {
    if (id) for (auto &owner : owners_) if (owner == id) {
      owner = 0; --status_.active; ++status_.released; return true;
    }
    ++status_.unexpected_release; return false;
  }
  bool owns(uint16_t id) const {
    if (!id) return false;
    for (auto owner : owners_) if (owner == id) return true;
    return false;
  }
  bool required() const { return status_.configured && config_.link.ertm_mandatory; }
  const Status &status() const { return status_; }
};
} // namespace spp
