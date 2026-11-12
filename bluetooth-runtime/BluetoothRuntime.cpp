#include "BluetoothRuntime.h"
#include "pico/cyw43_arch.h"
#include <algorithm>

BluetoothRuntime &BluetoothRuntime::instance() {
  static BluetoothRuntime runtime;
  return runtime;
}
bool BluetoothRuntime::setLinkKeyDatabase(const btstack_link_key_db_t *database) {
  if (link_key_db_locked_ || initialized_ || failed_ ||
      cyw43_is_initialized(&cyw43_state))
    return false;
  for (bool claimed : claimed_)
    if (claimed)
      return false;
  if (database) {
    if (!database->open || !database->set_local_bd_addr || !database->close ||
        !database->get_link_key || !database->put_link_key ||
        !database->delete_link_key)
      return false;
    const bool any_iterator = database->iterator_init ||
                              database->iterator_get_next ||
                              database->iterator_done;
    const bool full_iterator = database->iterator_init &&
                               database->iterator_get_next &&
                               database->iterator_done;
    if (any_iterator && !full_iterator)
      return false;
  }
  link_key_db_ = database;
  return true;
}
bool BluetoothRuntime::claim(Profile profile) {
  const auto i = static_cast<unsigned>(profile);
  if (i >= ProfileCount || failed_ || claimed_[i] ||
      (!initialized_ && cyw43_is_initialized(&cyw43_state)))
    return false;
  claimed_[i] = true;
  return true;
}
int BluetoothRuntime::initialize() {
  if (failed_)
    return -1;
  if (initialized_)
    return 0;
  if (link_key_db_locked_)
    return -1;
  // Lock before SDK initialization or the database's open callback can run.
  link_key_db_locked_ = true;
  if (cyw43_is_initialized(&cyw43_state)) {
    failed_ = true;
    return -1;
  }
  const int status = cyw43_arch_init();
  if (status) {
    failed_ = true;
    return status;
  }
  hci_set_link_key_db(link_key_db_ ? link_key_db_
                                 : btstack_link_key_db_memory_instance());
  hci_set_master_slave_policy(0);
  hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);
  gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
  gap_ssp_set_auto_accept(0);
  gap_set_local_name("PicoW Bluetooth 00:00:00:00:00:00");
  gap_set_class_of_device(0x200408);
  gap_discoverable_control(0);
  gap_connectable_control(0);
  l2cap_init();
  sdp_init();
  registration_.callback = hciPacket;
  hci_add_event_handler(&registration_);
  initialized_ = true;
  return 0;
}
void BluetoothRuntime::setHandler(Profile p, btstack_packet_handler_t handler) {
  const auto i = static_cast<unsigned>(p);
  if (i < ProfileCount)
    handlers_[i] = handler;
}
void BluetoothRuntime::setPairingPolicy(Profile p, Address peer, bool allow) {
  const auto i = static_cast<unsigned>(p);
  if (i >= ProfileCount)
    return;
  pairing_peers_[i] = peer;
  allow_confirmation_[i] = allow;
}
void BluetoothRuntime::setSecurityLevel(Profile p, uint8_t level) {
  const auto index = static_cast<unsigned>(p);
  if (index >= ProfileCount)
    return;
  security_[index] = std::min<uint8_t>(level, 4);
  uint8_t required = 0;
  for (unsigned i = 0; i < ProfileCount; ++i)
    if (claimed_[i])
      required = std::max(required, security_[i]);
  if (initialized_)
    gap_set_security_level(static_cast<gap_security_level_t>(required));
}
void BluetoothRuntime::confirmPairing(const Address &peer) {
  size_t pending = confirmation_count_;
  for (size_t i = 0; i < confirmation_count_; ++i)
    if (confirmations_[i] == peer) {
      pending = i;
      break;
    }
  if (pending == confirmation_count_)
    return;
  bool allow = false;
  for (unsigned i = 0; i < ProfileCount; ++i)
    allow |= claimed_[i] && allow_confirmation_[i] && pairing_peers_[i] == peer;
  auto address = peer;
  confirmations_[pending] = confirmations_[--confirmation_count_];
  if (allow)
    gap_ssp_confirmation_response(address.data());
  else
    gap_ssp_confirmation_negative(address.data());
}
int BluetoothRuntime::powerOn() {
  if (!initialized_ || failed_)
    return -1;
  if (powered_)
    return 0;
  auto status = hci_power_control(HCI_POWER_ON);
  if (!status)
    powered_ = true;
  return status;
}
int BluetoothRuntime::powerOff() {
  if (!initialized_)
    return -1;
  auto status = hci_power_control(HCI_POWER_OFF);
  if (!status)
    powered_ = false;
  return status;
}
void BluetoothRuntime::poll() {
  if (initialized_) {
    cyw43_arch_poll();
    while (confirmation_count_)
      confirmPairing(confirmations_[0]);
  }
}
BluetoothRuntime::Link
BluetoothRuntime::linkForPeer(const Address &peer) const {
  for (const auto &l : links_)
    if (l.active && l.peer == peer)
      return l;
  return {};
}
BluetoothRuntime::Link BluetoothRuntime::linkForHandle(uint16_t handle) const {
  for (const auto &l : links_)
    if (l.active && l.handle == handle)
      return l;
  return {};
}
void BluetoothRuntime::hciPacket(uint8_t type, uint16_t channel, uint8_t *p,
                                 uint16_t size) {
  auto &r = instance();
  if (type != HCI_EVENT_PACKET || !p || size < 2 || size < unsigned(p[1]) + 2)
    return;
  // Validate accessors against the declared event, not trailing transport bytes.
  size = static_cast<uint16_t>(p[1]) + 2;
  const auto kind = p[0];
  // AVRCP meta events belong to the dedicated AVRCP callback, never the
  // shared HCI broadcast (other profiles may interpret subevent numbers).
  if (kind == HCI_EVENT_AVRCP_META)
    return;
  if (kind == BTSTACK_EVENT_STATE && size >= 3 &&
      btstack_event_state_get_state(p) == HCI_STATE_OFF) {
    // Radio-wide shutdown invalidates every ACL even if no individual
    // disconnect event arrived. Notify profiles below after invalidation.
    r.links_ = {};
    r.confirmation_count_ = 0;
    r.powered_ = false;
  }
  Link *closing = nullptr;
  if (kind == HCI_EVENT_CONNECTION_COMPLETE && size >= 13 &&
      !hci_event_connection_complete_get_status(p) &&
      hci_event_connection_complete_get_link_type(p) == 1) {
    Address peer{};
    hci_event_connection_complete_get_bd_addr(p, peer.data());
    auto handle = hci_event_connection_complete_get_connection_handle(p);
    if (!r.linkForHandle(handle).active) {
      Link *slot = nullptr;
      for (auto &l : r.links_)
        if (!l.active) {
          slot = &l;
          break;
        }
      if (!slot || r.epoch_ == UINT32_MAX) {
        r.failed_ = true;
        r.powerOff();
        return;
      }
      *slot = {peer, ++r.epoch_, handle, true};
    }
  }
  if (kind == HCI_EVENT_DISCONNECTION_COMPLETE && size >= 6 &&
      !hci_event_disconnection_complete_get_status(p)) {
    auto handle = hci_event_disconnection_complete_get_connection_handle(p);
    for (auto &l : r.links_)
      if (l.active && l.handle == handle) {
        closing = &l;
        break;
      }
  }
  if (kind == HCI_EVENT_USER_CONFIRMATION_REQUEST && size >= 12) {
    Address peer{};
    hci_event_user_confirmation_request_get_bd_addr(p, peer.data());
    if (r.confirmation_count_ < r.confirmations_.size())
      r.confirmations_[r.confirmation_count_++] = peer;
    else
      gap_ssp_confirmation_negative(peer.data());
  }
  if (kind == HCI_EVENT_PIN_CODE_REQUEST && size >= 8) {
    // Legacy PIN pairing is unsupported by this shared runtime.
    Address peer{};
    hci_event_pin_code_request_get_bd_addr(p, peer.data());
    gap_pin_code_negative(peer.data());
  }
  for (auto handler : r.handlers_)
    if (handler)
      handler(type, channel, p, size);
  if (closing)
    closing->active = false;
}
int BluetoothRuntime::startSdp(uint32_t token, const Address &peer,
                               const std::array<uint8_t, 16> &uuid,
                               btstack_packet_handler_t callback) {
  if (!token || !callback || !initialized_ || failed_)
    return -1;
  if (sdp_token_ || !sdp_client_ready())
    return SDP_QUERY_BUSY;
  sdp_token_ = token;
  sdp_callback_ = callback;
  sdp_cancelled_ = false;
  auto address = peer;
  const int status =
      sdp_client_query_uuid128(sdpPacket, address.data(), uuid.data());
  if (status) {
    // BTstack sets W4_CONNECT before allocating its L2CAP channel. An
    // allocation error can return without a completion callback, leaving the
    // singleton client permanently busy. Do not pretend it has drained, or
    // reset its private state. Fail the entire shared runtime instead.
    if (status != SDP_QUERY_BUSY && !sdp_client_ready()) {
      failed_ = true;
      sdp_cancelled_ = true;
      powerOff();
    } else {
      sdp_token_ = 0;
      sdp_callback_ = nullptr;
    }
  }
  return status;
}
void BluetoothRuntime::cancelSdp(uint32_t token) {
  if (sdp_token_ == token)
    sdp_cancelled_ = true;
}
void BluetoothRuntime::sdpPacket(uint8_t type, uint16_t channel, uint8_t *p,
                                 uint16_t size) {
  auto &r = instance();
  if (!r.sdp_token_ || !r.sdp_callback_ || type != HCI_EVENT_PACKET || !p ||
      size < 2 || size < unsigned(p[1]) + 2)
    return;
  const bool complete = p[0] == SDP_EVENT_QUERY_COMPLETE && size >= 3;
  if (!r.sdp_cancelled_ || complete)
    r.sdp_callback_(type, channel, p, size);
  if (complete) {
    r.sdp_token_ = 0;
    r.sdp_callback_ = nullptr;
    r.sdp_cancelled_ = false;
  }
}
