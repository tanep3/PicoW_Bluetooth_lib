#include "PeerPairing.h"
#include <cassert>
#include <cstdio>
int main() {
  const BluetoothRuntime::Address peer{1,2,3,4,5,6};
  auto& runtime = BluetoothRuntime::instance();
  runtime.allowed = peer;
  PeerPairing::configure(peer, false);
  PeerPairing::poll();
  assert(!handler);
  runtime.initialized = true;
  PeerPairing::poll(); PeerPairing::poll();
  assert(handler && registrations == 1);
  uint8_t packet[12]{HCI_EVENT_IO_CAPABILITY_REQUEST, 6, 6,5,4,3,2,1};
  handler->callback(HCI_EVENT_PACKET,0,packet,7);
  handler->callback(0,0,packet,8);
  assert(io_yes == 0 && io_no == 0);
  handler->callback(HCI_EVENT_PACKET,0,packet,8);
  assert(io_no == 1);
  PeerPairing::configure(peer, true);
  handler->callback(HCI_EVENT_PACKET,0,packet,8);
  assert(io_yes == 1);
  packet[2] = 7;
  handler->callback(HCI_EVENT_PACKET,0,packet,8);
  assert(io_no == 2);
  packet[0] = HCI_EVENT_USER_CONFIRMATION_REQUEST;
  handler->callback(HCI_EVENT_PACKET,0,packet,11);
  assert(runtime.rejected == 0);
  runtime.permission = true;
  handler->callback(HCI_EVENT_PACKET,0,packet,12);
  assert(runtime.rejected == 1);
  packet[2] = 6;
  handler->callback(HCI_EVENT_PACKET,0,packet,12);
  assert(runtime.confirmations == 1);
  PeerPairing::disable();
  runtime.permission = false; // Standalone stop also revokes its runtime policy.
  handler->callback(HCI_EVENT_PACKET,0,packet,12);
  assert(runtime.rejected == 2);
  packet[0] = HCI_EVENT_IO_CAPABILITY_REQUEST;
  handler->callback(HCI_EVENT_PACKET,0,packet,8);
  assert(io_no == 3 && io_yes == 1);
  puts("PASS: deferred one-time registration, malformed events, fixed peer IO/SSP, disabled policy");
}
