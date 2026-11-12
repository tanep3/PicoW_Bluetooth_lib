#pragma once
#include "../bluetooth-runtime/BluetoothRuntime.h"
#include "classic/avrcp.h"
#include "classic/avrcp_controller.h"
#include "classic/avrcp_target.h"
#include "classic/sdp_client.h"
#include "AvrcpSdkExtensions.h"

namespace picow::avrcp {
// SDK AVRCP is one signalling service with two roles. Never call avrcp_init
// again when registering the second role: it clears the SDK connection list.
class AvrcpShared {
public:
  enum Role : unsigned { Target, Controller };
  struct Owner {
    btstack_packet_handler_t connection = nullptr, hci = nullptr;
    bool (*accepts)(const BluetoothRuntime::Address &) = nullptr;
    bool (*uses)(uint16_t) = nullptr;
  };
  static AvrcpShared &instance() {
    static AvrcpShared s;
    return s;
  }
  bool initialized() const { return initialized_; }
  int attach(Role role, Owner owner) {
    if (role_claimed_[role])
      return -1;
    role_claimed_[role] = true;
    auto &runtime = BluetoothRuntime::instance();
    if (!initialized_) {
      if (claimed_)
        return -1;
      claimed_ = true;
      if (!runtime.claim(BluetoothRuntime::Profile::Avrcp))
        return -1;
      const int status = runtime.initialize();
      if (status)
        return status;
      runtime.setSecurityLevel(BluetoothRuntime::Profile::Avrcp, 2);
      avrcp_init();
      // Outgoing avrcp_connect requires both internal SDK role handlers.
      avrcp_target_init();
      avrcp_controller_init();
      avrcp_target_register_packet_handler(ignore);
      avrcp_controller_register_packet_handler(ignore);
      avrcp_register_packet_handler(connection);
      runtime.setHandler(BluetoothRuntime::Profile::Avrcp, hci);
      initialized_ = true;
    }
    runtime.setHandler(BluetoothRuntime::Profile::Avrcp, hci);
    owners_[role] = owner;
    return 0;
  }
  void detach(Role role) {
    owners_[role] = {};
    if (!owners_[Target].connection && !owners_[Controller].connection)
      BluetoothRuntime::instance().setHandler(BluetoothRuntime::Profile::Avrcp,
                                              nullptr);
  }
  bool accepted(const BluetoothRuntime::Address &peer) const {
    for (const auto &o : owners_)
      if (o.accepts && o.accepts(peer))
        return true;
    return false;
  }
  int disconnectUnlessShared(Role role, uint16_t cid) {
    const auto &other = owners_[role == Target ? Controller : Target];
    return other.uses && other.uses(cid) ? 0 : avrcp_disconnect(cid);
  }
  int cancelConnection(const BluetoothRuntime::Address &peer) {
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
    auto address = peer;
    auto *c = avrcp_get_connection_for_bd_addr_for_role(AVRCP_CONTROLLER, address.data());
    if (!c) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    if (c->state >= AVCTP_CONNECTION_OPENED && !picow_avrcp_cancel_pending(c->avrcp_cid))
      return ERROR_CODE_COMMAND_DISALLOWED;
    return picow_avrcp_cancel(c->avrcp_cid);
#else
    (void)peer; return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
#endif
  }
  bool cancellationPending(const BluetoothRuntime::Address &peer) const {
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
    auto address = peer;
    auto *c = avrcp_get_connection_for_bd_addr_for_role(AVRCP_CONTROLLER, address.data());
    return c && picow_avrcp_cancel_pending(c->avrcp_cid);
#else
    (void)peer; return false;
#endif
  }
  int cancellationStatus(const BluetoothRuntime::Address &peer) const {
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
    auto address = peer;
    auto *c = avrcp_get_connection_for_bd_addr_for_role(AVRCP_CONTROLLER, address.data());
    return c ? picow_avrcp_cancel_status(c->avrcp_cid) : 0;
#else
    (void)peer; return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
#endif
  }
  int connect(const BluetoothRuntime::Address &peer) {
    auto &runtime = BluetoothRuntime::instance();
    if (runtime.failed() || runtime.sdpOwned() || !sdp_client_ready())
      return SDP_QUERY_BUSY;
    auto address = peer;
    uint16_t cid = 0;
    // SDK queues its own serialized SDP callback; shared SPP observes SDK busy.
    return avrcp_connect(address.data(), &cid);
  }

private:
  bool claimed_ = false, initialized_ = false;
  std::array<Owner, 2> owners_{};
  std::array<bool, 2> role_claimed_{};
  static void ignore(uint8_t, uint16_t, uint8_t *, uint16_t) {}
  static void connection(uint8_t t, uint16_t c, uint8_t *p, uint16_t n) {
    // One admission decision for the shared channel, not two independent
    // disconnect requests from role owners with disjoint peer allowlists.
    if (t == HCI_EVENT_PACKET && p && n >= 14 && unsigned(p[1]) + 2 == n &&
        p[0] == HCI_EVENT_AVRCP_META &&
        p[2] == AVRCP_SUBEVENT_CONNECTION_ESTABLISHED &&
        !avrcp_subevent_connection_established_get_status(p)) {
      BluetoothRuntime::Address peer{};
      avrcp_subevent_connection_established_get_bd_addr(p, peer.data());
      if (!instance().accepted(peer)) {
        const auto id = avrcp_subevent_connection_established_get_avrcp_cid(p);
        if (id)
          avrcp_disconnect(id);
        return;
      }
    }
    for (auto o : instance().owners_)
      if (o.connection)
        o.connection(t, c, p, n);
  }
  static void hci(uint8_t t, uint16_t c, uint8_t *p, uint16_t n) {
    for (auto o : instance().owners_)
      if (o.hci)
        o.hci(t, c, p, n);
  }
};
} // namespace picow::avrcp
