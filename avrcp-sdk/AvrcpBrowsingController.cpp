#include "AvrcpBrowsingController.h"
#include "AvrcpShared.h"
#include "classic/avrcp_browsing.h"
#include "classic/avrcp_browsing_controller.h"
#include "classic/sdp_client.h"
#if !defined(ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE)
#error AVRCP browsing requires ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#endif
extern "C" bool picow_avrcp_browsing_service_ready(void);
namespace picow::avrcp {
namespace {
using B = AvrcpBrowsingController::Binding;
struct Deadline {
  uint32_t budget = 0, start = 0;
  bool armed = false;
  void reset(uint32_t ms = 0) {
    budget = ms;
    armed = false;
  }
  bool expired(uint32_t now) {
    if (!budget)
      return false;
    if (!armed) {
      start = now;
      armed = true;
      return false;
    }
    return uint32_t(now - start) >= budget;
  }
};
struct Slot {
  B binding{};
  bool connected = false, closing = false, pending = false;
  bool close_retry = false;
  Deadline connection_deadline, response_deadline, target_deadline,
      assembly_deadline;
  uint8_t pdu = 0;
  uint32_t usable = 0;
  alignas(4) std::array<uint8_t, 10000> ertm{};
  BrowsingResponse response;
  BrowsingCommand command;
  BrowsingReply reply;
  bool request_waiting = false, tx_retry = false;
  uint32_t request_sequence = 0;
  uint8_t request_label = 0, request_pdu = 0;
  uint16_t tx_mtu = 0;
};
std::array<Slot, 2> slots;
AvrcpBrowsingController *owner = nullptr;
bool claimed = false;
uint32_t generation = 0;
uint32_t request_sequence = 0;
btstack_packet_handler_t sdk_handler = nullptr;
l2cap_ertm_config_t ertm_config = {1, 2, 2000, 12000, 512, 4, 4, 1};
Slot *byCid(uint16_t cid) {
  for (auto &s : slots)
    if (cid && s.binding.cid == cid)
      return &s;
  return nullptr;
}
Slot *byBinding(const B &b) {
  auto *s = byCid(b.cid);
  if (!s || s->binding.peer != b.peer || s->binding.epoch != b.epoch ||
      s->binding.generation != b.generation)
    return nullptr;
  auto link = BluetoothRuntime::instance().linkForPeer(b.peer);
  return link.active && link.epoch == b.epoch ? s : nullptr;
}
Slot *vacant() {
  for (auto &s : slots)
    if (!s.binding.cid)
      return &s;
  return nullptr;
}
void clear(Slot &s) {
  s.binding = {};
  s.connected = s.closing = s.pending = false;
  s.close_retry = false;
  s.connection_deadline.reset();
  s.response_deadline.reset();
  s.target_deadline.reset();
  s.assembly_deadline.reset();
  s.pdu = 0;
  s.response.reset();
  s.command.reset();
  s.reply.reset();
  s.request_waiting = s.tx_retry = false;
  s.request_sequence = 0;
}
uint32_t safeSize(Slot &s) {
  const uint32_t fixed = 16 -
                         (reinterpret_cast<uintptr_t>(s.ertm.data()) & 15) +
                         4 * sizeof(l2cap_ertm_rx_packet_state_t) +
                         4 * sizeof(l2cap_ertm_tx_packet_state_t) + 512;
  if (HCI_ACL_PAYLOAD_SIZE < 56 || s.ertm.size() < fixed + 8 * 48)
    return 0;
  const uint32_t mps =
      std::min<uint32_t>((s.ertm.size() - fixed) / 8, HCI_ACL_PAYLOAD_SIZE - 8);
  return fixed + 8 * mps;
}
bool attributesValid(uint32_t b) {
  return b == AVRCP_MEDIA_ATTR_NONE || (b & ~0x1feu) == 0;
}
} // namespace
bool AvrcpBrowsingController::allowed(const Address &a) const {
  for (unsigned i = 0; i < config_.peer_count; ++i)
    if (config_.peers[i] == a)
      return true;
  return false;
}
bool AvrcpBrowsingController::begin(const Config &c, bool enable_target) {
  if (claimed || !c.peer_count || c.peer_count > 2 ||
      (!AvrcpShared::instance().initialized() ||
       BluetoothRuntime::instance().failed()))
    return false;
  for (unsigned i = 0; i < c.peer_count; ++i)
    if (c.peers[i] == Address{} || (i && c.peers[i] == c.peers[0]))
      return false;
  for (auto &s : slots) {
    s.usable = safeSize(s);
    if (!s.usable)
      return false;
  }
  claimed = true;
  owner = this;
  config_ = c;
  target_enabled_ = enable_target;
  started_ = true;
  avrcp_browsing_init();
  if (!picow_avrcp_browsing_service_ready()) {
    started_ = false;
    owner = nullptr;
    return false;
  }
  avrcp_browsing_controller_init();
  sdk_handler = avrcp_controller_context.browsing_packet_handler;
  avrcp_browsing_register_controller_packet_handler(transport);
  avrcp_browsing_register_target_packet_handler(target);
  avrcp_browsing_controller_register_packet_handler(ignore);
  avrcp_browsing_register_packet_handler(connection);
  return true;
}
void AvrcpBrowsingController::setCallback(void (*cb)(const Event &, void *),
                                          void *c) {
  if (!dispatching_) {
    callback_ = cb;
    context_ = c;
  }
}
void AvrcpBrowsingController::emit(const Event &e) {
  if (callback_) {
    dispatching_ = true;
    callback_(e, context_);
    dispatching_ = false;
  }
}
AvrcpBrowsingController::Binding
AvrcpBrowsingController::bindingForPeer(const Address &a) const {
  for (const auto &s : slots)
    if (s.connected && !s.closing && s.binding.peer == a &&
        byBinding(s.binding))
      return s.binding;
  return {};
}
int AvrcpBrowsingController::connect(const Address &a) {
  if (!started_ || dispatching_ || !allowed(a))
    return ERROR_CODE_COMMAND_DISALLOWED;
  for (auto &s : slots)
    if (s.binding.cid && s.binding.peer == a)
      return ERROR_CODE_COMMAND_DISALLOWED;
  auto link = BluetoothRuntime::instance().linkForPeer(a);
  auto *s = vacant();
  if (!link.active || !s)
    return ERROR_CODE_COMMAND_DISALLOWED;
  if (BluetoothRuntime::instance().sdpOwned() || !sdp_client_ready())
    return SDP_QUERY_BUSY;
  s->binding = {a, link.epoch, ++generation, 0};
  s->connection_deadline.reset(connection_timeout_ms_);
  auto addr = a;
  uint16_t cid = 0;
  int rc = avrcp_browsing_connect(addr.data(), s->ertm.data(), s->usable,
                                  &ertm_config, &cid);
  // A nonzero returned CID means SDK has retained ERTM storage, even on local
  // failure.
  s->binding.cid = cid;
  if (rc && !cid)
    clear(*s);
  return rc;
}
int AvrcpBrowsingController::disconnect(const Binding &b) {
  auto *s = byBinding(b);
  if (!started_ || dispatching_ || !s || s->closing)
    return ERROR_CODE_COMMAND_DISALLOWED;
  int rc = avrcp_browsing_disconnect(b.cid);
  if (!rc)
    s->closing = true;
  return rc;
}
void AvrcpBrowsingController::stop() {
  if (dispatching_ || owner != this)
    return;
  started_ = false;
  for (auto &s : slots)
    if (s.binding.cid && !s.closing) {
      s.closing = true;
      avrcp_browsing_disconnect(s.binding.cid);
    }
  owner = nullptr;
  // Callbacks and ERTM buffers remain alive until asynchronous close; no
  // deinit.
}
bool AvrcpBrowsingController::setTimeouts(uint32_t connection_ms,
                                          uint32_t response_ms,
                                          uint32_t target_ms,
                                          uint32_t assembly_ms) {
  if (dispatching_ || connection_ms > 0x7fffffffu ||
      response_ms > 0x7fffffffu || target_ms > 0x7fffffffu ||
      assembly_ms > 0x7fffffffu)
    return false;
  connection_timeout_ms_ = connection_ms;
  response_timeout_ms_ = response_ms;
  target_timeout_ms_ = target_ms;
  assembly_timeout_ms_ = assembly_ms;
  return true;
}
void AvrcpBrowsingController::poll() { pollImpl(false, 0); }
void AvrcpBrowsingController::poll(uint32_t now_ms) { pollImpl(true, now_ms); }
void AvrcpBrowsingController::pollImpl(bool timed, uint32_t now_ms) {
  if (dispatching_ || !started_)
    return;
  BluetoothRuntime::instance().poll();
  for (auto &s : slots) {
    if (!s.binding.cid)
      continue;
    if (timed && !s.closing && byBinding(s.binding)) {
      const bool connection_expired =
          !s.connected && s.connection_deadline.expired(now_ms);
      const bool response_expired =
          s.pending && s.response_deadline.expired(now_ms);
      const bool target_expired = (s.request_waiting || s.reply.active()) &&
                                  s.target_deadline.expired(now_ms);
      const bool assembly_expired = s.command.assembling() &&
                                    s.assembly_deadline.expired(now_ms);
      if (connection_expired || response_expired || target_expired ||
          assembly_expired) {
        // Quarantine the shared CT/TG channel before notification. Never reuse
        // labels, request tokens or retained ERTM storage until SDK release.
        s.closing = s.close_retry = true;
        emit({Kind::Error, s.binding, Timeout,
              connection_expired ? uint8_t(0)
                                 : (response_expired ? s.pdu
                                    : (target_expired ? s.request_pdu
                                                      : s.command.pdu()))});
      }
    }
    if (s.close_retry) {
      const auto b = s.binding;
      int rc = avrcp_browsing_disconnect(b.cid);
      if (s.binding.cid == b.cid && s.binding.generation == b.generation)
        s.close_retry = rc != 0;
    }
  }
  for (auto &s : slots)
    if (s.tx_retry && s.reply.active() && !s.closing && byBinding(s.binding)) {
      auto *c = avrcp_get_connection_for_browsing_cid_for_role(AVRCP_TARGET,
                                                               s.binding.cid);
      if (c && c->browsing_connection) {
        s.tx_retry = false;
        avrcp_browsing_request_can_send_now(
            c->browsing_connection, c->browsing_connection->l2cap_browsing_cid);
      }
    }

  for (auto &s : slots)
    if (s.binding.cid && !byBinding(s.binding) && !s.closing) {
      s.closing = true;
      avrcp_browsing_disconnect(s.binding.cid);
    }
}
int AvrcpBrowsingController::ready(const Binding &b, uint8_t pdu) {
  auto *s = byBinding(b);
  if (!started_ || dispatching_ || !s || !s->connected || s->closing ||
      s->pending)
    return ERROR_CODE_COMMAND_DISALLOWED;
  auto *c =
      avrcp_get_connection_for_browsing_cid_for_role(AVRCP_CONTROLLER, b.cid);
  if (!c || !c->browsing_connection ||
      c->browsing_connection->state != AVCTP_CONNECTION_OPENED)
    return ERROR_CODE_COMMAND_DISALLOWED;
  c->browsing_connection->transaction_label =
      (c->browsing_connection->transaction_label + 1) & 15;
  s->response_deadline.reset(response_timeout_ms_);
  s->pending = true;
  s->pdu = pdu;
  s->response.reset();
  return 0;
}
int AvrcpBrowsingController::result(const Binding &b, int rc) {
  if (rc) {
    auto *s = byCid(b.cid);
    if (s)
      s->pending = false;
  }
  return rc;
}
int AvrcpBrowsingController::folderItems(const Binding &b,
                                         avrcp_browsing_scope_t scope,
                                         uint32_t first, uint32_t last,
                                         uint32_t attr) {
  if (unsigned(scope) > unsigned(AVRCP_BROWSING_NOW_PLAYING) || first > last ||
      !attributesValid(attr))
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  int rc = ready(b, AVRCP_PDU_ID_GET_FOLDER_ITEMS);
  if (rc)
    return rc;
  switch (scope) {
  case AVRCP_BROWSING_MEDIA_PLAYER_LIST:
    rc = avrcp_browsing_controller_get_media_players(b.cid, first, last, attr);
    break;
  case AVRCP_BROWSING_MEDIA_PLAYER_VIRTUAL_FILESYSTEM:
    rc = avrcp_browsing_controller_browse_file_system(b.cid, first, last, attr);
    break;
  case AVRCP_BROWSING_SEARCH:
    rc = avrcp_browsing_controller_browse_media(b.cid, first, last, attr);
    break;
  default:
    rc = avrcp_browsing_controller_browse_now_playing_list(b.cid, first, last,
                                                           attr);
    break;
  }
  return result(b, rc);
}
int AvrcpBrowsingController::setBrowsedPlayer(const Binding &b, uint16_t id) {
  int rc = ready(b, AVRCP_PDU_ID_SET_BROWSED_PLAYER);
  return rc ? rc
            : result(b,
                     avrcp_browsing_controller_set_browsed_player(b.cid, id));
}
int AvrcpBrowsingController::totalItems(const Binding &b,
                                        avrcp_browsing_scope_t scope) {
  if (unsigned(scope) > unsigned(AVRCP_BROWSING_NOW_PLAYING))
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  int rc = ready(b, AVRCP_PDU_ID_GET_TOTAL_NUMBER_OF_ITEMS);
  return rc ? rc
            : result(b, avrcp_browsing_controller_get_total_nr_items_for_scope(
                            b.cid, scope));
}
int AvrcpBrowsingController::changePath(const Binding &b, bool down,
                                        const std::array<uint8_t, 8> &uid) {
  int rc = ready(b, AVRCP_PDU_ID_CHANGE_PATH);
  auto copy = uid;
  return rc ? rc
            : result(b, avrcp_browsing_controller_change_path(
                            b.cid, down ? 1 : 0, copy.data()));
}
int AvrcpBrowsingController::itemAttributes(const Binding &b,
                                            const std::array<uint8_t, 8> &uid,
                                            uint16_t counter, uint32_t attrs,
                                            avrcp_browsing_scope_t scope) {
  if (scope < AVRCP_BROWSING_MEDIA_PLAYER_VIRTUAL_FILESYSTEM ||
      scope > AVRCP_BROWSING_NOW_PLAYING || attrs == AVRCP_MEDIA_ATTR_NONE ||
      !attributesValid(attrs))
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  int rc = ready(b, AVRCP_PDU_ID_GET_ITEM_ATTRIBUTES);
  auto copy = uid;
  return rc ? rc
            : result(b, avrcp_browsing_controller_get_item_attributes_for_scope(
                            b.cid, copy.data(), counter, attrs, scope));
}
int AvrcpBrowsingController::search(const Binding &b, const char *p,
                                    uint16_t n) {
  if (!p || !n || n > 19)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  int rc = ready(b, AVRCP_PDU_ID_SEARCH);
  return rc ? rc
            : result(b, avrcp_browsing_controller_search(
                            b.cid, n, const_cast<char *>(p)));
}
void AvrcpBrowsingController::connection(uint8_t t, uint16_t, uint8_t *p,
                                         uint16_t n) {
  if (t != HCI_EVENT_PACKET || !p || n < 3 || unsigned(p[1]) + 2 != n ||
      p[0] != HCI_EVENT_AVRCP_META)
    return;
  const auto kind = p[2];
  if (kind == AVRCP_SUBEVENT_INCOMING_BROWSING_CONNECTION && n == 11) {
    Address a{};
    reverse_bd_addr(p + 3, a.data());
    uint16_t id = little_endian_read_16(p, 9);
    auto *s = vacant();
    for (const auto &existing : slots)
      if (existing.binding.cid && existing.binding.peer == a) {
        avrcp_browsing_decline_incoming_connection(id);
        return;
      }
    auto link = BluetoothRuntime::instance().linkForPeer(a);
    if (!owner || !owner->started_ || !owner->allowed(a) || !link.active ||
        !s) {
      avrcp_browsing_decline_incoming_connection(id);
      return;
    }
    s->binding = {a, link.epoch, ++generation, id};
    s->connection_deadline.reset(owner->connection_timeout_ms_);
    int rc = avrcp_browsing_configure_incoming_connection(
        id, s->ertm.data(), s->usable, &ertm_config);
    if (rc) {
      avrcp_browsing_decline_incoming_connection(id);
      clear(*s);
    }
    return;
  }
  if (kind == AVRCP_SUBEVENT_BROWSING_CONNECTION_ESTABLISHED && n == 12) {
    uint16_t id = little_endian_read_16(p, 10);
    auto *s = byCid(id);
    if (!s) {
      if (!p[3])
        avrcp_browsing_disconnect(id);
      return;
    }
    auto b = s->binding;
    Address remote{};
    reverse_bd_addr(p + 4, remote.data());
    if (remote != b.peer) {
      if (!p[3])
        avrcp_browsing_disconnect(id);
      return;
    }
    if (p[3]) {
      clear(*s);
      if (owner)
        owner->emit({Kind::Error, b, p[3]});
      return;
    }
    s->connected = true;
    if (!owner || !byBinding(b) || s->closing) {
      s->closing = true;
      s->close_retry = avrcp_browsing_disconnect(id) != 0;
      return;
    }
    owner->emit({Kind::Connected, b});
    return;
  }
  if (kind == AVRCP_SUBEVENT_BROWSING_CONNECTION_RELEASED && n == 5) {
    auto *s = byCid(little_endian_read_16(p, 3));
    if (!s)
      return;
    auto b = s->binding;
    clear(*s);
    if (owner)
      owner->emit({Kind::Disconnected, b});
  }
}
void AvrcpBrowsingController::transport(uint8_t t, uint16_t channel, uint8_t *p,
                                        uint16_t n) {
  if (t != L2CAP_DATA_PACKET) {
    if (t != HCI_EVENT_PACKET || !p || n < 2 || unsigned(p[1]) + 2 != n ||
        !owner)
      return;
    auto *c = avrcp_get_connection_for_browsing_l2cap_cid_for_role(
        AVRCP_CONTROLLER, channel);
    auto *s = c ? byCid(c->avrcp_browsing_cid) : nullptr;
    if (s && !s->closing && byBinding(s->binding) && sdk_handler)
      sdk_handler(t, channel, p, n);
    return;
  }
  auto *c = avrcp_get_connection_for_browsing_l2cap_cid_for_role(
      AVRCP_CONTROLLER, channel);
  if (!c || !c->browsing_connection)
    return;
  auto *s = byCid(c->avrcp_browsing_cid);
  if (!s || !owner || !byBinding(s->binding) || s->closing || !s->pending)
    return;
  auto *r = c->browsing_connection;
  if (r->state != AVCTP_W2_RECEIVE_RESPONSE)
    return;
  auto outcome = s->response.accept(p, n, r->transaction_label, s->pdu);
  if (outcome == BrowsingResponse::Result::Rejected) {
    owner->emit({Kind::Error, s->binding,
                 ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS, s->pdu});
    return;
  }
  if (outcome == BrowsingResponse::Result::More)
    return;
  if (s->response.data()[0] == 4 &&
      (s->pdu == 0x70 || s->pdu == 0x71 || s->pdu == 0x75 || s->pdu == 0x80))
    r->uid_counter = big_endian_read_16(s->response.data(), 1);
  r->state = AVCTP_CONNECTION_OPENED;
  s->pending = false;
  owner->emit({Kind::Response, s->binding, 0, s->pdu, s->response.data(),
               uint16_t(s->response.size())});
  s->response.reset();
}
void AvrcpBrowsingController::setRequestCallback(void (*cb)(const Request &,
                                                            void *),
                                                 void *context) {
  if (!dispatching_) {
    request_callback_ = cb;
    request_context_ = context;
  }
}
int AvrcpBrowsingController::respondParts(const RequestToken &t, uint8_t status,
                                          const uint8_t *prefix, uint16_t pn,
                                          const uint8_t *p, uint16_t n) {
  auto *s = byBinding(t.binding);
  if (!started_ || dispatching_ || !target_enabled_ || !s || s->closing ||
      !s->request_waiting || s->request_sequence != t.sequence ||
      s->request_label != t.transaction || s->request_pdu != t.pdu ||
      s->reply.active())
    return ERROR_CODE_COMMAND_DISALLOWED;
  auto *c = avrcp_get_connection_for_browsing_cid_for_role(AVRCP_TARGET,
                                                           t.binding.cid);
  if (!c || !c->browsing_connection)
    return ERROR_CODE_COMMAND_DISALLOWED;
  auto *r = c->browsing_connection;
  uint16_t mtu = std::min<uint16_t>(
      512, l2cap_get_remote_mtu_for_local_cid(r->l2cap_browsing_cid));
  if (mtu < 48 ||
      !s->reply.prepareParts(t.transaction, t.pdu, status, prefix, pn, p, n))
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  s->request_waiting = false;
  s->tx_mtu = mtu;
  s->tx_retry = false;
  avrcp_browsing_request_can_send_now(r, r->l2cap_browsing_cid);
  return 0;
}
int AvrcpBrowsingController::respond(const RequestToken &t, uint8_t status,
                                     const uint8_t *p, uint16_t n) {
  return respondParts(t, status, nullptr, 0, p, n);
}
int AvrcpBrowsingController::reject(const RequestToken &t, uint8_t status) {
  if (status == 4)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return respond(t, status, nullptr, 0);
}
int AvrcpBrowsingController::respondSetBrowsedPlayer(
    const RequestToken &t, uint16_t counter, uint32_t items, uint16_t charset,
    uint8_t depth, const uint8_t *p, uint16_t n) {
  if (t.pdu != 0x70)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  uint8_t h[9];
  big_endian_store_16(h, 0, counter);
  big_endian_store_32(h, 2, items);
  big_endian_store_16(h, 6, charset);
  h[8] = depth;
  return respondParts(t, 4, h, sizeof(h), p, n);
}
int AvrcpBrowsingController::respondFolderItems(const RequestToken &t,
                                                uint16_t counter,
                                                uint16_t count,
                                                const uint8_t *p, uint16_t n) {
  if (t.pdu != 0x71)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  uint8_t h[4];
  big_endian_store_16(h, 0, counter);
  big_endian_store_16(h, 2, count);
  return respondParts(t, 4, h, sizeof(h), p, n);
}
int AvrcpBrowsingController::respondTotalItems(const RequestToken &t,
                                               uint16_t counter,
                                               uint32_t count) {
  if (t.pdu != 0x75)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  uint8_t h[6];
  big_endian_store_16(h, 0, counter);
  big_endian_store_32(h, 2, count);
  return respond(t, 4, h, sizeof(h));
}
int AvrcpBrowsingController::respondChangePath(const RequestToken &t,
                                               uint32_t count) {
  if (t.pdu != 0x72)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  uint8_t h[4];
  big_endian_store_32(h, 0, count);
  return respond(t, 4, h, sizeof(h));
}
int AvrcpBrowsingController::respondItemAttributes(const RequestToken &t,
                                                   uint8_t count,
                                                   const uint8_t *p,
                                                   uint16_t n) {
  if (t.pdu != 0x73)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return respondParts(t, 4, &count, 1, p, n);
}
int AvrcpBrowsingController::respondSearch(const RequestToken &t,
                                           uint16_t counter, uint32_t count) {
  if (t.pdu != 0x80)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  uint8_t h[6];
  big_endian_store_16(h, 0, counter);
  big_endian_store_32(h, 2, count);
  return respond(t, 4, h, sizeof(h));
}
void AvrcpBrowsingController::target(uint8_t type, uint16_t channel, uint8_t *p,
                                     uint16_t n) {
  if (type == HCI_EVENT_PACKET) {
    if (!p || n < 4 || unsigned(p[1]) + 2 != n ||
        p[0] != L2CAP_EVENT_CAN_SEND_NOW)
      return;
    channel = l2cap_event_can_send_now_get_local_cid(p);
  } else if (type != L2CAP_DATA_PACKET)
    return;
  auto *c = avrcp_get_connection_for_browsing_l2cap_cid_for_role(AVRCP_TARGET,
                                                                 channel);
  auto *s = c ? byCid(c->avrcp_browsing_cid) : nullptr;
  if (!c || !c->browsing_connection || !s || !owner || !byBinding(s->binding) ||
      s->closing)
    return;
  auto *r = c->browsing_connection;
  if (type == HCI_EVENT_PACKET) {
    if (!s->reply.active())
      return;
    uint8_t frame[512];
    size_t size = s->reply.frame(s->tx_mtu, frame, sizeof(frame));
    if (!size) {
      s->reply.reset();
      owner->emit({Kind::Error, s->binding,
                   ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS, s->request_pdu});
      return;
    }
    int rc = l2cap_send(channel, frame, uint16_t(size));
    if (rc) {
      s->tx_retry = true;
      owner->emit({Kind::Error, s->binding, rc, s->request_pdu});
      return;
    }
    s->reply.consume(frame, size);
    if (s->reply.active())
      avrcp_browsing_request_can_send_now(r, channel);
    else {
      owner->emit({Kind::TargetResponseSent, s->binding, 0, s->request_pdu});
      auto *ct = avrcp_get_connection_for_browsing_l2cap_cid_for_role(
          AVRCP_CONTROLLER, channel);
      if (ct && ct->browsing_connection &&
          ct->browsing_connection->wait_to_send)
        l2cap_request_can_send_now_event(channel);
    }
    return;
  }
  if (s->request_waiting || s->reply.active())
    return; // One inbound transaction per peer, independent of Controller.
  const bool assembling = s->command.assembling();
  auto result = s->command.accept(p, n);
  if (result == BrowsingCommand::Result::More) {
    if (!assembling)
      s->assembly_deadline.reset(owner->assembly_timeout_ms_);
    return;
  }
  if (assembling && result == BrowsingCommand::Result::Rejected)
    return; // Invalid/interleaved traffic cannot renew or cancel the budget.
  s->assembly_deadline.reset();
  if (result == BrowsingCommand::Result::Rejected || !owner->target_enabled_ ||
      !owner->request_callback_) {
    // Completed fragmented commands also need a reply when Target is disabled.
    bool complete = result == BrowsingCommand::Result::Complete;
    if (complete ||
        (p && n >= 6 && (p[0] & 15) == 0 && p[1] == 0x11 && p[2] == 0x0e)) {
      uint8_t label = complete ? s->command.label() : uint8_t(p[0] >> 4);
      s->request_pdu = 0xa0;
      s->tx_mtu =
          std::min<uint16_t>(512, l2cap_get_remote_mtu_for_local_cid(channel));
      if (s->tx_mtu >= 48 && s->reply.prepare(label, 0xa0, 0, nullptr, 0)) {
        s->target_deadline.reset(owner->target_timeout_ms_);
        avrcp_browsing_request_can_send_now(r, channel);
      }
    }
    s->command.reset();
    return;
  }
  s->target_deadline.reset(owner->target_timeout_ms_);
  s->request_waiting = true;
  s->request_sequence = ++request_sequence;
  s->request_label = s->command.label();
  s->request_pdu = s->command.pdu();
  Request request{
      {s->binding, s->request_sequence, s->request_label, s->request_pdu},
      s->command.data(),
      uint16_t(s->command.size())};
  owner->dispatching_ = true;
  owner->request_callback_(request, owner->request_context_);
  owner->dispatching_ = false;
  s->command.reset();
}
} // namespace picow::avrcp
