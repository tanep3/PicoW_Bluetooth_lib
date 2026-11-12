#pragma once
#include "SppClient.h"

// One pending application message per route. No implicit retry across reconnect.
struct PendingMessage {
  spp::Handle owner{};
  size_t offset = 0;
  bool active = false;
  static constexpr char text[] = "Hello from Pico W\n";
  void poll(spp::Client& client, const spp::State& state, uint8_t slot) {
    if (!active) return;
    if (!spp::matches(state, owner) || state.routes[slot].phase != spp::Phase::Open) {
      active = false; // A new route generation must not receive an old message.
      return;
    }
    const auto result = client.tryWrite(owner,
      reinterpret_cast<const uint8_t*>(text) + offset, sizeof(text)-1-offset);
    offset += result.bytes; // Only bytes actually accepted may be advanced.
    // Busy means queued callbacks must be drained by the next main-loop poll.
    if (offset == sizeof(text)-1 ||
        (result.error != spp::Error::None && result.error != spp::Error::WouldBlock && result.error != spp::Error::Busy))
      active = false;
  }
};

