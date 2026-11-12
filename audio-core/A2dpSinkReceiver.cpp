#include "A2dpSinkReceiver.h"
#include "sink/SinkProfile.h"

struct A2dpSinkReceiver::Impl {
  Settings settings;
  A2dpSinkProfile profile;
  bool attempted = false, begun = false, polling = false;
  int last_status = 0;
  Impl(IAudioSink &output, Settings s) : settings(s), profile(s.peer) {
    profile.decoder.output = &output;
    profile.allow_confirmation = s.allow_pairing_confirmation;
    profile.require_start_decision = s.require_start_decision;
  }
};
A2dpSinkReceiver::A2dpSinkReceiver(IAudioSink &output, Settings settings)
    : impl_(new Impl(output, settings)) {}
A2dpSinkReceiver::~A2dpSinkReceiver() {
  // SDK retains endpoint/SDP/callback storage. No supported destruction/restart.
  if (impl_->attempted) impl_.release();
}
A2dpSinkReceiver::BeginResult A2dpSinkReceiver::begin() {
  if (impl_->settings.require_start_decision && !startDecisionsAvailable()) return BeginResult::FeatureUnavailable;
  if (impl_->settings.peer == Address{}) return BeginResult::InvalidSettings;
  if (impl_->attempted) return BeginResult::RuntimeAlreadyOwned;
  impl_->attempted = true;
  impl_->last_status = impl_->profile.begin();
  if (impl_->last_status == -1000) return BeginResult::RuntimeAlreadyOwned;
  if (impl_->last_status) { impl_->profile.failed = true; return BeginResult::InitializationFailed; }
  impl_->begun = true;
  return BeginResult::Ok;
}
void A2dpSinkReceiver::poll() {
  if (!impl_->begun || impl_->polling) return;
  impl_->polling = true;
  auto &runtime = BluetoothRuntime::instance();
  picow::a2dp_shared::pruneQueuedDiscovery();
  runtime.poll();
  if (runtime.failed() && !impl_->profile.failed) {
    impl_->profile.failed = true;
    impl_->profile.allow_connections = false;
    impl_->profile.disconnect();
    impl_->profile.signaling = impl_->profile.configured = false;
    impl_->profile.cid = 0;
    impl_->last_status = -1;
    impl_->profile.events.clear();
    impl_->profile.media.clear();
  }
  if (!runtime.failed()) impl_->profile.poll();
  impl_->polling = false;
}
bool A2dpSinkReceiver::listen() {
  auto &p = impl_->profile;
  if (!impl_->begun || impl_->polling || !p.working || p.failed || p.closing || p.events.count) return false;
  p.allow_connections = true;
  gap_connectable_control(1);
  return true;
}
bool A2dpSinkReceiver::connect() {
  if (!impl_->begun || impl_->polling) return false;
  impl_->last_status = impl_->profile.connect();
  return impl_->last_status == 0;
}
void A2dpSinkReceiver::disconnect() {
  if (!impl_->begun || impl_->polling) return;
  impl_->polling = true;
  impl_->profile.allow_connections = false;
  impl_->profile.disconnect();
  impl_->polling = false;
}
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
bool A2dpSinkReceiver::cancelSharedDiscovery() {
  if (!impl_->begun || impl_->polling) return false;
  impl_->polling = true;
  const bool accepted = impl_->profile.cancelSharedDiscovery();
  impl_->last_status = impl_->profile.role_last_status;
  impl_->polling = false;
  return accepted;
}
bool A2dpSinkReceiver::detachSharedRole() {
  if (!impl_->begun || impl_->polling) return false;
  impl_->polling = true;
  const bool accepted = impl_->profile.detachSharedRole();
  impl_->last_status = impl_->profile.role_last_status;
  impl_->polling = false;
  return accepted;
}
#endif
A2dpSinkReceiver::DelayReportResult A2dpSinkReceiver::reportDelay100us(
    uint32_t delay_100us, uint32_t expected_generation) {
  if (!impl_->begun || impl_->polling) return {DelayReportStatus::NotReady};
  impl_->polling = true; // Also excludes mutation from synchronous SDK callbacks.
  const int status = impl_->profile.reportDelay100us(delay_100us, expected_generation);
  impl_->polling = false;
  switch (status) {
    case -1: return {DelayReportStatus::NotReady};
    case -2: return {DelayReportStatus::StaleSession};
    case -3: return {DelayReportStatus::InvalidDelay};
    case -4: return {DelayReportStatus::NotNegotiated};
    default: return {status == 0 ? DelayReportStatus::Accepted : DelayReportStatus::SdkRejected,
                     static_cast<uint8_t>(status)};
  }
}
A2dpSinkReceiver::State A2dpSinkReceiver::snapshot() const {
  const auto &p = impl_->profile;
  State state{impl_->begun, p.working, p.allow_connections, p.connecting,
          p.closing, p.signaling, p.configured, p.streaming, p.failed,
          p.cid, p.seid, p.last_event, p.last_status, p.generation(),
          p.config.rate, p.config.channels(), p.config.mode, impl_->last_status};
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
  state.shared_signaling = p.shared_signaling;
  state.role_detaching = p.role_detaching; state.role_retiring = p.role_retiring;
  state.role_cancelling = p.role_cancelling; state.cancel_timed_out = p.cancel_timed_out;
  state.role_operation_status = p.role_last_status;
#endif
  state.sbc_blocks = p.config.blocks; state.sbc_subbands = p.config.subbands;
  state.sbc_allocation = p.config.allocation;
  state.sbc_min_bitpool = p.config.min_pool; state.sbc_max_bitpool = p.config.max_pool;
  return state;
}
A2dpSinkReceiver::Diagnostics A2dpSinkReceiver::diagnostics() const {
  const auto &p = impl_->profile;
  Diagnostics d;
  d.received_packets = p.received; d.received_bytes = p.bytes; d.processed_packets = p.processed;
  d.media_overflow = p.media.overflow; d.media_oversize = p.media.oversize; d.media_discarded = p.media.discarded;
  d.event_overflow = p.events.overflow; d.event_oversize = p.events.oversize;
  d.wrong_state = p.wrong_state; d.rejected_peers = p.rejected_peers; d.malformed_events = p.malformed_events;
  d.missing_packets = p.sequence.missing; d.reordered_packets = p.sequence.reordered;
  d.timestamp_discontinuities = p.sequence.timestamp_discontinuities; d.source_changes = p.sequence.source_changes;
  d.decoded_frames = p.decoder.good; d.decode_failures = p.decoder.failed;
  d.pcm_frames = p.decoder.pcm_frames; d.delivered_frames = p.decoder.delivered_frames;
  d.dropped_frames = p.decoder.dropped_frames; d.invalid_output_results = p.decoder.invalid_output_results;
  d.parse_errors = p.parse_errors;
  d.queued_media_packets = p.media.count; d.media_high_water = p.media.high_water;
  return d;
}

bool A2dpSinkReceiver::startDecisionsAvailable() {
#ifdef PICOW_A2DP_SINK_START_DECISIONS
  return true;
#else
  return false;
#endif
}
A2dpSinkReceiver::StartRequest A2dpSinkReceiver::pendingStart() const {
  if (!impl_->begun || impl_->polling) return {};
  return impl_->profile.pendingStart();
}
A2dpSinkReceiver::StartDecisionResult A2dpSinkReceiver::decideStart(StartRequest request, bool accept) {
  if (!startDecisionsAvailable()) return {StartDecisionStatus::FeatureUnavailable};
  if (!impl_->begun || impl_->polling) return {StartDecisionStatus::NotReady};
  impl_->polling = true;
  const auto result = impl_->profile.decideStart(request, accept);
  impl_->polling = false;
  return result;
}
#ifdef PICOW_A2DP_SINK_START_DECISIONS
extern "C" bool picow_sink_start_confirmation_required(avdtp_connection_t *connection,
                                                       avdtp_stream_endpoint_t *endpoint) {
  return A2dpSinkProfile::startConfirmationRequired(connection, endpoint);
}
#endif
