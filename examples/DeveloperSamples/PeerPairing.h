#pragma once
#include "BluetoothRuntime.h"
// Application pairing support for ENABLE_EXPLICIT_IO_CAPABILITIES_REPLY.
// Register once after runtime initialization; call poll from the main loop.
class PeerPairing {
public:
  static void configure(const BluetoothRuntime::Address& peer, bool permit) {
    peer_ = peer;
    permit_ = permit;
  }
  static void poll() {
    if (!registered_ && BluetoothRuntime::instance().ownsRadio()) {
      registration_.callback = packet;
      hci_add_event_handler(&registration_); // Runtime handler already registered.
      registered_ = true;
    }
  }
  static void disable() { permit_ = false; }
private:
  inline static BluetoothRuntime::Address peer_{};
  inline static bool permit_ = false, registered_ = false;
  inline static btstack_packet_callback_registration_t registration_{};
  static void packet(uint8_t type, uint16_t, uint8_t* data, uint16_t size) {
    if (type != HCI_EVENT_PACKET || size < 2) return;
    const auto event = hci_event_packet_get_type(data);
    BluetoothRuntime::Address requested{};
    if (event == HCI_EVENT_IO_CAPABILITY_REQUEST && size >= 8) {
      hci_event_io_capability_request_get_bd_addr(data, requested.data());
      if (permit_ && requested == peer_) gap_ssp_io_capabilities_response(requested.data());
      else gap_ssp_io_capabilities_negative(requested.data());
    }
    if (event == HCI_EVENT_USER_CONFIRMATION_REQUEST && size >= 12) {
      hci_event_user_confirmation_request_get_bd_addr(data, requested.data());
      // The shared runtime requires a matching per-profile policy too. This
      // removes pending requests and rejects peers without permission.
      BluetoothRuntime::instance().confirmPairing(requested);
    }
  }
};
