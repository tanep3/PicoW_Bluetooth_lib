// Internal SDK adapter promoted from PicoA2dpSinkSppProbe. Not an application API.
#pragma once
#include "BluetoothRuntime.h"
#include "A2dpSinkReceiver.h"
#include "A2dpShared.h"
#include "A2dpSharedSdk.h"
#include "A2dpExperimentalSdk.h"
#include "SbcDecoder.h"
#include "SbcReassembler.h"
#include "btstack.h"

class A2dpSinkProfile {
public:
  using Address = BluetoothRuntime::Address;
  explicit A2dpSinkProfile(Address peer) : peer_(peer) {}
  bool working = false, signaling = false, streaming = false, configured = false, failed = false;
  bool allow_connections = false;
  uint16_t cid = 0; uint8_t seid = 0; unsigned stream_state = 0;
  uint64_t starts = 0, suspends = 0, releases = 0, received = 0, bytes = 0, processed = 0, wrong_state = 0, rejected_peers = 0, malformed_events = 0;
  uint64_t control_sequence = 0;
  uint8_t last_event = 0, last_status = 0;
  picow::sink::Config config;
  picow::sink::CopyQueue<8, 1024> media;
  picow::sink::CopyQueue<16, 64> events;
  picow::sink::Decoder decoder;
  picow::sink::Sequence sequence;
  picow::sink::Reassembler fragments;
  std::array<uint64_t, size_t(picow::sink::ParseError::CountValue)> parse_errors{};
  bool allow_confirmation = false;
  bool require_start_decision = false;
  // Called once at admission of a valid incoming START, not on repeated
  // can-send notifications. This gives restarts in one session unique IDs.
  static bool startConfirmationRequired(avdtp_connection_t *connection, avdtp_stream_endpoint_t *endpoint) {
#ifdef PICOW_A2DP_SINK_START_DECISIONS
    if (!active_ || endpoint != active_->endpoint_ || !active_->require_start_decision) return false;
    auto &s = *active_;
    s.invalidateStart();
    if (!s.callbackSessionLive() || s.failed || s.closing ||
        connection != endpoint->connection || connection->avdtp_cid != s.callback_cid_ ||
        s.next_start_id_ == UINT64_MAX) {
      s.identity_exhausted = true; return true; // Fail closed; poll tears down.
    }
    s.start_request_ = {s.callback_generation_, ++s.next_start_id_};
    s.start_transaction_ = connection->acceptor_transaction_label;
    return true;
#else
    (void)connection; (void)endpoint; return false;
#endif
  }
  A2dpSinkReceiver::StartRequest pendingStart() const {
    if (!start_notified_ || !startReady()) return {};
    return start_request_;
  }
  A2dpSinkReceiver::StartDecisionResult decideStart(A2dpSinkReceiver::StartRequest request, bool accept) {
    using Status = A2dpSinkReceiver::StartDecisionStatus;
#ifdef PICOW_A2DP_SINK_START_DECISIONS
    if (!request.request_id || request.request_id != start_request_.request_id ||
        request.generation != start_request_.generation) return {Status::StaleRequest};
    if (!startWaiting()) { invalidateStart(); return {Status::StaleRequest}; }
    if (!start_notified_ || !startReady()) return {Status::NotReady};
    const uint8_t result = accept ? a2dp_sink_start_stream_accept(callback_cid_, seid)
                                  : a2dp_sink_start_stream_reject(callback_cid_, seid);
    // Consume this application decision even if the SDK refused admission.
    // No automatic retry; disconnect remains available to abandon the request.
    invalidateStart();
    return {result ? Status::SdkRejected : Status::Accepted, result};
#else
    (void)request; (void)accept; return {Status::FeatureUnavailable};
#endif
  }
  uint16_t pending_cid = 0;
  bool connecting = false, closing = false, identity_exhausted = false;
  uint16_t closing_cid = 0;
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
  bool shared_signaling = false, role_detaching = false, role_retiring = false, role_cancelling = false, cancel_timed_out = false;
  uint32_t role_deadline_ms = 0;
  int role_last_status = 0;
  void clearRoleDetach() { role_detaching = role_retiring = role_cancelling = cancel_timed_out = false; shared_signaling = false; }
  bool cancelSharedDiscovery() {
    if (!working || failed || closing || events.count || !callbackIdentityLive() ||
        !picow::a2dp_shared::otherOwnsCid(picow::a2dp_shared::Role::Sink, callback_cid_)) {
      role_last_status = ERROR_CODE_COMMAND_DISALLOWED; return false;
    }
    role_last_status = picow_a2dp_cancel_role_discovery(AVDTP_ROLE_SINK, callback_cid_);
    if (role_last_status) return false;
    shared_signaling = true; role_cancelling = true; cancel_timed_out = false;
    closing = true; closing_cid = callback_cid_; allow_connections = false;
    role_deadline_ms = btstack_run_loop_get_time_ms() + 10000u;
    media.clear(); streaming = false; resetOutput();
    return true;
  }
  bool detachSharedRole() {
    // The other owner may register after this role established signaling.
    if (callbackIdentityLive() && picow::a2dp_shared::otherOwnsCid(picow::a2dp_shared::Role::Sink, callback_cid_)) {
      const auto *connection = avdtp_get_connection_for_avdtp_cid(callback_cid_);
      if (connection && connection->con_handle == callback_handle_) shared_signaling = true;
    }
    if (!shared_signaling || !working || failed || closing || events.count || !callbackIdentityLive()) {
      role_last_status = ERROR_CODE_COMMAND_DISALLOWED; return false;
    }
    role_last_status = picow_a2dp_close_role_media(AVDTP_ROLE_SINK, callback_cid_, seid);
    if (role_last_status) return false;
    role_detaching = true; closing = true; closing_cid = callback_cid_;
    role_deadline_ms = btstack_run_loop_get_time_ms() + 10000u;
    allow_connections = false; streaming = false; media.clear(); resetOutput();
    return true;
  }
#endif
  void rejectSession(uint16_t target) {
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
    if (picow::a2dp_shared::otherOwnsCid(picow::a2dp_shared::Role::Sink, target)) {
      failed = true; role_last_status = ERROR_CODE_COMMAND_DISALLOWED;
      allow_connections = streaming = false; media.clear(); resetOutput(); return;
    }
#endif
    a2dp_sink_disconnect(target);
  }
  void resetOutput() { fragments.reset(); if (decoder.output) decoder.output->reset(); }
  uint32_t generation() const { return active_generation_; }
  // Negative values are local admission failures; nonnegative values are the
  // exact SDK status. This operation never changes the receiver snapshot.
  int reportDelay100us(uint32_t delay_100us, uint32_t expected_generation) {
    if (delay_100us > UINT16_MAX) return -3;
    if (!working || failed || BluetoothRuntime::instance().failed() || closing || connecting ||
        !allow_connections || !signaling || !configured || events.count || events.overflow ||
        events.oversize || identity_exhausted || reject_cid_) return -1;
    if (!expected_generation || expected_generation != active_generation_ ||
        active_generation_ != callback_generation_ || cid != callback_cid_ ||
        !callbackIdentityLive()) return -2;
    // Keep the experimental shared-signaling path fail-closed for this API.
    if (picow::a2dp_shared::otherOwnsPeer(picow::a2dp_shared::Role::Sink, peer_) ||
        picow::a2dp_shared::otherOwnsCid(picow::a2dp_shared::Role::Sink, cid)) return -1;
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
    if (shared_signaling || role_detaching || role_retiring || role_cancelling || cancel_timed_out) return -1;
#endif
    const auto *connection = avdtp_get_connection_for_avdtp_cid(cid);
    const auto *ep = avdtp_get_stream_endpoint_for_seid(seid);
    if (!connection || connection->avdtp_cid != cid || connection->con_handle != callback_handle_ ||
        std::memcmp(connection->remote_addr, peer_.data(), peer_.size()) != 0 ||
        connection->state != AVDTP_SIGNALING_CONNECTION_OPENED ||
        connection->acceptor_connection_state != AVDTP_SIGNALING_CONNECTION_ACCEPTOR_IDLE ||
        connection->wait_to_send_acceptor || !ep || ep != endpoint_ || ep->connection != connection ||
        ep->sep.seid != seid || ep->sep.type != AVDTP_SINK || ep->sep.media_type != AVDTP_AUDIO ||
        !ep->sep.in_use || !ep->remote_sep.seid || ep->remote_sep.seid > 0x3e ||
        ep->initiator_config_state != AVDTP_INITIATOR_STREAM_CONFIG_IDLE ||
        ep->acceptor_config_state != AVDTP_ACCEPTOR_STREAM_CONFIG_IDLE ||
        (ep->state != AVDTP_STREAM_ENDPOINT_CONFIGURED && ep->state != AVDTP_STREAM_ENDPOINT_OPENED &&
         ep->state != AVDTP_STREAM_ENDPOINT_STREAMING)) return -1;
    // Advertising support is insufficient: SET_CONFIGURATION must select it.
    if (!(ep->remote_sep.configured_service_categories & (1u << AVDTP_DELAY_REPORTING))) return -4;
    // BTstack owns initiator arbitration, busy/error returns and serialization.
    return avdtp_sink_delay_report(cid, seid, static_cast<uint16_t>(delay_100us));
  }
  int connect() {
    if (!working || failed || callback_cid_ || connecting || closing || events.count) return -1;
    auto address = peer_;
    // SDK 1.6.2 sink establish uses an unset outgoing CID when avdtp_connect
    // finds an already-pending incoming connection. Never enter that path.
    // No radio poll can interleave this check and the call on our single loop.
    if (auto *existing = avdtp_get_connection_for_bd_addr(address.data())) {
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
      const auto link = BluetoothRuntime::instance().linkForPeer(peer_);
      if (picow::a2dp_shared::otherOwnsPeer(picow::a2dp_shared::Role::Sink, peer_) &&
          existing->state == AVDTP_SIGNALING_CONNECTION_OPENED && link.active &&
          link.handle == existing->con_handle && link.epoch) {
        allow_connections = true; connecting = true; shared_signaling = true;
        pending_cid = existing->avdtp_cid;
        role_last_status = picow_a2dp_join_existing_role(AVDTP_ROLE_SINK, pending_cid);
        if (role_last_status) { connecting = false; pending_cid = 0; }
        return role_last_status;
      }
#else
      (void)existing;
#endif
      return ERROR_CODE_COMMAND_DISALLOWED;
    }
    allow_connections = true; connecting = true;
    const int result = a2dp_sink_establish_stream(address.data(), &pending_cid);
    if (result) { connecting = false; pending_cid = 0; }
    return result;
  }
  int begin() {
    auto &rt = BluetoothRuntime::instance();
    if (active_ || !picow::a2dp_shared::canRegister(picow::a2dp_shared::Role::Sink, peer_) ||
        !rt.claim(BluetoothRuntime::Profile::A2dpSink)) return -1000;
    picow::a2dp_shared::registerRole(picow::a2dp_shared::Role::Sink, peer_);
    active_ = this;
    picow::a2dp_shared::setAdmission(picow::a2dp_shared::Role::Sink, [] {
      return active_ && active_->allow_connections && !active_->closing && !active_->failed &&
          !BluetoothRuntime::instance().failed();
    });
    picow::a2dp_shared::setDiscoveryAdmission(picow::a2dp_shared::Role::Sink, [] {
      return active_ && active_->callback_cid_ && active_->allow_connections && !active_->closing &&
          !active_->failed && !BluetoothRuntime::instance().failed();
    });
    int status = rt.initialize(); if (status) return status;
    rt.setHandler(BluetoothRuntime::Profile::A2dpSink, eventCallback);
    rt.setPairingPolicy(BluetoothRuntime::Profile::A2dpSink, peer_, allow_confirmation);
    rt.setSecurityLevel(BluetoothRuntime::Profile::A2dpSink, 2);
    a2dp_sink_init();
    a2dp_sink_register_packet_handler(eventCallback);
    picow::a2dp_shared::install(picow::a2dp_shared::Role::Sink);
    a2dp_sink_register_media_handler(mediaCallback);
    a2dp_sink_register_media_config_validator(validate);
    auto *ep = a2dp_sink_create_stream_endpoint(AVDTP_AUDIO, AVDTP_CODEC_SBC,
        capabilities_, sizeof(capabilities_), codec_configuration_, sizeof(codec_configuration_));
    if (!ep) return -2;
    endpoint_ = ep;
    seid = avdtp_local_seid(ep);
    a2dp_sink_create_sdp_record(sdp_record_, sdp_create_service_record_handle(),
        AVDTP_SINK_FEATURE_MASK_SPEAKER, nullptr, nullptr);
    if (de_get_len(sdp_record_) > sizeof(sdp_record_)) return -3;
    status = sdp_register_service(sdp_record_);
    if (!status) status = rt.powerOn();
    working = !status && hci_get_state() == HCI_STATE_WORKING;
    return status;
  }
  void disconnect() {
    invalidateStart();
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
    if (shared_signaling || picow::a2dp_shared::otherOwnsPeer(picow::a2dp_shared::Role::Sink, peer_)) {
      allow_connections = false;
      if (!callback_cid_ && !pending_cid && !connecting) {
        picow::a2dp_shared::pruneQueuedDiscovery(); return;
      }
      if (!role_detaching && !role_retiring && !role_cancelling &&
          !detachSharedRole() && !cancelSharedDiscovery()) {
        // Pending negotiation cancellation is deliberately fail-closed for
        // this owner. Never tear down the other role's signaling connection.
        failed = true; streaming = false; media.clear(); resetOutput();
      }
      picow::a2dp_shared::pruneQueuedDiscovery();
      return;
    }
#endif
    // The bounded consumer may still display the previous CID while a new
    // connection is already established in SDK callbacks. Close the current
    // callback-side session, never that lagging public snapshot.
    const uint16_t target = callback_cid_ ? callback_cid_ : pending_cid;
    streaming = false; media.clear(); connecting = false; pending_cid = 0; resetOutput();
    retireCallbackSession(); manual_retirement_ = true;
    if (target) { closing = true; closing_cid = target; a2dp_sink_disconnect(target); }
  }
  // The facade polls shared radio first. This method drains bounded copied work.
  void poll() {
    if (start_request_.request_id && !startWaiting()) invalidateStart();
    if (reject_cid_) { const auto cid = reject_cid_; reject_cid_ = 0; rejectSession(cid); }
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
    if (role_cancelling && !events.count) {
      const int status = callbackIdentityLive() ? picow_a2dp_poll_role_cancel(AVDTP_ROLE_SINK, callback_cid_) : ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
      if (!cancel_timed_out) role_last_status = status;
      if (!status) {
        retireCallbackSession(); manual_retirement_ = true;
        const bool timed_out = cancel_timed_out;
        clearRoleDetach(); cancel_timed_out = timed_out; closing = false; closing_cid = 0;
        signaling = configured = streaming = false; cid = 0; stream_state = 0;
        connecting = false; pending_cid = 0; sequence.resetStream();
        picow::a2dp_shared::resumeQueuedDiscovery();
      } else if (status != ERROR_CODE_COMMAND_DISALLOWED) {
        failed = true; role_cancelling = false; closing = false;
      } else if (!cancel_timed_out && int32_t(btstack_run_loop_get_time_ms() - role_deadline_ms) >= 0) {
        // Do not clear the SDK cancellation or initiator state on timeout.
        // Existing other-role media survives; this owner cannot reconnect.
        failed = true; cancel_timed_out = true; role_last_status = ERROR_CODE_CONNECTION_TIMEOUT;
      }
    }
    if (role_retiring && !events.count) {
      role_last_status = callbackIdentityLive() ? picow_a2dp_retire_role(AVDTP_ROLE_SINK, cid, seid) : ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
      if (!role_last_status) {
        retireCallbackSession(); manual_retirement_ = true;
        clearRoleDetach(); closing = false; closing_cid = 0;
        signaling = configured = streaming = false; cid = 0; stream_state = 0;
        connecting = false; pending_cid = 0; sequence.resetStream();
      } else if (role_last_status != ERROR_CODE_COMMAND_DISALLOWED) {
        failed = true; role_retiring = false; closing = false;
      }
    }
    if ((role_detaching || role_retiring) && !events.count &&
        int32_t(btstack_run_loop_get_time_ms() - role_deadline_ms) >= 0) {
      role_last_status = ERROR_CODE_CONNECTION_TIMEOUT; failed = true;
      role_detaching = role_retiring = false; closing = false;
    }
#endif
    if (events.overflow || events.oversize || identity_exhausted) {
      if (!failed) { failed = true; allow_connections = false; disconnect(); signaling = configured = false; cid = 0; stream_state = 0; }
      events.clear(); return;
    }
    // Merge the copied control/media queues by callback order. START cannot
    // discard its following packet; SUSPEND cannot discard its preceding one.
    // Bound decoder work per loop to retain SPP/USB fairness.
    unsigned budget = 0;
    while (events.count || media.count) {
      if (events.count && (!media.count || events.front().order < media.front().order)) {
        processEvent(events.front()); events.pop(); continue;
      }
      if (budget++ == 2) break;
      const auto &p = media.front();
      if (p.loss != active_media_loss_) { fragments.reset(); active_media_loss_ = p.loss; }
      if (!streaming || !configured || p.tag != seid || p.epoch != active_generation_) {
        ++wrong_state; fragments.reset();
      } else {
        picow::sink::PacketView view;
        const uint8_t *decoded_packet = p.data.data();
        size_t decoded_size = p.size;
        const auto error = fragments.receive(decoded_packet, decoded_size, config, view);
        if (error != picow::sink::ParseError::None) ++parse_errors[size_t(error)];
        else if (sequence.accept(view)) {
          ++processed;
          for (unsigned i = 0; i < view.count; ++i) decoder.decode(decoded_packet, decoded_size, view.frames[i]);
        } else fragments.reset();
      }
      media.pop();
    }
  }
private:
  A2dpSinkReceiver::StartRequest start_request_{};
  uint64_t next_start_id_ = 0;
  uint8_t start_transaction_ = 0;
  bool start_notified_ = false;
  void invalidateStart() { start_request_ = {}; start_notified_ = false; }
  bool startWaiting() const {
#ifdef PICOW_A2DP_SINK_START_DECISIONS
    if (!require_start_decision || !start_request_.request_id || !callbackSessionLive() ||
        start_request_.generation != callback_generation_ || failed || closing ||
        identity_exhausted || BluetoothRuntime::instance().failed()) return false;
    const auto *connection = avdtp_get_connection_for_avdtp_cid(callback_cid_);
    const auto *ep = avdtp_get_stream_endpoint_for_seid(seid);
    return connection && ep && ep == endpoint_ && ep->connection == connection &&
        connection->avdtp_cid == callback_cid_ && connection->con_handle == callback_handle_ &&
        std::memcmp(connection->remote_addr, peer_.data(), peer_.size()) == 0 &&
        connection->state == AVDTP_SIGNALING_CONNECTION_OPENED &&
        connection->acceptor_local_seid == seid &&
        connection->acceptor_transaction_label == start_transaction_ &&
        connection->acceptor_signaling_packet.signal_identifier == AVDTP_SI_START &&
        ep->sep.type == AVDTP_SINK && ep->sep.media_type == AVDTP_AUDIO && ep->sep.in_use &&
        ep->state == AVDTP_STREAM_ENDPOINT_OPENED &&
        ep->acceptor_config_state == AVDTP_ACCEPTOR_W4_USER_CONFIRM_START_STREAM;
#else
    return false;
#endif
  }
  bool startReady() const {
    return startWaiting() && working && signaling && configured && !connecting &&
        active_generation_ == callback_generation_ && cid == callback_cid_ &&
        !events.count && !events.overflow && !events.oversize && !reject_cid_;
  }
  Address peer_;
  avdtp_stream_endpoint_t *endpoint_ = nullptr;
  uint16_t reject_cid_ = 0;
  uint64_t callback_order_ = 0, callback_media_loss_ = 0, active_media_loss_ = 0;
  uint32_t callback_generation_ = 0, active_generation_ = 0, callback_acl_epoch_ = 0;
  uint16_t callback_cid_ = 0, callback_handle_ = 0;
  uint16_t retired_cid_ = 0, retired_handle_ = 0;
  uint32_t retired_generation_ = 0, retired_acl_epoch_ = 0;
  uint8_t retired_release_mask_ = 0;
  bool retired_valid_ = false, manual_retirement_ = false;
  void retireCallbackSession() {
    invalidateStart();
    if (!callback_cid_) return;
    retired_cid_ = callback_cid_; retired_handle_ = callback_handle_;
    retired_generation_ = callback_generation_; retired_acl_epoch_ = callback_acl_epoch_;
    retired_release_mask_ = 0; retired_valid_ = true; callback_cid_ = 0;
  }
  bool retiredRelease(const uint8_t *p, uint16_t n) {
    const bool stream = p[2] == A2DP_SUBEVENT_STREAM_RELEASED;
    if ((!stream && p[2] != A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED) ||
        n != (stream ? 6 : 5) || !retired_valid_ || callback_cid_ ||
        retired_generation_ != callback_generation_ || little_endian_read_16(p, 3) != retired_cid_ ||
        (stream && p[5] != seid)) return false;
    const auto link = BluetoothRuntime::instance().linkForPeer(peer_);
    if (link.active && (link.epoch != retired_acl_epoch_ || link.handle != retired_handle_)) return false;
    const uint8_t bit = stream ? 1 : 2;
    if (!(retired_release_mask_ & bit)) {
      events.push(p, n, 1, retired_generation_, ++callback_order_);
      retired_release_mask_ |= bit;
    }
    return true; // Duplicate releases are harmless, but cannot mutate state.
  }
  static inline A2dpSinkProfile *active_ = nullptr;
  uint8_t capabilities_[4]{0x3f, 0xff, 2, 86};
  uint8_t codec_configuration_[4]{};
  uint8_t sdp_record_[200]{};
  static picow::sink::Config configuration(const uint8_t *p) {
    const uint8_t mode = a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(p);
    return {a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(p),
            uint8_t(mode >= AVDTP_CHANNEL_MODE_MONO && mode <= AVDTP_CHANNEL_MODE_JOINT_STEREO ? mode - 1 : 255), p[16], p[17], p[13], p[14],
            uint8_t(p[15] == AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS ? 0 : p[15] == AVDTP_SBC_ALLOCATION_METHOD_SNR ? 1 : 255)};
  }
  static bool validCodec(const uint8_t *p, uint16_t n) {
    return p && n == 18 && p[1] + 2u == n &&
        (p[0] == HCI_EVENT_A2DP_META || p[0] == HCI_EVENT_AVDTP_META) &&
        p[2] == A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION &&
        p[5] == active_->seid && p[8] == AVDTP_AUDIO && configuration(p).valid() && p[12] == configuration(p).channels();
  }
  bool callbackIdentityLive() const {
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
    const auto *connection = avdtp_get_connection_for_avdtp_cid(callback_cid_);
    if (!connection || connection->con_handle != callback_handle_ ||
        std::memcmp(connection->remote_addr, peer_.data(), peer_.size()) != 0) return false;
#endif
    const auto link = BluetoothRuntime::instance().linkForPeer(peer_);
    return callback_cid_ && link.active &&
        link.epoch == callback_acl_epoch_ && link.handle == callback_handle_;
  }
  bool callbackSessionLive() const { return allow_connections && callbackIdentityLive(); }
  static uint8_t validate(const avdtp_stream_endpoint_t *, const uint8_t *p, uint16_t n) {
    if (active_) active_->invalidateStart();
    // This callback is synchronous inside the radio poll, before the copied
    // signaling event is consumed. Use the callback-side authenticated session.
    return validCodec(p, n) && active_->callbackSessionLive() &&
        little_endian_read_16(p, 3) == active_->callback_cid_ ? 0 : AVDTP_ERROR_CODE_UNSUPPORTED_CONFIGURATION;
  }
  static void eventCallback(uint8_t type, uint16_t, uint8_t *p, uint16_t n) {
    if (!active_ || type != HCI_EVENT_PACKET || !p) return;
    auto &s = *active_;
    if (n < 2 || p[1] + 2u != n) { ++s.malformed_events; return; }
    if (p[0] == HCI_EVENT_A2DP_META) {
      if (n < 3) { ++s.malformed_events; return; }
      const uint8_t kind = p[2];
      if (kind == A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED && n == 5 &&
          s.closing && little_endian_read_16(p, 3) == s.closing_cid) {
        s.closing = false; s.closing_cid = 0;
        if (!s.callback_cid_ && !s.retired_valid_) return;
      }
      if (s.retiredRelease(p, n)) return;
      if (kind == A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED) {
        if (n != 14) { ++s.malformed_events; return; }
        if (p[13]) {
          Address peer{}; a2dp_subevent_signaling_connection_established_get_bd_addr(p, peer.data());
          const auto incoming = little_endian_read_16(p, 3);
          // SDK cancels a not-yet-open connection with ESTABLISHED(error),
          // not RELEASED. Retire only this exact pending/cancelled request.
          if (peer != s.peer_ || !incoming ||
              !((s.connecting && incoming == s.pending_cid) ||
                (s.closing && incoming == s.closing_cid))) {
            ++s.malformed_events; return;
          }
          if (s.closing && incoming == s.closing_cid) {
            s.closing = false; s.closing_cid = 0;
          }
        } else {
          Address peer{}; a2dp_subevent_signaling_connection_established_get_bd_addr(p, peer.data());
          if (peer != s.peer_ && picow::a2dp_shared::otherOwnsPeer(picow::a2dp_shared::Role::Sink, peer)) return;
          const auto link = BluetoothRuntime::instance().linkForPeer(s.peer_);
          const auto incoming = little_endian_read_16(p, 3);
          if (!s.allow_connections || s.closing || peer != s.peer_ || !incoming || !link.active || link.handle != little_endian_read_16(p, 11) ||
              (s.callback_cid_ && (s.callback_cid_ != incoming || s.callback_acl_epoch_ != link.epoch))) {
            ++s.rejected_peers;
            if (incoming && incoming != s.callback_cid_) {
              if (s.reject_cid_ && s.reject_cid_ != incoming) s.identity_exhausted = true;
              else s.reject_cid_ = incoming;
            }
            return;
          }
          // A duplicate establishment must not silently restart the session.
          if (s.callback_cid_) { ++s.malformed_events; return; }
          s.retired_valid_ = false; s.manual_retirement_ = false;
          s.callback_cid_ = incoming; s.callback_handle_ = link.handle; s.callback_acl_epoch_ = link.epoch;
          if (s.callback_generation_ == UINT32_MAX) {
            s.identity_exhausted = true; s.allow_connections = false; return;
          }
          ++s.callback_generation_;
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
          s.shared_signaling = picow::a2dp_shared::otherOwnsPeer(picow::a2dp_shared::Role::Sink, peer);
#endif
        }
      } else {
        uint16_t expected = 0;
        switch (kind) {
          case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION: expected = 18; break;
          case A2DP_SUBEVENT_STREAM_ESTABLISHED: expected = 14; break;
#ifdef PICOW_A2DP_SINK_START_DECISIONS
          case A2DP_SUBEVENT_START_STREAM_REQUESTED: expected = 6; break;
#endif
          case A2DP_SUBEVENT_STREAM_STARTED:
          case A2DP_SUBEVENT_STREAM_SUSPENDED:
          case A2DP_SUBEVENT_STREAM_STOPPED:
          case A2DP_SUBEVENT_STREAM_RELEASED: expected = 6; break;
          case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED: expected = 5; break;
          default: return; // Not a state transition used by this adapter.
        }
        // A release can arrive while the runtime is completing ACL teardown.
        if (n != expected || !s.callback_cid_ || little_endian_read_16(p, 3) != s.callback_cid_) { ++s.malformed_events; return; }
        if ((expected == 6 || expected == 18) && p[5] != s.seid) { ++s.malformed_events; return; }
        if (kind == A2DP_SUBEVENT_STREAM_ESTABLISHED) {
          Address peer{}; a2dp_subevent_stream_established_get_bd_addr(p, peer.data());
          if (peer != s.peer_ || p[11] != s.seid) { ++s.malformed_events; return; }
        }
        bool session_live = s.callbackSessionLive();
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
        if (s.role_detaching || s.role_retiring || s.role_cancelling) {
          if (kind != A2DP_SUBEVENT_STREAM_RELEASED && kind != A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED) return;
          session_live = s.callbackIdentityLive();
        }
#endif
        if (kind != A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED && !session_live) { ++s.malformed_events; return; }
        if (kind == A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION && !validCodec(p, n)) { ++s.malformed_events; return; }
      }
#ifdef PICOW_A2DP_SINK_START_DECISIONS
      if (kind == A2DP_SUBEVENT_START_STREAM_REQUESTED) {
        // Repeated SDK can-send notification does not renew the token.
        if (s.startWaiting()) s.start_notified_ = true;
        return;
      }
#endif
      // All remaining used A2DP events are lifecycle/configuration boundaries.
      s.invalidateStart();
      s.events.push(p, n, 0, s.callback_generation_, ++s.callback_order_);
      if (kind == A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED) { s.retireCallbackSession(); s.retired_release_mask_ |= 2; }
      return;
    }
    if (p[0] == HCI_EVENT_DISCONNECTION_COMPLETE) {
      if (n != 6) { ++s.malformed_events; return; }
      if (p[2]) return; // Failed disconnection is not confirmation of retirement.
      const auto handle = little_endian_read_16(p, 3);
      if (s.callback_cid_) {
        if (handle != s.callback_handle_) return;
        s.events.push(p, n, 0, s.callback_generation_, ++s.callback_order_);
        s.retireCallbackSession(); s.retired_release_mask_ |= 4;
      } else {
        // Explicit audio disconnect/HALTING can retire the callback identity
        // before HCI closes the ACL, even without an AVDTP release callback.
        if (!s.retired_valid_ || handle != s.retired_handle_ ||
            s.retired_generation_ != s.callback_generation_ || (s.retired_release_mask_ & 4)) return;
        const auto link = BluetoothRuntime::instance().linkForPeer(s.peer_);
        if (link.active && (link.epoch != s.retired_acl_epoch_ || link.handle != s.retired_handle_)) return;
        s.events.push(p, n, 0, s.retired_generation_, ++s.callback_order_);
        s.retired_release_mask_ |= 4;
      }
    } else if (p[0] == BTSTACK_EVENT_STATE || p[0] == HCI_EVENT_CONNECTION_COMPLETE) {
      if ((p[0] == BTSTACK_EVENT_STATE && n != 3) || (p[0] == HCI_EVENT_CONNECTION_COMPLETE && n != 13)) { ++s.malformed_events; return; }
      s.events.push(p, n, 0, s.callback_generation_, ++s.callback_order_);
      if (p[0] == BTSTACK_EVENT_STATE && p[2] != HCI_STATE_WORKING) s.retireCallbackSession();
    }
  }
  static void mediaCallback(uint8_t seid, uint8_t *p, uint16_t n) {
    if (!active_) return;
    auto &s = *active_; ++s.received; s.bytes += n;
    if (!s.callbackSessionLive() || seid != s.seid) { ++s.wrong_state; return; }
    if (!s.media.push(p, n, seid, s.callback_generation_, ++s.callback_order_, s.callback_media_loss_)) {
      // Subsequent copied packets carry this ordered discontinuity. No partial
      // frame may cross a dropped callback, even if its RTP sequence is reused.
      if (++s.callback_media_loss_ == 0) s.failed = true;
    }
  }
  void processEvent(const picow::sink::CopyQueue<16, 64>::Packet &event) {
    const auto *p = event.data.data(); const auto n = event.size;
    if (p[0] == BTSTACK_EVENT_STATE && n == 3) {
      working = p[2] == HCI_STATE_WORKING;
      if (!working) {
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
        clearRoleDetach();
#endif
        resetOutput(); closing = false; closing_cid = 0; connecting = false; pending_cid = 0; signaling = streaming = configured = false; cid = 0; stream_state = 0; sequence.resetStream(); }
      return;
    }
    if (p[0] == HCI_EVENT_DISCONNECTION_COMPLETE) {
      if (event.epoch == active_generation_) {
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
        clearRoleDetach();
#endif
        resetOutput(); closing = false; closing_cid = 0; signaling = streaming = configured = false; cid = 0; stream_state = 0; sequence.resetStream(); ++releases; }
      return;
    }
    if (p[0] == HCI_EVENT_CONNECTION_COMPLETE) return; // Other profiles may own other peers.
    if (n < 3) { ++malformed_events; return; }
    if (event.tag == 1) {
      // Only callback-validated releases of the one retired session get this
      // tag. They may follow HALTING/HCI close after active cid was cleared.
      if (event.epoch == active_generation_) {
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
        clearRoleDetach();
#endif
        resetOutput(); signaling = streaming = configured = false; cid = 0; stream_state = 0;
        sequence.resetStream(); ++releases;
      }
      return;
    }
    if (p[2] == A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED && n == 14 && p[13]) {
      if (event.epoch == callback_generation_) {
        connecting = false; pending_cid = 0;
        last_event = p[2]; last_status = p[13]; ++control_sequence;
      }
      return;
    }
    if (manual_retirement_ && retired_valid_ && event.epoch == retired_generation_ &&
        p[2] != A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED &&
        p[2] != A2DP_SUBEVENT_STREAM_RELEASED) return;
    if (p[2] != A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED &&
        (event.epoch != active_generation_ || n < 5 || little_endian_read_16(p, 3) != cid)) { ++malformed_events; return; }
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
    if (role_detaching || role_retiring || role_cancelling) {
      if (!role_cancelling && p[2] == A2DP_SUBEVENT_STREAM_RELEASED) {
        role_detaching = false; role_retiring = true;
        streaming = false; media.clear(); resetOutput(); ++releases; return;
      }
      if (p[2] != A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED) return;
    }
#endif
    last_event = p[2]; ++control_sequence;
    switch (p[2]) {
      case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED: {
        if (n < 14) { ++malformed_events; break; }
        connecting = false; pending_cid = 0;
        last_status = a2dp_subevent_signaling_connection_established_get_status(p);
        if (last_status) break;
        const auto incoming = a2dp_subevent_signaling_connection_established_get_a2dp_cid(p);
        Address address{}; a2dp_subevent_signaling_connection_established_get_bd_addr(p, address.data());
        if (!allow_connections || address != peer_) { ++rejected_peers; rejectSession(incoming); break; }
        cid = incoming; active_generation_ = event.epoch; signaling = true; configured = streaming = false; stream_state = 0; break;
      }
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
        if (!validCodec(p, uint16_t(n))) { ++malformed_events; break; }
        config = configuration(p); configured = true; active_generation_ = event.epoch; sequence.resetStream(); resetOutput(); decoder.configure(config); break;
      case A2DP_SUBEVENT_STREAM_ESTABLISHED: {
        if (n < 14) { ++malformed_events; break; }
        last_status = a2dp_subevent_stream_established_get_status(p);
        if (last_status) break;
        Address address{}; a2dp_subevent_stream_established_get_bd_addr(p, address.data());
        if (address != peer_ || !allow_connections) { ++rejected_peers; rejectSession(a2dp_subevent_stream_established_get_a2dp_cid(p)); break; }
        if (p[11] != seid) { ++malformed_events; break; }
        stream_state = 1; break;
      }
      case A2DP_SUBEVENT_STREAM_STARTED:
      case A2DP_SUBEVENT_STREAM_SUSPENDED:
      case A2DP_SUBEVENT_STREAM_STOPPED:
      case A2DP_SUBEVENT_STREAM_RELEASED:
        if (n < 6 || little_endian_read_16(p, 3) != cid || p[5] != seid) { ++malformed_events; break; }
        active_generation_ = event.epoch; sequence.resetStream(); resetOutput();
        if (p[2] == A2DP_SUBEVENT_STREAM_STARTED && configured && allow_connections) { streaming = true; stream_state = 2; ++starts; decoder.configure(config); }
        else { streaming = false; stream_state = p[2] == A2DP_SUBEVENT_STREAM_SUSPENDED ? 3 : 0; if (stream_state == 3) ++suspends; else ++releases; }
        break;
      case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
        if (n < 5 || little_endian_read_16(p, 3) != cid) { ++malformed_events; break; }
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
        clearRoleDetach();
#endif
        resetOutput(); signaling = streaming = configured = false; cid = 0; stream_state = 0; ++releases; active_generation_ = event.epoch; sequence.resetStream(); break;
      default: break;
    }
  }
};
