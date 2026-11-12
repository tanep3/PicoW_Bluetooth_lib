#include "SppClient.h"
#include "SdkEventLength.h"
#include "SdkSend.h"
#include "SdkTransport.h"
#include "SdkPortConfiguration.h"
#include "../bluetooth-runtime/BluetoothRuntime.h"
#include "pico/time.h"
#if defined(ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE_FOR_RFCOMM) && !defined(ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE)
#error "RFCOMM ERTM requires the shared L2CAP ERTM build flag too"
#endif
#if defined(ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE_FOR_RFCOMM) && !defined(PICOW_SPP_SDK_OVERLAY)
#error "RFCOMM ERTM requires the validated transport overlay; use picow_target_spp"
#endif
namespace spp {
namespace {
struct RfcommSender {
  bool canSend(uint16_t cid) { return rfcomm_can_send_packet_now(cid); }
  void reserve() {
#ifndef ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE_FOR_RFCOMM
    rfcomm_reserve_packet_buffer();
#endif
  }
  uint8_t *buffer() { return rfcomm_get_outgoing_buffer(); }
  int32_t send(uint16_t cid, uint16_t bytes) {
    return rfcomm_send_prepared(cid, bytes);
  }
  void release() {
#ifndef ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE_FOR_RFCOMM
    rfcomm_release_packet_buffer();
#endif
  }
  bool transient(int32_t status) {
    return status == BTSTACK_ACL_BUFFERS_FULL ||
           status == RFCOMM_NO_OUTGOING_CREDITS ||
           status == RFCOMM_AGGREGATE_FLOW_OFF;
  }
};
} // namespace
// Outgoing RFCOMM has no per-call MTU cap in this SDK. Shared targets must
// choose an HCI buffer whose RFCOMM payload fits the fixed receive budget.
static_assert(HCI_ACL_PAYLOAD_SIZE <= MaxFrame + 9,
              "SPP requires HCI_ACL_PAYLOAD_SIZE <= MaxFrame + 9; use a shared BTstack config");
Client *Client::active_ = nullptr;
Client::Transport Client::transport(Handle handle) const {
  const auto state = engine_.snapshot();
  if (!matches(state, handle) || state.runtime != Runtime::Ready ||
      state.routes[handle.slot].phase != Phase::Open) return Transport::Unknown;
  for (const auto &binding : bindings_) {
    if (!binding.cid || !binding.opened || binding.handle.slot != handle.slot ||
        binding.handle.generation != handle.generation) continue;
    const auto link = BluetoothRuntime::instance().linkForPeer(binding.peer);
    if (!link.active || link.epoch != binding.acl) return Transport::Unknown;
#ifdef PICOW_SPP_SDK_OVERLAY
    const auto mode = picow_rfcomm_channel_mode(binding.cid);
    if (mode == 0) return Transport::Basic;
    if (mode == 3) return Transport::Ertm;
    return Transport::Unknown;
#else
    return Transport::Basic; // RFCOMM ERTM compilation requires the overlay.
#endif
  }
  return Transport::Unknown;
}
bool Client::begin() {
  Event tick;
  tick.kind = Kind::Tick;
  tick.now_us = time_us_64();
  if (!engine_.post(tick))
    return false;
  Event e;
  e.kind = Kind::Begin;
  return engine_.post(e);
}
void Client::poll() {
  auto &rt = BluetoothRuntime::instance();
  rt.poll();
  Event e;
  const auto runtime = engine_.snapshot().runtime;
  if (rt.failed() && runtime != Runtime::Failed && runtime != Runtime::Stopped) {
    e.kind = Kind::RuntimeFault;
    e.status = -1;
  } else {
    e.kind = Kind::Tick;
    e.now_us = time_us_64();
  }
  engine_.post(e);
  engine_.poll();
}
void Client::linkOpened(Address peer, uint16_t handle, uint32_t epoch) {
  if (peer != engine_.snapshot().config.peer)
    return;
  Event e;
  e.kind = Kind::AclOpened;
  e.peer = peer;
  e.cid = handle;
  e.token = epoch;
  engine_.post(e);
  for (auto &b : bindings_)
    if (b.cid && !b.acl && b.peer == peer)
      b.acl = epoch;
}
void Client::execute(const Action &a, const State &s, const uint8_t *p,
                     size_t n) {
  Event e;
  e.handle = a.handle;
  e.token = a.token;
  e.cid = a.cid;
  auto &rt = BluetoothRuntime::instance();
  switch (a.op) {
  case Op::Initialize:
    init_ = a.token;
    if (active_ || !rt.claim(BluetoothRuntime::Profile::Spp)) {
      e.kind = Kind::InitResult;
      e.status = -1;
      engine_.post(e);
      break;
    }
    active_ = this;
    e.status = rt.initialize();
    if (!e.status) {
      rt.setHandler(BluetoothRuntime::Profile::Spp, packet);
      rt.setPairingPolicy(BluetoothRuntime::Profile::Spp, s.config.peer,
                          security_.allow_confirmation);
      rt.setSecurityLevel(BluetoothRuntime::Profile::Spp,
                          security_.authentication ? 2 : 0);
      rfcomm_init();
#ifdef ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE_FOR_RFCOMM
      rfcomm_enable_l2cap_ertm(ertmRequest, ertmReleased);
#endif
      e.status = rt.powerOn();
    }
    if (e.status || hci_get_state() == HCI_STATE_WORKING) {
      e.kind = Kind::InitResult;
      engine_.post(e);
      if (!e.status) {
        auto l = rt.linkForPeer(s.config.peer);
        if (l.active)
          linkOpened(l.peer, l.handle, l.epoch);
      }
    }
    break;
  case Op::SelectPeer: {
    rt.setPairingPolicy(BluetoothRuntime::Profile::Spp, s.config.peer,
                        security_.allow_confirmation);
    const auto link = rt.linkForPeer(s.config.peer);
    if (link.active) linkOpened(link.peer, link.handle, link.epoch);
    break;
  }
  case Op::QuiesceRuntime:
    rt.powerOff();
    break;
  case Op::StartSdp: {
    query_ = a.token;
    cancelled_ = false;
    parser_.reset();
    e.status =
        rt.startSdp(a.token, s.query.peer, s.query.uuid.bytes, sdpPacket);
    if (e.status) {
      e.kind = Kind::SdpComplete;
      engine_.post(e);
      query_ = 0;
    }
    break;
  }
  case Op::CancelSdp:
    if (query_ == a.token) {
      cancelled_ = true;
      rt.cancelSdp(a.token);
    }
    break;
  case Op::Connect: {
    auto &b = bindings_[a.handle.slot];
    auto peer = s.routes[a.handle.slot].peer;
    b = {};
    b.handle = a.handle;
    b.peer = peer;
    b.acl = rt.linkForPeer(peer).epoch;
    e.status = rfcomm_create_channel_with_initial_credits(
        packet, peer.data(), s.routes[a.handle.slot].channel, 0, &b.cid);
    if (e.status) {
      // The validated SDK overlay unlinks failed nodes and returns any new
      // ERTM lease synchronously before this failure. Keep other routes alive.
      e.kind = Kind::Opened;
      e.peer = peer;
      e.channel = s.routes[a.handle.slot].channel;
      e.cid = b.cid;
      engine_.post(e);
      b = {};
    }
    break;
  }
  case Op::Accept: {
    Offer *offer = nullptr;
    for (auto &o : offers_)
      if (o.cid == a.cid) {
        offer = &o;
        break;
      }
    auto link = offer ? rt.linkForPeer(offer->peer) : BluetoothRuntime::Link{};
    if (!offer || !link.active || link.epoch != offer->acl) {
      if (offer)
        *offer = {};
      e.kind = Kind::Opened;
      e.status = -1;
      e.peer = s.routes[a.handle.slot].peer;
      e.channel = s.routes[a.handle.slot].channel;
      engine_.post(e);
      break;
    }
    auto &b = bindings_[a.handle.slot];
    b = {};
    b.handle = a.handle;
    b.peer = offer->peer;
    b.acl = offer->acl;
    b.cid = a.cid;
    *offer = {};
    e.status = rfcomm_accept_connection(a.cid);
    if (e.status) {
      e.kind = Kind::Opened;
      e.peer = b.peer;
      e.channel = s.routes[a.handle.slot].channel;
      engine_.post(e);
      b = {};
    }
    break;
  }
  case Op::Reject:
    for (auto &o : offers_)
      if (o.cid == a.cid && o.acl == a.token) {
        auto link = rt.linkForPeer(o.peer);
        bool owned = false;
        for (const auto &b : bindings_)
          owned |= b.cid == a.cid;
        if (link.active && link.epoch == o.acl && !owned)
          rfcomm_decline_connection(a.cid);
        o = {};
        break;
      }
    break;
  case Op::Disconnect:
  case Op::CancelConnect: {
    auto &b = bindings_[a.handle.slot];
    const uint16_t cid = a.cid ? a.cid : b.cid;
    e.status = cid ? rfcomm_disconnect(cid) : -1;
    if (e.status) {
      e.kind = Kind::DisconnectFailed;
      engine_.post(e);
    }
    break;
  }
  case Op::RequestCanSend: {
    auto &b = bindings_[a.handle.slot];
    b.send = a.token;
    e.status = rfcomm_request_can_send_now_event(a.cid);
    if (e.status) {
      b.send = 0;
      e.kind = Kind::CanSendFailed;
      engine_.post(e);
    }
    break;
  }
  case Op::Send: {
    RfcommSender sender;
    const auto attempt = detail::sendPrepared(sender, a.cid, p,
                                              static_cast<uint16_t>(n));
    e.kind = attempt.deferred ? Kind::SendDeferred : Kind::SendResult;
    e.status = attempt.status;
    engine_.post(e);
    break;
  }
  case Op::GrantCredits:
    e.kind = Kind::CreditResult;
    e.status = rfcomm_grant_credits(a.cid, static_cast<uint8_t>(a.bytes));
    engine_.post(e);
    break;
  case Op::UnregisterService: {
    const auto slot = a.handle.slot;
    if (record_handles_[slot]) {
      sdp_unregister_service(record_handles_[slot]);
      record_handles_[slot] = 0;
    }
    rfcomm_unregister_service(s.endpoints[slot].channel);
    e.kind = Kind::UnlistenResult; e.slot = slot;
    engine_.post(e);
    break;
  }
  case Op::Control: {
    const auto &c = s.routes[a.handle.slot].control;
    e.kind = Kind::ControlResult;
    switch (c.operation) {
    case Control::LineStatus: e.status = rfcomm_send_local_line_status(a.cid, c.value); break;
    case Control::ModemStatus:
      // The SDK serializes this byte verbatim; terminate the V.24 signal octet
      // with EA=1. The public value contains only FC/RTC/RTR/IC/DV flags.
      e.status = rfcomm_send_modem_status(a.cid, c.value | 1); break;
    case Control::ConfigurePort:
      e.status = picow_rfcomm_send_port_configuration(a.cid,
          static_cast<uint8_t>(c.port.baud),
          static_cast<uint8_t>(c.port.data_bits),
          static_cast<uint8_t>(c.port.stop_bits),
          static_cast<uint8_t>(c.port.parity), c.port.flow_control);
      break;
    case Control::QueryPort: e.status = rfcomm_query_port_configuration(a.cid); break;
    }
    engine_.post(e);
    break;
  }
  case Op::RegisterService: {
    auto slot = a.handle.slot;
    const auto &ep = s.endpoints[slot];
    e.kind = Kind::ListenResult;
    e.slot = slot;
    e.status = rfcomm_register_service_with_initial_credits(packet, ep.channel,
                                                            MaxFrame, 0);
    if (!e.status) {
      record_handles_[slot] = sdp_create_service_record_handle();
      spp_create_custom_sdp_record(records_[slot].data(),
                                   record_handles_[slot],
                                   ep.uuid.bytes.data(), ep.channel, "SPP");
      e.status = sdp_register_service(records_[slot].data());
      if (e.status) {
        rfcomm_unregister_service(ep.channel);
        record_handles_[slot] = 0;
      }
    }
    if (!e.status)
      gap_connectable_control(1);
    engine_.post(e);
    break;
  }
  }
}
void Client::ertmRequest(rfcomm_ertm_request_t *request) {
  auto *c = active_; if (!c || !request) return;
  const auto peer = c->engine_.snapshot().config.peer;
  Address actual{}; std::copy(request->addr, request->addr + 6, actual.begin());
  if (!c->ertm_.request(*request, peer) && c->ertm_.required() && actual == peer) {
    // The SDK callback has no decline result. Refuse a silent BASIC fallback
    // for a required policy by failing the runtime before exposing an Open DLC.
    Event e; e.kind = Kind::RuntimeFault; e.status = BTSTACK_MEMORY_ALLOC_FAILED;
    c->engine_.post(e);
  }
}
void Client::ertmReleased(uint16_t id) {
  if (active_) active_->ertm_.release(id);
}
void Client::sdpPacket(uint8_t type, uint16_t, uint8_t *p, uint16_t size) {
  auto *c = active_;
  if (!c || type != HCI_EVENT_PACKET || !c->query_ || size < 2 ||
      size < unsigned(p[1]) + 2)
    return;
  if (p[0] == SDP_EVENT_QUERY_ATTRIBUTE_VALUE && size == 11 && !c->cancelled_)
    c->parser_.byte(little_endian_read_16(p, 2), little_endian_read_16(p, 4),
                    little_endian_read_16(p, 6), little_endian_read_16(p, 8),
                    p[10]);
  if (p[0] != SDP_EVENT_QUERY_COMPLETE || size < 3)
    return;
  Event e;
  e.token = c->query_;
  if (c->cancelled_)
    e.kind = Kind::SdpQuiesced;
  else {
    std::array<Candidate, CandidateCapacity> candidates{};
    uint8_t count = 0;
    e.error = c->parser_.finish(candidates, count);
    e.status = sdp_event_query_complete_get_status(p);
    if (e.error == Error::None && !e.status)
      for (uint8_t i = 0; i < count; ++i) {
        e.kind = Kind::SdpCandidate;
        e.candidate = candidates[i];
        c->engine_.post(e);
      }
    e.kind = Kind::SdpComplete;
  }
  c->engine_.post(e);
  c->query_ = 0;
}
void Client::packet(uint8_t type, uint16_t cid, uint8_t *p, uint16_t size) {
  auto *c = active_;
  if (!c)
    return;
  Event e;
  auto &rt = BluetoothRuntime::instance();
  if (type == HCI_EVENT_PACKET) {
    if (!detail::eventLengthValid(p, size, RFCOMM_EVENT_CHANNEL_OPENED))
      return;
    switch (p[0]) {
    case BTSTACK_EVENT_STATE:
      if (size >= 3 && btstack_event_state_get_state(p) == HCI_STATE_WORKING &&
          c->engine_.snapshot().runtime == Runtime::Starting) {
        e.kind = Kind::InitResult;
        e.token = c->init_;
        c->engine_.post(e);
      }
      return;
    case HCI_EVENT_CONNECTION_COMPLETE: {
      if (size < 13 || hci_event_connection_complete_get_status(p) ||
          hci_event_connection_complete_get_link_type(p) != 1)
        return;
      auto link = rt.linkForHandle(
          hci_event_connection_complete_get_connection_handle(p));
      if (link.active)
        c->linkOpened(link.peer, link.handle, link.epoch);
      return;
    }
    case HCI_EVENT_DISCONNECTION_COMPLETE: {
      if (size < 6)
        return;
      auto link = rt.linkForHandle(
          hci_event_disconnection_complete_get_connection_handle(p));
      if (link.active)
        for (auto &offer : c->offers_)
          if (offer.acl == link.epoch)
            offer = {};
      if (link.active && link.peer == c->engine_.snapshot().config.peer) {
        e.kind = Kind::AclClosed;
        e.cid = link.handle;
        e.token = link.epoch;
        e.peer = link.peer;
        e.status = hci_event_disconnection_complete_get_reason(p);
        c->engine_.post(e);
      }
      return;
    }
    case RFCOMM_EVENT_INCOMING_CONNECTION: {
      if (size < 13)
        return;
      e.kind = Kind::Incoming;
      rfcomm_event_incoming_connection_get_bd_addr(p, e.peer.data());
      e.channel = rfcomm_event_incoming_connection_get_server_channel(p);
      e.cid = rfcomm_event_incoming_connection_get_rfcomm_cid(p);
      const auto incoming_link =
          rt.linkForHandle(rfcomm_event_incoming_connection_get_con_handle(p));
      e.token = (incoming_link.active && incoming_link.peer == e.peer)
                    ? incoming_link.epoch
                    : 0;
      bool owned = false;
      for (const auto &b : c->bindings_)
        owned |= b.cid == e.cid;
      if (!owned) {
        Offer *offer = nullptr;
        for (auto &o : c->offers_)
          if (o.cid == e.cid) {
            offer = &o;
            break;
          }
        if (!offer)
          for (auto &o : c->offers_)
            if (!o.cid) {
              offer = &o;
              break;
            }
        if (offer)
          *offer = {e.peer, e.token, e.cid};
      }
      c->engine_.post(e);
      return;
    }
    case RFCOMM_EVENT_CHANNEL_OPENED:
      if (size < 17)
        return;
      cid = rfcomm_event_channel_opened_get_rfcomm_cid(p);
      e.kind = Kind::Opened;
      e.status = rfcomm_event_channel_opened_get_status(p);
      e.channel = rfcomm_event_channel_opened_get_server_channel(p);
      e.mtu = rfcomm_event_channel_opened_get_max_frame_size(p);
      rfcomm_event_channel_opened_get_bd_addr(p, e.peer.data());
      break;
    case RFCOMM_EVENT_CHANNEL_CLOSED:
      if (size < 4)
        return;
      cid = rfcomm_event_channel_closed_get_rfcomm_cid(p);
      e.kind = Kind::Closed;
      break;
    case RFCOMM_EVENT_REMOTE_LINE_STATUS:
      if (size < 5) return;
      cid = rfcomm_event_remote_line_status_get_rfcomm_cid(p);
      e.kind = Kind::RemoteLineStatus;
      e.control.value = rfcomm_event_remote_line_status_get_line_status(p);
      break;
    case RFCOMM_EVENT_REMOTE_MODEM_STATUS:
      if (size < 5) return;
      cid = rfcomm_event_remote_modem_status_get_rfcomm_cid(p);
      e.kind = Kind::RemoteModemStatus;
      e.control.value = rfcomm_event_remote_modem_status_get_modem_status(p);
      break;
    case RFCOMM_EVENT_PORT_CONFIGURATION:
      if (size < 10) return;
      cid = rfcomm_event_port_configuration_get_rfcomm_cid(p);
      e.kind = Kind::PortConfiguration;
      e.remote = rfcomm_event_port_configuration_get_remote(p);
      e.control.port.baud = static_cast<Baud>(rfcomm_event_port_configuration_get_baud_rate(p));
      e.control.port.data_bits = static_cast<DataBits>(p[6] & 3);
      e.control.port.stop_bits = static_cast<StopBits>(rfcomm_event_port_configuration_get_stop_bits(p));
      e.control.port.parity = static_cast<Parity>(rfcomm_event_port_configuration_get_parity(p));
      e.control.port.flow_control = rfcomm_event_port_configuration_get_flow_control(p);
      e.control.port.xon = rfcomm_event_port_configuration_get_xon(p);
      e.control.port.xoff = rfcomm_event_port_configuration_get_xoff(p);
      break;
    case RFCOMM_EVENT_CAN_SEND_NOW:
      if (size < 4)
        return;
      cid = rfcomm_event_can_send_now_get_rfcomm_cid(p);
      e.kind = Kind::CanSend;
      break;
    default:
      return;
    }
  } else if (type != RFCOMM_DATA_PACKET)
    return;
  for (auto &b : c->bindings_)
    if (b.cid && b.cid == cid) {
      e.cid = cid;
      e.handle = b.handle;
      if (type == HCI_EVENT_PACKET) {
        if (e.kind == Kind::RemoteLineStatus || e.kind == Kind::RemoteModemStatus ||
            e.kind == Kind::PortConfiguration) {
          const auto link = rt.linkForPeer(b.peer);
          if (!link.active || link.epoch != b.acl) return;
          e.peer = b.peer; e.token = b.acl;
        }
        if (e.kind == Kind::Opened) {
          // Success belongs to the HCI handle actually carried by the SDK
          // callback, not merely a reused RFCOMM cid/address.
          const auto link = rt.linkForHandle(
              rfcomm_event_channel_opened_get_con_handle(p));
          e.token = (!e.status && link.active && link.peer == e.peer &&
                     link.epoch == b.acl) ? link.epoch : 0;
          if (!e.status && e.token) {
#ifdef ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE_FOR_RFCOMM
            // An existing BASIC multiplexer can be reused without invoking
            // the ERTM request callback, e.g. after selecting another peer.
            // Verify the live transport before exposing a mandatory route.
            const auto state = c->engine_.snapshot();
            const bool opening = matches(state, b.handle) &&
                state.routes[b.handle.slot].phase == Phase::Connecting &&
                state.routes[b.handle.slot].peer == e.peer &&
                state.routes[b.handle.slot].channel == e.channel &&
                (!state.routes[b.handle.slot].cid ||
                 state.routes[b.handle.slot].cid == cid);
            if (opening && c->ertm_.required() &&
                picow_rfcomm_channel_mode(cid) != 3) {
              Event failure; failure.kind = Kind::RuntimeFault;
              failure.status = ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
              c->engine_.post(failure);
              return;
            }
#endif
            b.opened = true;
          }
        }
        if (e.kind == Kind::CanSend) {
          if (!b.send)
            return;
          e.token = b.send;
          b.send = 0;
        }
        if (e.kind == Kind::Closed && !b.opened)
          e.cid = c->engine_.snapshot().routes[b.handle.slot].cid;
        c->engine_.post(e);
        if (e.kind == Kind::Closed || (e.kind == Kind::Opened && e.status))
          b = {};
      } else
        c->engine_.receive(e, p, size);
      break;
    }
}
} // namespace spp
