#pragma once
#include "AvrcpTarget.h"
#include <cstdio>
struct Buttons {
  // Callback only records application intent. No blocking I/O or API mutation.
  bool stop_requested = false;
  unsigned operations = 0;
  picow::avrcp::Event last{}; // Last-event observation, not a lossless event log.
  static void receive(const picow::avrcp::Event& event, void* context) {
    auto& self = *static_cast<Buttons*>(context);
    self.last = event;
    if (event.kind == picow::avrcp::Kind::Operation) {
      ++self.operations;
      if (event.operation == 0x45 && event.pressed) self.stop_requested = true;
    }
    // If implementing held buttons, cancel holds on Disconnected or Reset.
  }
};
inline picow::avrcp::AvrcpTarget::Config remoteSettings(sample::Address peer) {
  picow::avrcp::AvrcpTarget::Config config;
  config.peers[0] = peer;
  config.peer_count = 1;
  return config;
}
inline void remoteStatus(const picow::avrcp::AvrcpTarget& target, const Buttons& buttons) {
  printf("last kind=%u operation=0x%02x pressed=%u result=%u (last event only)\n",
    unsigned(buttons.last.kind), buttons.last.operation, buttons.last.pressed, unsigned(buttons.last.result));
  printf("avrcp active=%u operations=%u errors=%lu/%lu last=%d\n",
    unsigned(target.core().status().active), buttons.operations,
    (unsigned long)target.status().connection_errors,
    (unsigned long)target.status().response_errors, target.status().last_error);
}
