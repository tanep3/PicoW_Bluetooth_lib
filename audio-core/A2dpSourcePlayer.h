#pragma once
#include "IAudioSource.h"
#include "connection/A2dpConnection.h"
#include <memory>

// A2DP Source profile on the shared BluetoothRuntime. Construct once, keep
// alive for the program lifetime. Not compatible with legacy connection plugins
// or another Source owner. Source + Sink use separate registered peers; the
// pinned SDK cannot configure two media streams on one peer connection.
// Every method, callback and poll belongs to the same main loop (never an IRQ).
class A2dpSourcePlayer {
public:
  struct Settings {
    const char *target_address = "";
    uint32_t sample_rate =
        48000; // Signed int16, interleaved stereo; no resampler.
    uint64_t start_delay_us = 0;
    uint64_t play_duration_us = 0; // Per START/resumed segment; 0 = continuous.
    bool auto_connect = true;
    bool auto_start = true; // false: wait for resume() or a remote START.
    uint64_t control_timeout_us = 10000000; // Bound START/SUSPEND/RECONFIGURE.
    // Opt in to both 44.1 and 48 kHz. No resampling is performed. The callback
    // must switch the source's PCM production to the accepted rate, returning
    // false if it cannot. Called on initial configuration and accepted changes,
    // from the same poll/event-loop context; must not reenter this player.
    bool allow_sample_rate_change = false;
    bool (*configure_source)(uint32_t sample_rate, void *context) = nullptr;
    void *source_context = nullptr;
  };
  enum class BeginResult {
    Ok,
    InvalidSettings,
    RuntimeAlreadyOwned,
    InitializationFailed
  };
  A2dpSourcePlayer(IAudioSource &source, Settings settings);
  ~A2dpSourcePlayer();
  A2dpSourcePlayer(const A2dpSourcePlayer &) = delete;
  A2dpSourcePlayer &operator=(const A2dpSourcePlayer &) = delete;
  BeginResult begin();
  void poll();
  void connect(); // Discover the exact configured address, no automatic retry.
  // Explicit opt-in for the configured peer's current shared-runtime ACL.
  // expected must contain its exact peer, nonzero epoch and HCI handle.
  // true means accepted and immediate SDK establish succeeded, NOT connected
  // or authenticated. SDP/AVDTP/security negotiation still runs normally.
  // Use Settings.auto_connect=false; this path never cancels ongoing inquiry.
  // Requires begun, working and idle. Rechecks identity before establish and
  // on asynchronous completion/poll. Loss/replacement or radio OFF is terminal
  // for this opt-in path until reset. disconnect() cancels a pending request;
  // normal discovery connect() retains its existing behavior.
  // SDK callbacks lack epochs; this validates actual current runtime identity,
  // not arbitrary historical packets replayed after CID/handle reuse.
  bool connectOverExistingAcl(const a2dp::AclBinding &expected);
  // Enable/disable incoming admission for the configured peer. Enables shared
  // radio connectability, never discovery; disabling does not close an existing
  // connection or disable other profiles' connectability. Explicit disconnect
  // also disables admission. An ACL identity is checked on incoming acceptance.
  bool listen(bool enabled = true);
  // Nonblocking controls: true = request accepted, not remote completion.
  // pause() on Open cancels automatic start; on Playing requests SUSPEND.
  // Pending PCM/encoded data is discarded on pause; RTP sequence is retained.
  // resume() starts an Open/Suspended stream. Observe snapshot/history for ACK.
  bool pause();
  bool resume();
  // Only while Open/Suspended and allow_sample_rate_change is enabled.
  // Reconfiguration never automatically resumes. Old buffered PCM is discarded.
  bool reconfigureSamplingFrequency(uint32_t sample_rate);
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
  // Host/explicit-overlay experiment only. Close this role's stable media,
  // then retire it on a later poll without disconnecting shared signaling.
  // Pending negotiation/control is rejected; true is acceptance, not completion.
  bool detachSharedRole();
#endif
  void disconnect();
  struct Diagnostics {
    uint32_t rtp_timestamp = 0;
    uint16_t rtp_sequence = 0;
    uint32_t sent_packets = 0;
    uint8_t last_send_status = 0;
    uint64_t media_deferrals = 0; // Includes source starvation; never sent packets.
    size_t pending_encoded_bytes = 0;
    // Lifetime maxima during Playing; us, no per-packet logging required.
    uint64_t max_poll_gap_us = 0, max_pump_us = 0, max_credit_wait_us = 0;
    uint64_t completed_media = 0;
    uint32_t abandoned_partial_frames = 0; // At least one physical fragment sent; counted once.
  };
  Diagnostics diagnostics() const;
  a2dp::State snapshot() const;
  size_t historySize() const;
  a2dp::HistoryEntry history(size_t oldest_index) const;
  uint64_t overwrittenHistory() const;
  uint64_t droppedEvents() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
