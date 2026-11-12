#pragma once
#include "../avrcp-core/AvrcpTargetCore.h"
#include "../bluetooth-runtime/BluetoothRuntime.h"
#include <memory>

namespace picow::avrcp {
extern "C" uint8_t picow_avrcp_target_extension(uint16_t, uint8_t, const uint8_t*, uint16_t, uint8_t*, uint16_t*);
extern "C" uint8_t picow_avrcp_target_pass_through(uint16_t, uint8_t, bool, const uint8_t*, uint16_t);
// Single-main-loop, process-lifetime owner. Never combine with legacy AVRCP plugin.
// Both incoming and explicit outgoing connections; shared SDK SDP serialization.
class AvrcpTarget {
public:
  struct Config { std::array<Address, 2> peers{}; uint8_t peer_count = 0; };
  struct Status {
    bool started = false, stopped = false;
    uint32_t malformed = 0, ignored = 0, connection_errors = 0, response_errors = 0;
    int last_error = 0;
  };
  AvrcpTarget() = default;
  ~AvrcpTarget();
  AvrcpTarget(const AvrcpTarget&) = delete;
  AvrcpTarget& operator=(const AvrcpTarget&) = delete;
  bool begin(const Config&, bool advertise_browsing = false);
  void poll();
  // Terminal for this owner; disconnects AVRCP only, never powers off A2DP/SPP.
  void stop();
  // Local request result, not proof of remote connection. Disconnect affects the
  // shared AVRCP signalling channel if both roles use this binding.
  int connect(const Address& peer);
  int cancelConnection(const Address &); // Pending signalling only; affects both roles.
  bool cancellationPending(const Address &) const;
  int cancellationStatus(const Address &) const;
  int disconnect(const Binding& binding);
  void setCallback(void (*callback)(const Event&, void*), void* context) {
    core_.setCallback(callback, context);
  }
  bool setSupported(uint8_t operation, bool enabled);
  enum class PassThroughDecision : uint8_t { NotImplemented = 8, Accepted = 9, Rejected = 10 };
  struct PassThroughRequest {
    Binding binding{};
    uint8_t operation = 0;
    bool pressed = false;
    // Standard buttons have no operands. Group navigation is SIG vendor-unique
    // 0x7e with exactly 00 19 58 00 00 (next) or 00 19 58 00 01 (previous).
    std::array<uint8_t, 5> operands{};
    uint8_t operand_count = 0;
  };
  // Overlay only. Bounded synchronous decision, no SDK ACK or API reentry.
  // No handler: valid supported standard buttons accepted; group not implemented.
  void setPassThroughHandler(PassThroughDecision (*handler)(const PassThroughRequest&, void*), void* context) {
    if (!requesting_) { pass_handler_ = handler; pass_context_ = context; }
  }
  const AvrcpTargetCore& core() const { return core_; }
  const Status& status() const { return status_; }
  // Play-status queries require a response even in a button-only product.
  // Unknown length/position = UINT32_MAX. No automatic application button mapping.
  bool setPlayback(uint32_t length_ms, uint32_t position_ms, uint8_t playback_status);
  // SDK retains metadata pointers. Strings are copied (max 255 bytes each).
  // Updates return COMMAND_DISALLOWED while a response/continuation is pending.
  struct Metadata {
    std::array<uint8_t, 8> track_id{};
    uint16_t track_number = 0, total_tracks = 0;
    uint32_t length_ms = UINT32_MAX;
    const char *title = "", *artist = "", *album = "", *genre = "";
  };
  int setMetadata(const Binding&, const Metadata&);
  int supportNotification(const Binding&, uint8_t event_id);
  int notifyPlayback(const Binding&, uint8_t playback_status);
  int notifyVolume(const Binding&, uint8_t volume); // 0..127, no audio gain applied
  int notifyTrack(const Binding&, const std::array<uint8_t, 8>&);
  int notifyContentChanged(const Binding&);
  int notifyAddressedPlayer(const Binding&, uint16_t player, uint16_t uid_counter);
  int notifyUids(const Binding&, uint16_t uid_counter);
  int notifyBattery(const Binding&, uint8_t battery_status); // SDK values 0..4
  // These capability buffers are copied and retained by this owner.
  int setCompanies(const Binding&, const uint32_t*, size_t count); // 1..8, 24-bit IDs
  int setUnitInfo(const Binding&, uint8_t subunit_type, uint32_t company_id);
  int setSubunitInfo(const Binding&, uint8_t subunit_type, const uint8_t*, size_t size); // 1..32
  struct Request {
    enum class Kind { AbsoluteVolume, PlayItem, AddToNowPlaying, SetAddressedPlayer, SetPlayerSettings } kind;
    Binding binding{};
    uint8_t volume = 0, scope = 0;
    uint16_t uid_counter = 0, player_id = 0;
    uint8_t setting_count = 0;
    std::array<uint8_t, 4> setting_ids{}, setting_values{};
    std::array<uint8_t, 8> item_id{};
  };
  struct Response { uint8_t status = 0; uint8_t volume = 0; }; // AVRCP status, actual volume
  // Synchronous bounded decision callback. Copy intent, do not reenter APIs.
  // Item default is INVALID_COMMAND; volume default preserves requested value.
  void setRequestHandler(Response (*handler)(const Request&, void*), void* context) {
    request_handler_ = handler; request_context_ = context;
  }
  // Standard player setting IDs 1..4; zero value disables before begin.
  // configure enables initial support, update changes an already enabled value.
  bool configurePlayerSetting(uint8_t attribute, uint8_t initial_value);
  bool updatePlayerSetting(uint8_t attribute, uint8_t value);
  int notifyPlayerSettings(const Binding&);
  int notifyPosition(const Binding&, uint32_t position_ms);
  uint32_t positionNotificationInterval(const Binding&) const; // seconds requested by peer
  int notifyTrackReachedStart(const Binding&);
  int notifyTrackReachedEnd(const Binding&);
  int notifySystemStatus(const Binding&, uint8_t status); // 0..2
  int notifyAvailablePlayers(const Binding&);
private:
  friend uint8_t picow_avrcp_target_pass_through(uint16_t, uint8_t, bool, const uint8_t*, uint16_t);
  static uint8_t handlePassThrough(uint16_t, uint8_t, bool, const uint8_t*, uint16_t);
  PassThroughDecision (*pass_handler_)(const PassThroughRequest&, void*) = nullptr;
  void* pass_context_ = nullptr;
  friend uint8_t picow_avrcp_target_extension(uint16_t, uint8_t, const uint8_t*, uint16_t, uint8_t*, uint16_t*);
  static uint8_t handleExtension(uint16_t, uint8_t, const uint8_t*, uint16_t, uint8_t*, uint16_t*);
  std::array<uint8_t,4> player_settings_{};
  int extensionNotify(const Binding&, uint8_t, const uint8_t*, uint16_t);
  struct OwnedInfo {
    uint32_t length = UINT32_MAX, position = UINT32_MAX;
    uint8_t playback = 0, system_status = 0;
    uint32_t position_interval = 0;
    uint32_t group_generation = 0;
    std::array<bool, 2> group_pressed{};
    std::array<std::array<char, 256>, 4> text{};
    std::array<uint32_t, 8> companies{};
    std::array<uint8_t, 32> subunit{};
  };
  // Storage is not cleared on disconnection: SDK may still retain pointers
  // until its asynchronous channel cleanup completes.
  std::unique_ptr<std::array<OwnedInfo, 2>> info_{new std::array<OwnedInfo, 2>{}};
  Response (*request_handler_)(const Request&, void*) = nullptr;
  void* request_context_ = nullptr;
  bool requesting_ = false;
  int validateBinding(const Binding&) const;
  int mutableInfo(const Binding&) const;
  OwnedInfo& infoFor(const Binding&);
  int recordResponse(int);
  static AvrcpTarget* active_;
  static void avrcpPacket(uint8_t, uint16_t, uint8_t*, uint16_t);
  static void hciPacket(uint8_t, uint16_t, uint8_t*, uint16_t);
  void receive(uint8_t, uint8_t*, uint16_t);
  bool allowed(const Address&) const;
  bool live(const Binding&) const;
  AvrcpTargetCore core_{};
  Config config_{};
  Status status_{};
  bool attempted_ = false;
  uint8_t sdp_[200]{};
  uint32_t sdp_handle_ = 0;
  bool sdp_registered_ = false;
};
}
