#include "A2dpSourcePlayer.h"
#include "AudioPipeline.h"
#include "BTStackA2DPTransport.h"
#include "btstack.h"
#include "encoders/SBCEncoder_BTStack.h"
#include "BluetoothRuntime.h"
#include "A2dpShared.h"
#include "A2dpSharedSdk.h"
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
#include "A2dpExperimentalSdk.h"
#endif
#include "pico/stdlib.h"
#include <algorithm>
#include <cstring>

namespace {
#include "encoders/SBCEncoder_BTStack.h"
#include <vector>

// Group one to four complete frames, or one frame for bounded fragmentation.
// Selection happens only while stopped, before pipeline buffers are replaced.
class A2dpSbcFrameBatch : public IAudioEncoder {
public:
  static constexpr size_t maxFramesPerPacket = 4;
  bool configurePayload(size_t payload) {
    const size_t frame = encoder_.maxEncodedSize();
    if (!frame || payload <= 1 || (frame + payload - 2) / (payload - 1) > 15) return false;
    framesPerPacket = std::max(size_t(1), std::min(maxFramesPerPacket, (payload - 1) / frame));
    return true;
  }
  explicit A2dpSbcFrameBatch(SBCEncoder_BTStack &encoder) : encoder_(encoder) {}
  size_t requiredSamples() const override {
    return framesPerPacket * encoder_.requiredSamples();
  }
  size_t maxEncodedSize() const override {
    return framesPerPacket * encoder_.maxEncodedSize();
  }
  size_t samplesPerChannel() const {
    return framesPerPacket * encoder_.samplesPerChannel();
  }
  std::vector<uint8_t> encode(const int16_t *pcm, size_t samples) override {
    std::vector<uint8_t> output(maxEncodedSize());
    output.resize(encode_into(pcm, samples, output.data(), output.size()));
    return output;
  }
  size_t encode_into(const int16_t *pcm, size_t samples, uint8_t *out,
                     size_t cap) override {
    if (!pcm || !out || samples != requiredSamples() || cap < maxEncodedSize())
      return 0;
    const size_t pcm_size = encoder_.requiredSamples();
    const size_t frame_size = encoder_.maxEncodedSize();
    for (size_t i = 0; i < framesPerPacket; ++i) {
      if (encoder_.encode_into(pcm + i * pcm_size, pcm_size,
                               out + i * frame_size, frame_size) != frame_size)
        return 0;
    }
    return maxEncodedSize();
  }

private:
  SBCEncoder_BTStack &encoder_;
  size_t framesPerPacket = maxFramesPerPacket;
};

} // namespace
struct A2dpSourcePlayer::Impl final : a2dp::ActionExecutor {
  IAudioSource &source;
  Settings settings;
  a2dp::Engine<> engine;
  SBCEncoder_BTStack encoder;
  A2dpSbcFrameBatch batch{encoder};
  uint8_t storage[1024]{};
  BTStackA2DPTransport transport{0, 0, sizeof(storage), 0, storage, 0};
  btstack_packet_callback_registration_t registration{};
  uint8_t sdp[150]{}, capabilities[4]{}, configuration[4]{};
  bool valid = false;
  bool runtime_failure_observed = false;
  uint64_t last_playing_poll_us = 0, credit_request_us = 0;
  uint64_t max_poll_gap_us = 0, max_pump_us = 0, max_credit_wait_us = 0;
  static Impl *owner;
  static bool
      claimed; // Resource lifetime, not connection state. No runtime restart.
  static a2dp::Config config(Settings s) {
    a2dp::Config c;
    // Adaptive executor checks the actual encoder size before opening.
    c.minimum_payload = 2;
    if (s.target_address)
      sscanf_bd_addr(s.target_address, c.target.data());
    c.start_delay_us = s.start_delay_us;
    c.play_duration_us = s.play_duration_us;
    c.auto_connect = s.auto_connect;
    c.auto_start = s.auto_start;
    c.sample_rate = s.sample_rate;
    c.allow_sample_rate_change = s.allow_sample_rate_change;
    c.control_timeout_us = s.control_timeout_us;
    return c;
  }
  Impl(IAudioSource &src, Settings s)
      : source(src), settings(s), engine(config(s), *this) {
    transport.enableSbcFragmentation(true);
    bd_addr_t address{};
    valid = s.target_address && std::strlen(s.target_address) == 17 &&
            sscanf_bd_addr(s.target_address, address) &&
            std::any_of(address, address + 6, [](uint8_t byte) { return byte != 0; }) &&
            (s.sample_rate == 48000 || s.sample_rate == 44100) &&
            s.control_timeout_us &&
            (!s.allow_sample_rate_change || s.configure_source);
  }
  static bool admitsConnection() {
    if (!owner || BluetoothRuntime::instance().failed()) return false;
    const auto s = owner->engine.snapshot();
    if (!s.initialized || s.error != a2dp::Error::None || s.power != a2dp::Power::Working)
      return false;
    // Match the reducer's admission states. A connect intent also exists while
    // inquiry is running, before this owner is ready for AVDTP signaling.
    const bool outgoing = s.connect_intent && s.signaling == a2dp::Signaling::Connecting;
    const bool incoming = s.accepting && s.inquiry == a2dp::Inquiry::Idle &&
                          s.signaling == a2dp::Signaling::Idle && !s.cid;
    if (outgoing) return !s.existing_acl_bound || owner->liveAcl(s.existing_acl);
    if (!incoming) return false;
    const auto link = BluetoothRuntime::instance().linkForPeer(s.config.target);
    return link.active && owner->liveAcl({link.peer, link.epoch, link.handle});
  }
  static bool admitsDiscovery() {
    if (!owner || BluetoothRuntime::instance().failed()) return false;
    const auto s = owner->engine.snapshot();
    return s.initialized && s.error == a2dp::Error::None &&
           s.power == a2dp::Power::Working && s.connect_intent && s.cid &&
           s.signaling == a2dp::Signaling::Connected &&
           (!s.existing_acl_bound || owner->liveAcl(s.existing_acl)) &&
           (!s.incoming || owner->liveAcl(s.incoming_acl))
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
           && (!s.shared_signaling || owner->liveAcl(s.shared_acl))
#endif
           ;
  }
  void post(a2dp::Event e) {
    e.now_us = time_us_64();
    engine.dispatch(e);
  }
  void post(a2dp::Kind k) {
    a2dp::Event e;
    e.kind = k;
    post(e);
  }
  void fail(int status) {
    a2dp::Event e;
    e.kind = a2dp::Kind::ActionFailed;
    e.status = status;
    post(e);
  }
  bool liveAcl(const a2dp::AclBinding &expected) const {
    const auto &rt = BluetoothRuntime::instance();
    if (!rt.ownsRadio() || rt.failed() || hci_get_state() != HCI_STATE_WORKING ||
        !expected.epoch || expected.peer != engine.snapshot().config.target ||
        expected.peer == a2dp::Address{} || expected.handle > 0x0eff) return false;
    const auto peer = rt.linkForPeer(expected.peer);
    const auto handle = rt.linkForHandle(expected.handle);
    return peer.active && handle.active && peer.peer == expected.peer &&
           handle.peer == expected.peer && peer.epoch == expected.epoch &&
           handle.epoch == expected.epoch && peer.handle == expected.handle &&
           handle.handle == expected.handle;
  }
  void syncSharedOwnership() {
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
    const auto s = engine.snapshot();
    if (!s.cid || s.shared_signaling || s.signaling != a2dp::Signaling::Connected ||
        s.error != a2dp::Error::None ||
        !picow::a2dp_shared::otherOwnsPeer(picow::a2dp_shared::Role::Source, s.config.target)) return;
    const auto *connection = avdtp_get_connection_for_avdtp_cid(s.cid);
    if (!connection || connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) return;
    if (connection->con_handle != s.shared_acl.handle ||
        std::memcmp(connection->remote_addr, s.config.target.data(), 6) || !liveAcl(s.shared_acl)) {
      fail(-6); return;
    }
    a2dp::Event e; e.kind = a2dp::Kind::SharedOwnershipObserved;
    e.cid = s.cid; e.seid = s.seid; e.acl = s.shared_acl; post(e);
#endif
  }
  void validateExistingAcl() {
    const auto s = engine.snapshot();
    if (s.existing_acl_bound && s.error == a2dp::Error::None && !liveAcl(s.existing_acl))
      post(a2dp::Kind::ExistingAclInvalidated);
    if (s.incoming && s.error == a2dp::Error::None && !liveAcl(s.incoming_acl))
      fail(-6); // The admitted ACL incarnation disappeared; never adopt a replacement.
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
    if (s.shared_signaling && s.error == a2dp::Error::None && !liveAcl(s.shared_acl))
      fail(-6);
#endif
  }
  void execute(a2dp::Action action, const a2dp::State &s) override {
    using namespace a2dp;
    int status = 0;
    switch (action.op) {
    case Op::Initialize: {
      status = BluetoothRuntime::instance().initialize();
      if (status)
        break;
      a2dp_source_init();
      a2dp_source_register_packet_handler(callback);
      picow::a2dp_shared::install(picow::a2dp_shared::Role::Source);
      a2dp_source_register_media_config_validator(validateCodec);
      capabilities[0] =
          ((settings.allow_sample_rate_change ? (AVDTP_SBC_48000 | AVDTP_SBC_44100) :
             (settings.sample_rate == 48000 ? AVDTP_SBC_48000 : AVDTP_SBC_44100)) << 4) |
          AVDTP_SBC_STEREO;
      capabilities[1] = (AVDTP_SBC_BLOCK_LENGTH_16 << 4) |
                        (AVDTP_SBC_SUBBANDS_8 << 2) |
                        AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS;
      capabilities[2] = 2;
      capabilities[3] = 53;
      auto *endpoint = a2dp_source_create_stream_endpoint(
          AVDTP_AUDIO, AVDTP_CODEC_SBC, capabilities, sizeof(capabilities),
          configuration, sizeof(configuration));
      if (!endpoint) {
        status = -1;
        break;
      }
      avdtp_set_preferred_sampling_frequency(endpoint, settings.sample_rate);
      a2dp_source_create_sdp_record(sdp, sdp_create_service_record_handle(),
                                    AVDTP_SOURCE_FEATURE_MASK_PLAYER, nullptr,
                                    nullptr);
      status = sdp_register_service(sdp);
      if (status)
        break;
      registration.callback = callback;
      BluetoothRuntime::instance().setHandler(BluetoothRuntime::Profile::A2dp, callback);
      BluetoothRuntime::instance().setPairingPolicy(BluetoothRuntime::Profile::A2dp, s.config.target, true);
      BluetoothRuntime::instance().setSecurityLevel(BluetoothRuntime::Profile::A2dp, 2);
      Event e;
      e.kind = Kind::Initialized;
      e.seid = avdtp_local_seid(endpoint);
      post(e);
      break;
    }
    case Op::PowerOn:
      status = BluetoothRuntime::instance().powerOn();
      // A profile can join a runtime already brought up by SPP. There will
      // be no second HCI working event in that case.
      if (!status && hci_get_state() == HCI_STATE_WORKING)
        post(Kind::PowerWorking);
      break;
    case Op::StartInquiry:
      status = gap_inquiry_start(5);
      break;
    case Op::StopInquiry:
      status = gap_inquiry_stop();
      break;
    case Op::EstablishA2dpOnExistingAcl:
      if (!s.existing_acl_bound || s.existing_acl_stopped ||
          !s.connect_intent || !liveAcl(s.existing_acl)) {
        post(Kind::ExistingAclInvalidated);
        break;
      }
      [[fallthrough]];
    case Op::EstablishA2dp: {
      bd_addr_t address;
      std::memcpy(address, s.config.target.data(), 6);
      uint16_t cid = 0;
      // Do not launch negotiation on an already allocated AVDTP connection.
      // In particular, the SDK's pending incoming path can return success
      // without assigning a CID. A live ACL alone is still allowed.
      if (auto *connection = avdtp_get_connection_for_bd_addr(address)) {
        status = ERROR_CODE_COMMAND_DISALLOWED;
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
        const auto link = BluetoothRuntime::instance().linkForPeer(s.config.target);
        if (picow::a2dp_shared::otherOwnsPeer(picow::a2dp_shared::Role::Source, s.config.target) &&
            connection->state == AVDTP_SIGNALING_CONNECTION_OPENED && link.active &&
            connection->con_handle == link.handle &&
            liveAcl({link.peer, link.epoch, link.handle}))
          status = picow_a2dp_join_existing_role(AVDTP_ROLE_SOURCE, connection->avdtp_cid);
#else
        (void) connection;
#endif
        break;
      }
      status = a2dp_source_establish_stream(address, &cid);
      break;
    }
    case Op::ConfirmPairing: {
      bd_addr_t address;
      std::memcpy(address, s.config.target.data(), 6);
      BluetoothRuntime::instance().confirmPairing(s.config.target);
      break;
    }
    case Op::ConfigureAudio:
      if ((!settings.allow_sample_rate_change && s.codec.rate != settings.sample_rate) ||
          (settings.configure_source && !settings.configure_source(s.codec.rate, settings.source_context)) ||
          !encoder.configure(s.codec.rate, s.codec.blocks, s.codec.bands,
                             SBC_ALLOCATION_METHOD_LOUDNESS, s.codec.max_pool,
                             SBC_CHANNEL_MODE_STEREO)) {
        status = -2;
        break;
      }
      if (!batch.configurePayload(s.payload_limit ? s.payload_limit : sizeof(storage))) {
        status = -3; break;
      }
      AudioPipeline::getInstance().configure(&source, &batch, &transport);
      break;
    case Op::ConfigureTransport: {
      const int mtu = std::min<int>(a2dp_max_media_payload_size(s.cid, s.seid),
                                    sizeof(storage));
      Event e;
      e.kind = Kind::TransportConfigured;
      e.cid = s.cid;
      e.seid = s.seid;
      e.payload_limit = mtu > 0 ? mtu : 0;
      e.status = mtu <= 0 || !batch.configurePayload(static_cast<size_t>(mtu)) ? -3 : 0;
      if (!e.status) {
        transport.configure(s.cid, s.seid, mtu);
        // MTU arrives after codec configuration. Resize/reset before opening;
        // no encoded retry or partial PCM may survive a batch-size change.
        AudioPipeline::getInstance().configure(&source, &batch, &transport);
      }
      post(e);
      break;
    }
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
    case Op::CloseRoleMedia: {
      Event e; e.cid = s.cid; e.seid = s.seid;
      e.status = liveAcl(s.shared_acl) ? picow_a2dp_close_role_media(AVDTP_ROLE_SOURCE, s.cid, s.seid)
                                       : ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
      e.kind = e.status ? Kind::RoleCloseFailed : Kind::RoleCloseAccepted;
      post(e);
      break;
    }
    case Op::RetireRole: {
      Event e; e.cid = s.cid; e.seid = s.seid;
      e.status = liveAcl(s.shared_acl) ? picow_a2dp_retire_role(AVDTP_ROLE_SOURCE, s.cid, s.seid)
                                       : ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
      e.kind = !e.status ? Kind::RoleDetached : e.status == ERROR_CODE_COMMAND_DISALLOWED
                    ? Kind::RoleRetireDeferred : Kind::ActionFailed;
      post(e);
      break;
    }
#endif
    case Op::SetConnectable:
      gap_connectable_control(1);
      break;
    case Op::PauseStream:
      status = a2dp_source_pause_stream(s.cid, s.seid);
      break;
    case Op::ReconfigureStream:
      status = a2dp_source_reconfigure_stream_sampling_frequency(s.cid, s.requested_rate);
      break;
    case Op::StartStream:
      status = a2dp_source_start_stream(s.cid, s.seid);
      break;
    case Op::EnableStreaming:
      transport.setStreaming(true);
      break;
    case Op::DisableStreaming:
      last_playing_poll_us = credit_request_us = 0;
      transport.setStreaming(false);
      AudioPipeline::getInstance().reset();
      break;
    case Op::RequestCanSend:
      credit_request_us = time_us_64();
      transport.requestCanSendNow();
      break;
    case Op::GrantTransportCredit:
      if (credit_request_us) {
        const auto now = time_us_64();
        if (now >= credit_request_us)
          max_credit_wait_us = std::max(max_credit_wait_us, now - credit_request_us);
        credit_request_us = 0;
      }
      transport.onCanSendNow();
      break;
    case Op::PumpAudioOnce: {
      const auto started = time_us_64();
      const auto result = AudioPipeline::getInstance().loop();
      const auto finished = time_us_64();
      if (finished >= started) max_pump_us = std::max(max_pump_us, finished - started);
      // A source underrun also consumes this scheduling hint, but never counts
      // as a sent packet. Request a fresh hint on a later tick, not recursively.
      transport.clearCanSend();
      Event e;
      e.kind = result == AudioPipeline::Result::Deferred || result == AudioPipeline::Result::Idle
                   ? Kind::MediaDeferred : result == AudioPipeline::Result::Progress
                   ? Kind::MediaProgress : Kind::MediaResult;
      e.cid = s.cid;
      e.seid = s.seid;
      e.status = result == AudioPipeline::Result::Failed
                   ? (transport.lastSendStatus() ? transport.lastSendStatus() : -4) : 0;
      if (result == AudioPipeline::Result::Sent)
        e.accepted_samples = static_cast<uint16_t>(batch.samplesPerChannel());
      post(e);
      break;
    }
    case Op::RejectConnection:
      if (picow::a2dp_shared::otherOwnsCid(picow::a2dp_shared::Role::Source, action.cid)) break;
      status = a2dp_source_disconnect(action.cid);
      break;
    case Op::DisconnectA2dp:
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
      if (s.shared_signaling || picow::a2dp_shared::otherOwnsPeer(picow::a2dp_shared::Role::Source, s.config.target)) break;
#endif
      if (picow::a2dp_shared::otherOwnsCid(picow::a2dp_shared::Role::Source, s.cid)) break;
      status = a2dp_source_disconnect(s.cid);
      break;
    case Op::ClearConnection:
      transport.configure(0, 0, 0);
      break;
    }
    if (status) {
      if (action.op == Op::PauseStream || action.op == Op::StartStream || action.op == Op::ReconfigureStream) {
        Event e; e.kind = Kind::ControlFailed; e.cid = s.cid; e.seid = s.seid; e.status = status; post(e);
      } else fail(status);
    }
  }
  static a2dp::Codec codecFromPacket(const uint8_t *p) {
    a2dp::Codec c;
    c.rate = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(p);
    c.blocks = a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(p);
    c.bands = a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(p);
    c.stereo = a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(p) == AVDTP_CHANNEL_MODE_STEREO;
    c.loudness = a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(p) == AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS;
    c.min_pool = a2dp_subevent_signaling_media_codec_sbc_configuration_get_min_bitpool_value(p);
    c.max_pool = a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(p);
    return c;
  }
  static uint8_t validateCodec(const avdtp_stream_endpoint_t *, const uint8_t *p, uint16_t n) {
    if (!owner || !p || n != 18 || p[1] + 2u != n ||
        (p[0] != HCI_EVENT_A2DP_META && p[0] != HCI_EVENT_AVDTP_META) ||
        p[2] != A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION)
      return AVDTP_ERROR_CODE_UNSUPPORTED_CONFIGURATION;
    const auto s = owner->engine.snapshot();
    const auto c = codecFromPacket(p);
    const bool reconfig = p[7] != 0;
    const auto &rt = BluetoothRuntime::instance();
    const auto link = rt.linkForPeer(s.config.target);
    if (rt.failed() || s.error != a2dp::Error::None || !s.connect_intent || !s.cid ||
        little_endian_read_16(p, 3) != s.cid || p[5] != s.seid || p[8] != AVDTP_AUDIO || p[12] != 2 ||
        !link.active || (s.existing_acl_bound &&
            (link.epoch != s.existing_acl.epoch || link.handle != s.existing_acl.handle)) ||
        (s.incoming && (link.epoch != s.incoming_acl.epoch || link.handle != s.incoming_acl.handle)) ||
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
        (s.shared_signaling && (link.epoch != s.shared_acl.epoch || link.handle != s.shared_acl.handle)) ||
#endif
        (reconfig && s.stream != a2dp::Stream::Open && s.stream != a2dp::Stream::Suspended) ||
        (!reconfig && s.codec_ready && s.stream != a2dp::Stream::Idle) ||
        (c.rate != 44100 && c.rate != 48000) ||
        (!s.config.allow_sample_rate_change && c.rate != s.config.sample_rate) ||
        c.blocks != 16 || c.bands != 8 || !c.stereo || !c.loudness ||
        c.min_pool < 2 || c.max_pool > 53 || c.min_pool > c.max_pool)
      return AVDTP_ERROR_CODE_UNSUPPORTED_CONFIGURATION;
    return 0;
  }
  bool request(a2dp::Kind kind, uint32_t rate = 0, bool enabled = false) {
    if (owner != this || BluetoothRuntime::instance().failed()) return false;
    syncSharedOwnership();
    validateExistingAcl();
    const auto before = engine.snapshot();
    if (!before.initialized || before.error != a2dp::Error::None || before.power != a2dp::Power::Working) return false;
    a2dp::Event e; e.kind = kind; e.codec.rate = rate; e.enabled = enabled; post(e);
    const auto after = engine.snapshot();
    return after.error == a2dp::Error::None && after.rejected == before.rejected && after.status == 0;
  }
  static void callback(uint8_t type, uint16_t, uint8_t *packet, uint16_t size) {
    if (owner && !BluetoothRuntime::instance().failed() &&
        type == HCI_EVENT_PACKET && packet && size >= 2 &&
        size >= static_cast<unsigned>(packet[1]) + 2)
    {
      owner->syncSharedOwnership();
      owner->normalize(packet, static_cast<uint16_t>(packet[1]) + 2);
    }
  }
  void normalize(uint8_t *p, uint16_t size) {
    using namespace a2dp;
    Event e;
    e.raw_type = hci_event_packet_get_type(p);
    switch (hci_event_packet_get_type(p)) {
    case BTSTACK_EVENT_STATE:
      if (size >= 3)
        e.status = btstack_event_state_get_state(p);
      if (size >= 3 && btstack_event_state_get_state(p) == HCI_STATE_WORKING)
        e.kind = Kind::PowerWorking;
      else if (size >= 3 && btstack_event_state_get_state(p) == HCI_STATE_OFF)
        e.kind = Kind::ExistingAclRuntimeStopped;
      break;
    case HCI_EVENT_DISCONNECTION_COMPLETE: {
      const auto s = engine.snapshot();
      if (size >= 6 && !hci_event_disconnection_complete_get_status(p) &&
          s.existing_acl_bound &&
          hci_event_disconnection_complete_get_connection_handle(p) == s.existing_acl.handle)
        e.kind = Kind::ExistingAclInvalidated;
      break;
    }
    case GAP_EVENT_INQUIRY_RESULT:
      if (size < 16)
        break;
      e.kind = Kind::DeviceSeen;
      gap_event_inquiry_result_get_bd_addr(p, e.address.data());
      break;
    case GAP_EVENT_INQUIRY_COMPLETE:
      if (size < 3)
        break;
      e.kind = Kind::InquiryFinished;
      e.status = gap_event_inquiry_complete_get_status(p);
      break;
    case HCI_EVENT_PIN_CODE_REQUEST:
      if (size < 8)
        break;
      e.kind = Kind::UnsupportedPairing;
      hci_event_pin_code_request_get_bd_addr(p, e.address.data());
      break;
    case HCI_EVENT_USER_CONFIRMATION_REQUEST:
      if (size < 12)
        break;
      e.kind = Kind::ConfirmationRequested;
      hci_event_user_confirmation_request_get_bd_addr(p, e.address.data());
      break;
    case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
      if (size < 9)
        break;
      e.kind = Kind::PairingFinished;
      e.status = hci_event_simple_pairing_complete_get_status(p);
      hci_event_simple_pairing_complete_get_bd_addr(p, e.address.data());
      break;
    case HCI_EVENT_A2DP_META:
      if (size < 3)
        break;
      e.raw_subevent = hci_event_a2dp_meta_get_subevent_code(p);
      switch (hci_event_a2dp_meta_get_subevent_code(p)) {
      case A2DP_SUBEVENT_COMMAND_REJECTED:
        if (size < 8)
          break;
        e.cid = a2dp_subevent_command_rejected_get_a2dp_cid(p);
        e.seid = a2dp_subevent_command_rejected_get_local_seid(p);
        e.command = a2dp_subevent_command_rejected_get_signal_identifier(p);
        // SDK maps both AVDTP reject kinds here; no remote error byte is
        // exposed.
        if (a2dp_subevent_command_rejected_get_is_initiator(p)) {
          e.kind = Kind::CommandRejected;
          e.status = -5;
        }
        break;
      case A2DP_SUBEVENT_STREAM_SUSPENDED:
        if (size < 6)
          break;
        e.kind = Kind::StreamSuspended;
        e.cid = a2dp_subevent_stream_suspended_get_a2dp_cid(p);
        e.seid = a2dp_subevent_stream_suspended_get_local_seid(p);
        break;
      case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED:
        if (size < 14)
          break;
        e.kind = Kind::SignalingEstablished;
        e.cid = a2dp_subevent_signaling_connection_established_get_a2dp_cid(p);
        e.status = a2dp_subevent_signaling_connection_established_get_status(p);
        a2dp_subevent_signaling_connection_established_get_bd_addr(
            p, e.address.data());
        if (picow::a2dp_shared::otherOwnsPeer(picow::a2dp_shared::Role::Source, e.address)) {
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
          if (e.address != engine.snapshot().config.target) return;
#else
          return;
#endif
        }
        {
          const auto link = BluetoothRuntime::instance().linkForPeer(e.address);
          if (link.active && link.handle == a2dp_subevent_signaling_connection_established_get_con_handle(p))
            e.acl = {link.peer, link.epoch, link.handle};
        }
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
        e.shared_signaling = picow::a2dp_shared::otherOwnsPeer(picow::a2dp_shared::Role::Source, e.address);
#endif
        if (engine.snapshot().existing_acl_bound) {
          const auto expected = engine.snapshot().existing_acl;
          if (liveAcl(expected) &&
              a2dp_subevent_signaling_connection_established_get_con_handle(p) == expected.handle)
            e.acl = expected;
        }
        break;
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
        if (size < 18)
          break;
        e.kind = Kind::CodecConfigured;
        e.cid =
            a2dp_subevent_signaling_media_codec_sbc_configuration_get_a2dp_cid(
                p);
        e.seid =
            a2dp_subevent_signaling_media_codec_sbc_configuration_get_local_seid(
                p);
        e.codec = codecFromPacket(p);
        e.reconfigure = a2dp_subevent_signaling_media_codec_sbc_configuration_get_reconfigure(p) != 0;
        break;
      case A2DP_SUBEVENT_STREAM_ESTABLISHED:
        if (size < 14)
          break;
        a2dp_subevent_stream_established_get_bd_addr(p, e.address.data());
        if (picow::a2dp_shared::otherOwnsPeer(picow::a2dp_shared::Role::Source, e.address)) {
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
          if (e.address != engine.snapshot().config.target) return;
#else
          return;
#endif
        }
        e.kind = Kind::StreamOpened;
        e.cid = a2dp_subevent_stream_established_get_a2dp_cid(p);
        e.seid = a2dp_subevent_stream_established_get_local_seid(p);
        e.status = a2dp_subevent_stream_established_get_status(p);
        break;
      case A2DP_SUBEVENT_STREAM_STARTED:
        if (size < 6)
          break;
        e.kind = Kind::StreamStarted;
        e.cid = a2dp_subevent_stream_started_get_a2dp_cid(p);
        e.seid = a2dp_subevent_stream_started_get_local_seid(p);
        break;
      case A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW:
        if (size < 6)
          break;
        e.kind = Kind::SendCreditGranted;
        e.cid =
            a2dp_subevent_streaming_can_send_media_packet_now_get_a2dp_cid(p);
        e.seid =
            a2dp_subevent_streaming_can_send_media_packet_now_get_local_seid(p);
        break;
      case A2DP_SUBEVENT_STREAM_RECONFIGURED:
        if (size < 7) break;
        e.kind = Kind::StreamReconfigured;
        e.cid = a2dp_subevent_stream_reconfigured_get_a2dp_cid(p);
        e.seid = a2dp_subevent_stream_reconfigured_get_local_seid(p);
        e.status = a2dp_subevent_stream_reconfigured_get_status(p);
        break;
      case A2DP_SUBEVENT_SIGNALING_DELAY_REPORT:
        if (size < 8) break;
        e.kind = Kind::DelayReported;
        e.cid = a2dp_subevent_signaling_delay_report_get_a2dp_cid(p);
        e.seid = a2dp_subevent_signaling_delay_report_get_local_seid(p);
        e.payload_limit = a2dp_subevent_signaling_delay_report_get_delay_100us(p);
        break;
      case A2DP_SUBEVENT_STREAM_STOPPED:
      case A2DP_SUBEVENT_STREAM_RELEASED:
        if (size < 6)
          break;
        e.kind = p[2] == A2DP_SUBEVENT_STREAM_STOPPED ? Kind::StreamStopped : Kind::StreamReleased;
        e.cid = a2dp_subevent_stream_released_get_a2dp_cid(p);
        e.seid = a2dp_subevent_stream_released_get_local_seid(p);
        break;
      case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
        if (size < 5)
          break;
        e.kind = Kind::SignalingReleased;
        e.cid = a2dp_subevent_signaling_connection_released_get_a2dp_cid(p);
        break;
      default:
        break;
      }
      break;
    default:
      break;
    }
    post(e); // No Bluetooth calls or side effects in event normalization.
  }
};
A2dpSourcePlayer::Impl *A2dpSourcePlayer::Impl::owner = nullptr;
bool A2dpSourcePlayer::Impl::claimed = false;
A2dpSourcePlayer::A2dpSourcePlayer(IAudioSource &source, Settings settings)
    : impl_(new Impl(source, settings)) {}
A2dpSourcePlayer::~A2dpSourcePlayer() {
  // BTstack retains SDP/endpoint storage: deliberately retain the owner until
  // reset.
  if (Impl::owner == impl_.get())
    impl_.release();
}
A2dpSourcePlayer::BeginResult A2dpSourcePlayer::begin() {
  if (!impl_->valid)
    return BeginResult::InvalidSettings;
  const auto peer = impl_->engine.snapshot().config.target;
  if (Impl::claimed || !picow::a2dp_shared::canRegister(picow::a2dp_shared::Role::Source, peer) ||
      !BluetoothRuntime::instance().claim(
                           BluetoothRuntime::Profile::A2dp))
    return BeginResult::RuntimeAlreadyOwned;
  picow::a2dp_shared::registerRole(picow::a2dp_shared::Role::Source, peer);
  Impl::claimed = true;
  Impl::owner = impl_.get();
  picow::a2dp_shared::setAdmission(picow::a2dp_shared::Role::Source, Impl::admitsConnection);
  picow::a2dp_shared::setDiscoveryAdmission(picow::a2dp_shared::Role::Source, Impl::admitsDiscovery);
  impl_->post(a2dp::Kind::Initialize);
  if (!snapshot().initialized)
    return BeginResult::InitializationFailed;
  impl_->post(a2dp::Kind::PowerRequested);
  return snapshot().error == a2dp::Error::None
             ? BeginResult::Ok
             : BeginResult::InitializationFailed;
}
void A2dpSourcePlayer::poll() {
  if (Impl::owner != impl_.get() || !snapshot().initialized)
    return;
  const auto now = time_us_64();
  if (snapshot().stream == a2dp::Stream::Playing) {
    if (impl_->last_playing_poll_us && now >= impl_->last_playing_poll_us)
      impl_->max_poll_gap_us = std::max(impl_->max_poll_gap_us, now - impl_->last_playing_poll_us);
    impl_->last_playing_poll_us = now;
  } else impl_->last_playing_poll_us = 0;
  impl_->syncSharedOwnership();
  impl_->validateExistingAcl();
  auto &runtime = BluetoothRuntime::instance();
  // Continue pumping the shared runtime even after failure so asynchronous
  // radio shutdown can drain. Profile callbacks are suppressed below.
  // Report failure once; no further tick advances discovery or audio.
  picow::a2dp_shared::pruneQueuedDiscovery();
  runtime.poll();
  if (runtime.failed()) {
    if (!impl_->runtime_failure_observed) {
      impl_->runtime_failure_observed = true;
      impl_->fail(-1);
    }
    return;
  }
  impl_->validateExistingAcl();
  impl_->post(a2dp::Kind::Tick);
}
void A2dpSourcePlayer::connect() {
  if (BluetoothRuntime::instance().failed())
    return;
  impl_->post(a2dp::Kind::DiscoveryRequested);
}
bool A2dpSourcePlayer::connectOverExistingAcl(const a2dp::AclBinding &expected) {
  if (Impl::owner != impl_.get() || !impl_->liveAcl(expected)) return false;
  const auto before = snapshot();
  if (!before.initialized || before.existing_acl_stopped ||
      before.error != a2dp::Error::None || before.power != a2dp::Power::Working ||
      before.inquiry != a2dp::Inquiry::Idle || before.signaling != a2dp::Signaling::Idle ||
      before.stream != a2dp::Stream::Idle || before.connect_intent || before.cid) return false;
  a2dp::Event event;
  event.kind = a2dp::Kind::ExistingAclConnectRequested;
  event.acl = expected;
  impl_->post(event);
  const auto after = snapshot();
  return after.error == a2dp::Error::None && after.rejected == before.rejected &&
         after.connect_intent && after.existing_acl_bound && after.existing_acl == expected &&
         (after.signaling == a2dp::Signaling::Connecting || after.signaling == a2dp::Signaling::Connected);
}
bool A2dpSourcePlayer::listen(bool enabled) {
  return impl_->request(a2dp::Kind::ListenRequested, 0, enabled);
}
bool A2dpSourcePlayer::pause() { return impl_->request(a2dp::Kind::PauseRequested); }
bool A2dpSourcePlayer::resume() { return impl_->request(a2dp::Kind::ResumeRequested); }
bool A2dpSourcePlayer::reconfigureSamplingFrequency(uint32_t sample_rate) {
  return impl_->request(a2dp::Kind::ReconfigureRequested, sample_rate);
}
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
bool A2dpSourcePlayer::detachSharedRole() {
  const bool accepted = impl_->request(a2dp::Kind::SharedDetachRequested);
  picow::a2dp_shared::pruneQueuedDiscovery();
  return accepted;
}
#endif
void A2dpSourcePlayer::disconnect() {
  if (BluetoothRuntime::instance().failed())
    return;
  impl_->syncSharedOwnership();
  impl_->validateExistingAcl();
  impl_->post(a2dp::Kind::DisconnectRequested);
  picow::a2dp_shared::pruneQueuedDiscovery();
}
A2dpSourcePlayer::Diagnostics A2dpSourcePlayer::diagnostics() const {
  const auto &t = impl_->transport;
  return {t.timestamp(), t.sequenceNumber(), t.sentPackets(), t.lastSendStatus(),
          snapshot().media_deferrals,
          Impl::owner == impl_.get() ? AudioPipeline::getInstance().pendingBytes() : 0,
          impl_->max_poll_gap_us, impl_->max_pump_us, impl_->max_credit_wait_us,
          snapshot().completed_media, t.abandonedPartialFrames()};
}
a2dp::State A2dpSourcePlayer::snapshot() const {
  return impl_->engine.snapshot();
}
size_t A2dpSourcePlayer::historySize() const {
  return impl_->engine.historySize();
}
a2dp::HistoryEntry A2dpSourcePlayer::history(size_t i) const {
  return impl_->engine.history(i);
}
uint64_t A2dpSourcePlayer::overwrittenHistory() const {
  return impl_->engine.overwrittenHistory();
}
uint64_t A2dpSourcePlayer::droppedEvents() const {
  return impl_->engine.droppedEvents();
}
