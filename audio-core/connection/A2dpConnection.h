#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace a2dp {
using Address = std::array<uint8_t, 6>;
// Identifies one current shared-runtime ACL incarnation, not authentication.
struct AclBinding {
  Address peer{};
  uint32_t epoch = 0;
  uint16_t handle = 0;
};
inline bool operator==(const AclBinding &a, const AclBinding &b) {
  return a.peer == b.peer && a.epoch == b.epoch && a.handle == b.handle;
}
inline bool operator!=(const AclBinding &a, const AclBinding &b) { return !(a == b); }
enum class Power { Off, Starting, Working };
enum class Inquiry { Idle, Active, Stopping };
enum class Signaling { Idle, Connecting, Connected };
enum class Stream { Idle, Configuring, Open, StartRequested, Playing,
                    SuspendRequested, Suspended, ReconfigureRequested };
enum class Credit { None, Requested, Granted };
enum class Error {
  None,
  InvalidInput,
  UnsupportedOperation,
  Failed,
  UnsupportedCodec,
  QueueOverflow,
  ClockRegression
};
enum class Kind {
  Initialize,
  Initialized,
  PowerRequested,
  PowerWorking,
  DiscoveryRequested,
  DeviceSeen,
  InquiryFinished,
  ConfirmationRequested,
  PairingFinished,
  SignalingEstablished,
  CapabilitiesObserved,
  CodecConfigured,
  StreamOpened,
  TransportConfigured,
  DisconnectRequested,
  StreamStarted,
  Tick,
  SendCreditGranted,
  MediaResult,
  StreamReleased,
  StreamSuspended,
  CommandRejected,
  UnsupportedPairing,
  SignalingReleased,
  ActionFailed,
  Observed,
  MediaDeferred,
  ExistingAclConnectRequested,
  ExistingAclInvalidated,
  ExistingAclRuntimeStopped,
  ListenRequested, PauseRequested, ResumeRequested, ReconfigureRequested,
  StreamReconfigured, StreamStopped, ControlFailed, DelayReported
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
  , SharedDetachRequested, RoleCloseAccepted, RoleCloseFailed, RoleRetireDeferred, RoleDetached, SharedOwnershipObserved
#endif
  , MediaProgress
};
enum class Op {
  Initialize,
  PowerOn,
  StartInquiry,
  StopInquiry,
  EstablishA2dp,
  ConfirmPairing,
  ConfigureAudio,
  ConfigureTransport,
  StartStream,
  EnableStreaming,
  DisableStreaming,
  RequestCanSend,
  GrantTransportCredit,
  PumpAudioOnce,
  DisconnectA2dp,
  RejectConnection,
  ClearConnection,
  EstablishA2dpOnExistingAcl,
  SetConnectable, PauseStream, ReconfigureStream
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
  , CloseRoleMedia, RetireRole
#endif
};
struct Codec {
  uint32_t rate = 48000;
  uint8_t blocks = 16, bands = 8, min_pool = 2, max_pool = 53;
  bool stereo = true, loudness = true;
};
struct Config {
  Address target{};
  uint64_t start_delay_us = 4000000, play_duration_us = 3000000;
  uint16_t minimum_payload = 473;
  bool auto_connect = false;
  bool auto_start = true, allow_sample_rate_change = false;
  uint32_t sample_rate = 48000;
  uint64_t control_timeout_us = 10000000;
};
struct State {
  Config config{};
  AclBinding existing_acl{};
  bool existing_acl_bound = false, existing_acl_stopped = false;
  Power power = Power::Off;
  Inquiry inquiry = Inquiry::Idle;
  Signaling signaling = Signaling::Idle;
  Stream stream = Stream::Idle;
  Credit credit = Credit::None;
  bool connect_intent = false;
  bool accepting = false, incoming = false, automatic_start = false;
  bool reconfigure_seen = false, reconfigure_rejected = false;
  AclBinding incoming_acl{};
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
  AclBinding shared_acl{};
  bool shared_signaling = false, detaching_role = false;
  bool role_close_accepted = false, role_media_released = false, role_retire_requested = false;
  bool detach_previous_accepting = false, detach_stop_on_failure = false;
#endif
  Stream control_previous = Stream::Idle;
  Codec previous_codec{};
  uint32_t requested_rate = 0;
  uint64_t control_due_us = 0;
  uint16_t delay_100us = 0;
  uint64_t delay_reports = 0;
  bool initialized = false, initializing = false, found = false, paired = false,
       pairing_replied = false, codec_ready = false, media_pending = false,
       closed = false;
  uint16_t cid = 0, payload_limit = 0;
  uint8_t seid = 0;
  Codec codec{};
  uint64_t now_us = 0, start_due_us = 0, next_due_us = 0, end_due_us = 0,
           fraction = 0, packets = 0, samples = 0, media_deferrals = 0;
  // Pacing stays anchored to accepted sample time. Delayed credit must not
  // silently turn a temporary stall into a permanently slower media clock.
  uint64_t max_lateness_us = 0, pacing_rebases = 0, pacing_discarded_us = 0;
  Error error = Error::None;
  int status = 0;
  uint64_t rejected = 0;
  uint64_t completed_media = 0;
};
struct Event {
  Kind kind = Kind::Observed;
  uint64_t now_us = 0;
  Address address{};
  AclBinding acl{};
  uint16_t cid = 0, payload_limit = 0;
  uint8_t seid = 0;
  int status = 0;
  uint16_t raw_type = 0, raw_subevent = 0, command = 0;
  Codec codec{};
  bool enabled = false, reconfigure = false;
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
  bool shared_signaling = false, detach_stop_on_failure = false;
#endif
  // Samples/channel accepted by MediaResult. Default preserves fixed-four-frame
  // executors; adaptive executors must explicitly supply their actual count.
  uint16_t accepted_samples = 512;
};
struct Action {
  Op op;
  uint16_t cid = 0;
};
struct Transition {
  State next;
  std::array<Action, 4> actions{};
  size_t count = 0;
  void add(Op op) { actions[count++] = {op}; }
};
struct Rules {
  static Transition apply(const State &s, const Event &e) {
    Transition t{s};
    auto &n = t.next;
    auto stop = [&]() {
      n.stream = Stream::Idle;
      n.credit = Credit::None;
      n.media_pending = false;
      n.automatic_start = false;
      n.control_due_us = 0;
      t.add(Op::DisableStreaming);
    };
    auto fail = [&](Error err) {
      n.error = err;
      n.status = e.status;
      stop();
    };
    auto reject = [&]() { ++n.rejected; };
    if (e.kind == Kind::Observed || e.kind == Kind::CapabilitiesObserved)
      return t;
    if (e.now_us < s.now_us) {
      fail(Error::ClockRegression);
      return t;
    }
    n.now_us = e.now_us;
    const bool match = s.cid && e.cid == s.cid && e.seid == s.seid;
    // Release remains accepted after errors; recovery/reconnect is
    // intentionally not implemented.
    auto clearConnection = [&]() {
      stop();
      n.signaling = Signaling::Idle;
      n.existing_acl_bound = false;
      n.existing_acl = {};
      n.incoming = false;
      n.incoming_acl = {};
      n.requested_rate = 0;
      n.reconfigure_seen = n.reconfigure_rejected = false;
      n.cid = 0;
      n.codec_ready = false;
      n.closed = false;
      n.connect_intent = false;
      n.found = false;
      n.paired = false;
      n.pairing_replied = false;
      n.payload_limit = 0;
      n.codec = Codec{};
      n.start_due_us = n.next_due_us = n.end_due_us = n.fraction = 0;
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
      n.shared_signaling = n.detaching_role = n.role_close_accepted = false;
      n.role_media_released = n.role_retire_requested = n.detach_stop_on_failure = false;
      n.shared_acl = {};
#endif
      t.add(Op::ClearConnection);
    };
    if (e.kind == Kind::SignalingReleased && s.cid && e.cid == s.cid) {
      clearConnection();
      return t;
    }
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
    if (e.kind == Kind::RoleDetached) {
      if (!match || !s.detaching_role || !s.role_retire_requested || e.status) {
        reject(); return t;
      }
      clearConnection(); n.status = 0;
      return t;
    }
    // Never let shared-session cleanup fall through to a global disconnect.
    // Pending/failed transactions are rejected here, not forcefully torn down.
    if (e.kind == Kind::DisconnectRequested && s.shared_signaling) {
      if (s.detaching_role) return t;
      if (s.error == Error::None && s.connect_intent && !s.control_due_us &&
          (s.stream == Stream::Open || s.stream == Stream::Playing || s.stream == Stream::Suspended)) {
        auto detach = e; detach.kind = Kind::SharedDetachRequested;
        detach.detach_stop_on_failure = true;
        return apply(s, detach);
      }
      n.accepting = n.connect_intent = false;
      // Cancelling an in-flight SDK transaction safely is a separate task.
      // Stop this owner, report failure, and never cut the other role's link.
      fail(s.error == Error::None ? Error::UnsupportedOperation : s.error);
      return t;
    }
#endif
    if ((e.kind == Kind::StreamReleased || e.kind == Kind::StreamStopped) && match) {
      stop();
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
      if (s.detaching_role) {
        n.control_due_us = s.control_due_us;
        if (e.kind == Kind::StreamReleased) n.role_media_released = true;
      }
#endif
      // CLOSE may be followed by OPEN on the same configured SEP without a
      // new codec event. Preserve format until signaling release or replacement.
      return t;
    }
    // Cleanup remains available after a terminal error.
    if (e.kind == Kind::DisconnectRequested && s.error != Error::None) {
      n.accepting = n.connect_intent = false;
      stop();
      if (s.cid) t.add(Op::DisconnectA2dp);
      return t;
    }
    if (e.kind == Kind::ExistingAclRuntimeStopped) {
      n.existing_acl_stopped = true;
      if (s.existing_acl_bound) {
        n.connect_intent = false;
        fail(Error::Failed);
      }
      return t;
    }
    if (e.kind == Kind::ExistingAclInvalidated) {
      if (!s.existing_acl_bound || s.existing_acl_stopped) { reject(); return t; }
      n.existing_acl_stopped = true;
      n.connect_intent = false;
      fail(Error::Failed);
      if (s.cid) t.add(Op::DisconnectA2dp);
      return t;
    }
    // A delayed SDK completion may not revive a cancelled or invalidated
    // opt-in request. SDK callbacks carry no epoch: adapter supplies the
    // current runtime identity and checks the SDK-reported ACL handle.
    if (e.kind == Kind::SignalingEstablished && s.existing_acl_bound &&
        (s.error != Error::None || !s.connect_intent ||
         e.acl != s.existing_acl || e.address != s.config.target)) {
      reject();
      n.connect_intent = false;
      n.existing_acl_stopped = true;
      fail(Error::Failed);
      if (!e.status && e.cid) t.actions[t.count++] = {Op::RejectConnection, e.cid};
      return t;
    }
    if (s.error != Error::None) {
      reject();
      return t;
    }
    auto controlDeadline = [&]() {
      if (s.config.control_timeout_us > UINT64_MAX - e.now_us) {
        fail(Error::ClockRegression); return false;
      }
      n.control_due_us = e.now_us + s.config.control_timeout_us;
      return true;
    };
    switch (e.kind) {
    case Kind::ListenRequested:
      if (!s.initialized || s.power != Power::Working ||
          (e.enabled && (s.inquiry != Inquiry::Idle || s.signaling == Signaling::Connecting))) { reject(); break; }
      n.status = 0;
      n.accepting = e.enabled;
      if (e.enabled) t.add(Op::SetConnectable);
      break;
    case Kind::Initialize:
      if (s.initialized || s.initializing) {
        reject();
        break;
      }
      n.initializing = true;
      t.add(Op::Initialize);
      break;
    case Kind::Initialized:
      if (!s.initializing) {
        reject();
        break;
      }
      if (e.status || !e.seid) {
        fail(Error::Failed);
        break;
      }
      n.initializing = false;
      n.initialized = true;
      n.seid = e.seid;
      break;
    case Kind::PowerRequested:
      if (!s.initialized || s.power != Power::Off) {
        reject();
        break;
      }
      n.power = Power::Starting;
      n.connect_intent = s.config.auto_connect;
      t.add(Op::PowerOn);
      break;
    case Kind::PowerWorking:
      if (s.power != Power::Starting) {
        reject();
        break;
      }
      n.power = Power::Working;
      if (s.connect_intent) {
        n.inquiry = Inquiry::Active;
        n.found = false;
        t.add(Op::StartInquiry);
      }
      break;
    case Kind::DiscoveryRequested:
      if (s.power != Power::Working || s.inquiry != Inquiry::Idle ||
          s.signaling != Signaling::Idle) {
        reject();
        break;
      }
      n.connect_intent = true;
      n.inquiry = Inquiry::Active;
      n.found = false;
      t.add(Op::StartInquiry);
      break;
    case Kind::ExistingAclConnectRequested:
      if (!s.initialized || s.existing_acl_stopped ||
          s.power != Power::Working || s.inquiry != Inquiry::Idle ||
          s.signaling != Signaling::Idle || s.stream != Stream::Idle ||
          s.connect_intent || s.cid || e.acl.peer != s.config.target ||
          e.acl.peer == Address{} || !e.acl.epoch || e.acl.handle > 0x0eff) {
        reject();
        break;
      }
      n.connect_intent = true;
      n.existing_acl_bound = true;
      n.existing_acl = e.acl;
      n.signaling = Signaling::Connecting;
      // Do not assert found/paired, stop inquiry, or bypass SDP/AVDTP.
      t.add(Op::EstablishA2dpOnExistingAcl);
      break;
    case Kind::DeviceSeen:
      if (s.inquiry == Inquiry::Active && e.address == s.config.target) {
        n.found = true;
        n.inquiry = Inquiry::Stopping;
        t.add(Op::StopInquiry);
      }
      break;
    case Kind::InquiryFinished:
      if (s.inquiry == Inquiry::Idle) {
        reject();
        break;
      }
      if (e.status) {
        fail(Error::Failed);
        break;
      }
      n.inquiry = Inquiry::Idle;
      if (s.found && s.connect_intent) {
        n.signaling = Signaling::Connecting;
        t.add(Op::EstablishA2dp);
      }
      break;
    case Kind::ConfirmationRequested:
      if (e.address != s.config.target ||
          (s.signaling != Signaling::Connecting && !s.accepting) || s.pairing_replied) {
        reject();
        break;
      }
      n.pairing_replied = true;
      t.add(Op::ConfirmPairing);
      break;
    case Kind::PairingFinished:
      if (e.address != s.config.target ||
          (s.signaling != Signaling::Connecting && !s.accepting)) {
        reject();
        break;
      }
      n.status = e.status;
      if (e.status)
        fail(Error::Failed);
      else
        n.paired = true;
      break;
    case Kind::SignalingEstablished:
      if (s.accepting && s.power == Power::Working &&
          s.inquiry == Inquiry::Idle && s.signaling == Signaling::Idle &&
          !s.cid && !e.status && e.cid && e.address == s.config.target &&
          e.acl.peer == s.config.target && e.acl.epoch && e.acl.handle <= 0x0eff) {
        n.cid = e.cid; n.signaling = Signaling::Connected;
        n.connect_intent = true; n.incoming = true; n.incoming_acl = e.acl;
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
        n.shared_signaling = e.shared_signaling; n.shared_acl = e.acl;
#endif
        break;
      }
      if (!s.connect_intent || s.signaling != Signaling::Connecting ||
          e.address != s.config.target) {
        reject();
        if (!s.connect_intent && s.signaling == Signaling::Connecting)
          n.signaling = Signaling::Idle;
        if (!e.status && e.cid && e.cid != s.cid) {
          t.actions[t.count++] = {Op::RejectConnection, e.cid};
        }
        break;
      }
      if (e.status || !e.cid) {
        fail(Error::Failed);
        break;
      }
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
      if (e.shared_signaling && (e.acl.peer != s.config.target || !e.acl.epoch || e.acl.handle > 0x0eff)) {
        fail(Error::InvalidInput); break;
      }
      n.shared_signaling = e.shared_signaling; n.shared_acl = e.acl;
#endif
      n.cid = e.cid;
      n.signaling = Signaling::Connected;
      break;
    case Kind::CodecConfigured:
      if (!match || !s.connect_intent || s.signaling != Signaling::Connected ||
          (!e.reconfigure && s.codec_ready && s.stream != Stream::Idle) ||
          (e.reconfigure && (!s.codec_ready || (s.stream != Stream::Open &&
             s.stream != Stream::Suspended && s.stream != Stream::ReconfigureRequested)))) {
        reject(); break;
      }
      if ((e.codec.rate != 48000 && e.codec.rate != 44100) ||
          (!s.config.allow_sample_rate_change && e.codec.rate != s.config.sample_rate) ||
          (s.stream == Stream::ReconfigureRequested && e.codec.rate != s.requested_rate) ||
          e.codec.blocks != 16 || e.codec.bands != 8 || !e.codec.stereo ||
          !e.codec.loudness || e.codec.min_pool < 2 || e.codec.max_pool > 53 ||
          e.codec.min_pool > e.codec.max_pool) {
        fail(Error::UnsupportedCodec);
        break;
      }
      n.codec = e.codec;
      n.codec_ready = true;
      if (e.reconfigure) {
        // Local reconfigure emits codec configuration before final ACK. Only
        // apply it after ACK; remote reconfigure is already accepted by SDK.
        n.automatic_start = false;
        if (s.stream == Stream::ReconfigureRequested) n.reconfigure_seen = true;
        else { n.stream = Stream::Suspended; t.add(Op::ConfigureAudio); }
      } else t.add(Op::ConfigureAudio);
      break;
    case Kind::DisconnectRequested:
      n.accepting = false;
      n.connect_intent = false;
      if (s.inquiry != Inquiry::Idle) {
        n.found = false;
        n.inquiry = Inquiry::Stopping;
        t.add(Op::StopInquiry);
      }
      if (s.cid) {
        stop();
        t.add(Op::DisconnectA2dp);
      }
      break;
    case Kind::StreamOpened:
      if (!s.connect_intent || !match || !s.codec_ready ||
          s.stream != Stream::Idle) {
        reject();
        break;
      }
      if (e.status) {
        fail(Error::Failed);
        break;
      }
      n.stream = Stream::Configuring;
      t.add(Op::ConfigureTransport);
      break;
    case Kind::TransportConfigured:
      if (!s.connect_intent || !match || s.stream != Stream::Configuring) {
        reject();
        break;
      }
      if (e.status || e.payload_limit < s.config.minimum_payload) {
        fail(Error::Failed);
        break;
      }
      n.payload_limit = e.payload_limit;
      n.stream = Stream::Open;
      n.automatic_start = s.config.auto_start;
      if (s.config.start_delay_us > UINT64_MAX - e.now_us) { fail(Error::ClockRegression); break; }
      n.start_due_us = e.now_us + s.config.start_delay_us;
      break;
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
    case Kind::SharedOwnershipObserved:
      if (!match || s.signaling != Signaling::Connected || s.shared_signaling ||
          !e.acl.epoch || e.acl.handle > 0x0eff || e.acl.peer != s.config.target || e.acl != s.shared_acl) {
        reject(); break;
      }
      n.shared_signaling = true;
      break;
    case Kind::SharedDetachRequested:
      if (!s.initialized || s.power != Power::Working || !s.shared_signaling ||
          s.signaling != Signaling::Connected || !s.cid || !s.connect_intent ||
          !s.codec_ready || s.detaching_role || s.control_due_us ||
          (s.stream != Stream::Open && s.stream != Stream::Playing && s.stream != Stream::Suspended)) {
        reject(); break;
      }
      n.status = 0; n.detaching_role = true;
      n.role_close_accepted = n.role_media_released = n.role_retire_requested = false;
      n.detach_previous_accepting = s.accepting;
      n.detach_stop_on_failure = e.detach_stop_on_failure;
      n.accepting = n.connect_intent = false;
      if (controlDeadline()) t.add(Op::CloseRoleMedia);
      break;
    case Kind::RoleCloseAccepted:
      if (!match || !s.detaching_role || s.role_close_accepted) { reject(); break; }
      stop(); n.control_due_us = s.control_due_us;
      n.role_close_accepted = true;
      break;
    case Kind::RoleCloseFailed:
      if (!match || !s.detaching_role || s.role_close_accepted) { reject(); break; }
      n.detaching_role = false; n.control_due_us = 0; n.status = e.status;
      if (s.detach_stop_on_failure) {
        n.accepting = n.connect_intent = false;
        fail(Error::Failed);
      } else {
        n.accepting = s.detach_previous_accepting; n.connect_intent = true;
      }
      break;
    case Kind::RoleRetireDeferred:
      if (!match || !s.detaching_role || !s.role_retire_requested) { reject(); break; }
      n.role_retire_requested = false; n.status = e.status;
      break;
#endif
    case Kind::PauseRequested:
      if (!s.cid || !s.connect_intent || !s.codec_ready ||
          (s.stream != Stream::Playing && s.stream != Stream::Open)) { reject(); break; }
      n.status = 0;
      n.control_previous = s.stream;
      stop();
      if (s.stream == Stream::Open) { n.stream = Stream::Suspended; break; }
      n.stream = Stream::SuspendRequested;
      if (controlDeadline()) t.add(Op::PauseStream);
      break;
    case Kind::ResumeRequested:
      if (!s.cid || !s.connect_intent || !s.codec_ready ||
          (s.stream != Stream::Suspended && s.stream != Stream::Open)) { reject(); break; }
      n.status = 0;
      n.control_previous = s.stream;
      n.automatic_start = false;
      n.stream = Stream::StartRequested;
      if (controlDeadline()) t.add(Op::StartStream);
      break;
    case Kind::ReconfigureRequested:
      if (!s.cid || !s.connect_intent || !s.codec_ready ||
          (s.stream != Stream::Suspended && s.stream != Stream::Open) ||
          !s.config.allow_sample_rate_change ||
          (e.codec.rate != 44100 && e.codec.rate != 48000) || e.codec.rate == s.codec.rate) {
        reject(); break;
      }
      n.status = 0;
      n.control_previous = s.stream; n.previous_codec = s.codec;
      n.requested_rate = e.codec.rate;
      n.reconfigure_seen = n.reconfigure_rejected = false;
      n.automatic_start = false; n.stream = Stream::ReconfigureRequested;
      if (controlDeadline()) t.add(Op::ReconfigureStream);
      break;
    case Kind::StreamReconfigured:
      if (!match || s.stream != Stream::ReconfigureRequested) { reject(); break; }
      n.control_due_us = 0; n.status = e.status;
      n.stream = Stream::Suspended;
      if (e.status) {
        n.codec = s.previous_codec; n.reconfigure_rejected = true;
      } else if (!s.reconfigure_seen || s.codec.rate != s.requested_rate) {
        fail(Error::InvalidInput);
      } else t.add(Op::ConfigureAudio);
      n.requested_rate = 0;
      break;
    case Kind::ControlFailed:
      if (!match || (s.stream != Stream::StartRequested &&
          s.stream != Stream::SuspendRequested && s.stream != Stream::ReconfigureRequested)) { reject(); break; }
      n.status = e.status; n.control_due_us = 0;
      n.stream = s.control_previous;
      n.requested_rate = 0;
      if (s.stream == Stream::ReconfigureRequested) n.codec = s.previous_codec;
      if (n.stream == Stream::Playing) {
        n.next_due_us = e.now_us; t.add(Op::EnableStreaming);
      }
      break;
    case Kind::CommandRejected:
      // The SDK follows a rejected reconfigure completion with COMMAND_REJECTED.
      if (match && e.command == 4 && s.reconfigure_rejected) {
        n.reconfigure_rejected = false; break;
      }
      if (!match) { reject(); break; }
      fail(Error::UnsupportedOperation);
      break;
    case Kind::StreamSuspended:
      if (!match || (s.stream != Stream::Playing && s.stream != Stream::SuspendRequested &&
          s.stream != Stream::StartRequested)) { reject(); break; }
      stop(); n.stream = Stream::Suspended; n.status = 0;
      break;
    case Kind::DelayReported:
      if (!match) { reject(); break; }
      n.delay_100us = e.payload_limit; ++n.delay_reports;
      break;
    case Kind::UnsupportedPairing:
      if (e.address != s.config.target ||
          s.signaling != Signaling::Connecting) {
        reject();
        break;
      }
      fail(Error::UnsupportedOperation);
      break;
    case Kind::StreamStarted:
      if (!match || !s.connect_intent || !s.codec_ready ||
          (s.stream != Stream::StartRequested && s.stream != Stream::Open && s.stream != Stream::Suspended)) {
        reject();
        break;
      }
      if (e.status) {
        fail(Error::Failed);
        break;
      }
      if (s.config.play_duration_us > UINT64_MAX - e.now_us) { fail(Error::ClockRegression); break; }
      n.stream = Stream::Playing; n.status = 0;
      n.control_due_us = 0; n.automatic_start = false;
      n.next_due_us = e.now_us; n.fraction = 0;
      n.end_due_us = e.now_us + s.config.play_duration_us;
      t.add(Op::EnableStreaming);
      break;
    case Kind::Tick:
      if (s.control_due_us && e.now_us >= s.control_due_us) {
        fail(Error::Failed);
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
        if (!s.shared_signaling)
#endif
        if (s.cid) t.add(Op::DisconnectA2dp);
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
      } else if (s.detaching_role) {
        if (s.role_media_released && !s.role_retire_requested) {
          n.role_retire_requested = true;
          t.add(Op::RetireRole);
        }
#endif
      } else if (s.stream == Stream::Open && s.automatic_start && e.now_us >= s.start_due_us) {
        n.status = 0;
        n.control_previous = s.stream;
        n.automatic_start = false;
        n.stream = Stream::StartRequested;
        if (controlDeadline()) t.add(Op::StartStream);
      } else if (s.stream == Stream::Playing) {
        if (e.now_us > s.next_due_us && e.now_us - s.next_due_us > n.max_lateness_us)
          n.max_lateness_us = e.now_us - s.next_due_us;
        if (s.config.play_duration_us && e.now_us >= s.end_due_us) {
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
          if (s.shared_signaling) {
            auto detach = e; detach.kind = Kind::SharedDetachRequested;
            detach.detach_stop_on_failure = true;
            return apply(s, detach);
          }
#endif
          stop();
          t.add(Op::DisconnectA2dp);
        } else if (e.now_us >= s.next_due_us && !s.media_pending) {
          if (s.credit == Credit::None) {
            n.credit = Credit::Requested;
            t.add(Op::RequestCanSend);
          } else if (s.credit == Credit::Granted) {
            n.credit = Credit::None;
            n.media_pending = true;
            t.add(Op::PumpAudioOnce);
          }
        }
      }
      break;
    case Kind::SendCreditGranted:
      if (!match || s.stream != Stream::Playing ||
          s.credit != Credit::Requested) {
        reject();
        break;
      }
      n.credit = Credit::Granted;
      t.add(Op::GrantTransportCredit);
      break;
    case Kind::MediaProgress:
    case Kind::MediaDeferred:
    case Kind::MediaResult: {
      if (!match || !s.media_pending) {
        reject();
        break;
      }
      n.media_pending = false;
      if (e.kind == Kind::MediaDeferred) {
        ++n.media_deferrals;
        break; // Retry on a later Tick. Do not advance pacing/accounting.
      }
      n.status = e.status;
      if (e.status) {
        fail(Error::Failed);
        break;
      }
      if (e.kind == Kind::MediaProgress) {
        ++n.packets; // Physical packet, not completed audio or pacing progress.
        break;
      }
      if (!e.accepted_samples || e.accepted_samples > 512 ||
          e.accepted_samples % 128) {
        fail(Error::Failed);
        break;
      }
      ++n.packets;
      ++n.completed_media;
      n.samples += e.accepted_samples;
      n.fraction += uint64_t(e.accepted_samples) * 1000000;
      const uint64_t advance = n.fraction / s.codec.rate;
      n.fraction %= s.codec.rate;
      if (advance > UINT64_MAX - n.next_due_us) {
        fail(Error::ClockRegression); // Never wrap the monotonic deadline.
        break;
      }
      n.next_due_us += advance;
      // Keep all schedule debt. Catch-up still needs a later Tick, one fresh
      // credit and at most one PumpAudioOnce per poll; no loop or PCM skip.
      // No backlog storage grows, and no unreported cap/rebase loses time.
      if (e.now_us > n.next_due_us && e.now_us - n.next_due_us > n.max_lateness_us)
        n.max_lateness_us = e.now_us - n.next_due_us;
      break;
    }
    case Kind::ActionFailed:
      fail(Error::Failed);
      break;
    default:
      reject();
      break;
    }
    return t;
  }
};
class ActionExecutor {
public:
  virtual ~ActionExecutor() = default;
  virtual void execute(Action, const State &next) = 0;
};
// Results are retained as normalized follow-up events (Initialized,
// TransportConfigured, MediaResult, ActionFailed). sequence counts retained
// entries.
struct HistoryEntry {
  uint64_t sequence = 0;
  Event event{};
  State before{}, after{};
  std::array<Action, 4> actions{};
  size_t count = 0;
};
// Single event-loop owner, not thread/IRQ safe. Executors must report every
// failed operation as ActionFailed, and exactly one MediaResult, MediaProgress or
// MediaDeferred per PumpAudioOnce. Hardware verification applies to the probe
// only, not this engine yet.
template <size_t QueueSize = 32, size_t HistorySize = 64> class Engine {
  static_assert(QueueSize > 0 && HistorySize > 0, "positive bounds required");
  State state_;
  ActionExecutor &executor_;
  std::array<Event, QueueSize> queue_{};
  std::array<HistoryEntry, HistorySize> history_{};
  size_t head_ = 0, size_ = 0, history_head_ = 0, history_size_ = 0;
  bool draining_ = false, overflow_ = false;
  uint64_t dropped_ = 0, overwritten_ = 0, sequence_ = 0;

public:
  explicit Engine(Config config, ActionExecutor &executor)
      : executor_(executor) {
    state_.config = config;
  }
  State snapshot() const { return state_; }
  size_t historySize() const { return history_size_; }
  HistoryEntry history(size_t i) const {
    return i < history_size_
               ? history_[(history_head_ + HistorySize - history_size_ + i) %
                          HistorySize]
               : HistoryEntry{};
  }
  uint64_t droppedEvents() const { return dropped_; }
  uint64_t overwrittenHistory() const { return overwritten_; }
  bool dispatch(Event event) {
    if (size_ == QueueSize) {
      ++dropped_;
      overflow_ = true;
      return false;
    }
    queue_[(head_ + size_) % QueueSize] = event;
    ++size_;
    if (draining_)
      return true;
    draining_ = true;
    while (size_) {
      auto e = queue_[head_];
      head_ = (head_ + 1) % QueueSize;
      --size_;
      auto before = state_;
      auto t = Rules::apply(state_, e);
      for (size_t i = 0; i < t.count; ++i) {
        executor_.execute(t.actions[i], t.next);
        if (overflow_)
          break;
      }
      state_ = t.next;
      if (overflow_) {
        state_.error = Error::QueueOverflow;
        state_.stream = Stream::Idle;
        state_.credit = Credit::None;
        state_.media_pending = false;
        size_ = 0;
        executor_.execute({Op::DisableStreaming}, state_);
        size_ = 0;
        overflow_ = false;
      }
      // Clock-only ticks do not evict diagnostic transitions.
      if (e.kind == Kind::Tick && t.count == 0 &&
          before.error == state_.error && before.rejected == state_.rejected)
        continue;
      history_[history_head_] = {sequence_++, e,         before,
                                 state_,      t.actions, t.count};
      history_head_ = (history_head_ + 1) % HistorySize;
      if (history_size_ < HistorySize)
        ++history_size_;
      else
        ++overwritten_;
    }
    draining_ = false;
    return true;
  }
};
} // namespace a2dp
