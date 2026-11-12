#pragma once
#include "IAudioSink.h"
#include <array>
#include <cstdint>
#include <memory>

// One process-lifetime SBC receiver. Use one main loop, never IRQ/second core.
// Distinct-peer Source+Sink dispatch is under validation. Same-peer dual media
// is rejected by shared registration until the SDK lifecycle is extended.
class A2dpSinkReceiver {
public:
  using Address = std::array<uint8_t, 6>;
  struct Settings {
    Address peer{}; // Exactly one permitted source.
    bool allow_pairing_confirmation = false;
    bool require_start_decision = false; // Optional build support; default auto-start.
  };
  enum class BeginResult { Ok, InvalidSettings, RuntimeAlreadyOwned, InitializationFailed, FeatureUnavailable };
  struct State {
    bool begun = false, working = false, accepting = false, connecting = false;
    bool closing = false, signaling = false, configured = false, streaming = false, failed = false;
    uint16_t cid = 0;
    uint8_t seid = 0, last_event = 0, last_status = 0;
    uint32_t generation = 0, sample_rate = 0;
    uint8_t channels = 2, sbc_channel_mode = 0;
    int last_operation_status = 0;
    uint8_t sbc_blocks = 16, sbc_subbands = 8, sbc_allocation = 0; // 0 loudness, 1 SNR
    uint8_t sbc_min_bitpool = 2, sbc_max_bitpool = 53;
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
    bool shared_signaling = false, role_detaching = false, role_retiring = false;
    bool role_cancelling = false, cancel_timed_out = false;
    int role_operation_status = 0;
#endif
  };
  struct Diagnostics {
    uint64_t received_packets = 0, received_bytes = 0, processed_packets = 0;
    uint64_t media_overflow = 0, media_oversize = 0, media_discarded = 0;
    uint64_t event_overflow = 0, event_oversize = 0;
    uint64_t wrong_state = 0, rejected_peers = 0, malformed_events = 0;
    uint64_t missing_packets = 0, reordered_packets = 0;
    uint64_t timestamp_discontinuities = 0, source_changes = 0;
    uint64_t decoded_frames = 0, decode_failures = 0;
    uint64_t pcm_frames = 0, delivered_frames = 0, dropped_frames = 0;
    uint64_t invalid_output_results = 0;
    std::array<uint64_t, 8> parse_errors{};
    size_t queued_media_packets = 0, media_high_water = 0;
  };
  A2dpSinkReceiver(IAudioSink &output, Settings settings);
  ~A2dpSinkReceiver();
  A2dpSinkReceiver(const A2dpSinkReceiver &) = delete;
  A2dpSinkReceiver &operator=(const A2dpSinkReceiver &) = delete;
  BeginResult begin(); // Initialize and request power. Does not connect/listen.
  void poll();
  // Permit this peer's incoming A2DP and enable shared-radio connectability.
  // Does not enable discoverability; does not change another profile's policy.
  bool listen();
  // One outgoing connection attempt to peer. true = SDK accepted, not connected.
  bool connect();
  // Close this A2DP signaling session and disable incoming admission. No radio
  // power-off, automatic reconnect or change to other profiles' ACLs/policies.
  void disconnect();
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
  // Experimental stable-media CLOSE, then deferred role retirement. Never closes
  // the shared signaling/ACL. false means no detach was accepted.
  bool detachSharedRole();
  // Pre-configuration discovery only. Accepted cancellation drains the real
  // outstanding transaction; a timeout never forces shared initiator reuse.
  bool cancelSharedDiscovery();
#endif
  enum class DelayReportStatus { Accepted, InvalidDelay, StaleSession, NotReady, NotNegotiated, SdkRejected };
  struct DelayReportResult {
    DelayReportStatus status;
    uint8_t sdk_status = 0; // Meaningful only for Accepted/SdkRejected.
  };
  // Explicit application-measured delay: 0..65535 units of 100 us (0..6553.5 ms).
  // Pass the snapshot generation associated with the measurement, never a CID.
  // Accepted means SDK queued the request, NOT sent/acknowledged by the peer.
  // Main loop only, outside poll/output callbacks. No automatic report or retry.
  DelayReportResult reportDelay100us(uint32_t delay_100us, uint32_t expected_generation);
  struct StartRequest {
    uint32_t generation = 0;
    uint64_t request_id = 0; // Zero means no currently actionable request.
  };
  enum class StartDecisionStatus { Accepted, FeatureUnavailable, NotReady, StaleRequest, SdkRejected };
  struct StartDecisionResult {
    StartDecisionStatus status;
    uint8_t sdk_status = 0; // Only meaningful for Accepted/SdkRejected.
  };
  static bool startDecisionsAvailable();
  // Build with picow_enable_sink_start_decisions(target), then set
  // Settings::require_start_decision=true before begin(). Both are opt-in.
  // Poll first. Requests are invalidated by decisions and lifecycle changes.
  // No automatic timeout, retry or default answer. Decide promptly or disconnect;
  // a remote peer may time out before a human supplies a decision.
  StartRequest pendingStart() const;
  // Main loop only, outside callbacks. Accepted is SDK admission, not wire ACK.
  // Observe snapshot().streaming for successful START completion.
  // Accepted and SdkRejected both consume the request; after SdkRejected,
  // disconnect to abandon the SDK transaction. Do not retry the same token.
  StartDecisionResult decideStart(StartRequest request, bool accept);
  State snapshot() const;
  Diagnostics diagnostics() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
