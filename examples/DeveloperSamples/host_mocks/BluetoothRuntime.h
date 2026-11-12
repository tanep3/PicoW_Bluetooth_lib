#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
constexpr uint8_t HCI_EVENT_PACKET = 4, HCI_EVENT_IO_CAPABILITY_REQUEST = 0x31,
  HCI_EVENT_USER_CONFIRMATION_REQUEST = 0x33;
struct btstack_packet_callback_registration_t {
  void (*callback)(uint8_t, uint16_t, uint8_t*, uint16_t) = nullptr;
};
inline btstack_packet_callback_registration_t* handler = nullptr;
inline unsigned registrations = 0, io_yes = 0, io_no = 0;
inline void hci_add_event_handler(btstack_packet_callback_registration_t* h) { handler=h; ++registrations; }
inline uint8_t hci_event_packet_get_type(const uint8_t* p) { return p[0]; }
inline void address(const uint8_t* p, uint8_t* out) { std::reverse_copy(p+2,p+8,out); }
inline void hci_event_io_capability_request_get_bd_addr(const uint8_t* p, uint8_t* out) { address(p,out); }
inline void hci_event_user_confirmation_request_get_bd_addr(const uint8_t* p, uint8_t* out) { address(p,out); }
inline int gap_ssp_io_capabilities_response(const uint8_t*) { ++io_yes; return 0; }
inline int gap_ssp_io_capabilities_negative(const uint8_t*) { ++io_no; return 0; }
class BluetoothRuntime {
public:
  using Address = std::array<uint8_t,6>;
  bool initialized = false, permission = false;
  Address allowed{};
  unsigned confirmations = 0, rejected = 0;
  static BluetoothRuntime& instance() { static BluetoothRuntime runtime; return runtime; }
  bool ownsRadio() const { return initialized; }
  void confirmPairing(const Address& peer) {
    if (permission && peer == allowed) ++confirmations;
    else ++rejected;
  }
};
