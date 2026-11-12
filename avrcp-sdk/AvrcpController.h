#pragma once
#include "AvrcpTarget.h"
#include "classic/avrcp_controller.h"

namespace picow::avrcp {
// One owner, single main loop, at most two allowed peers. Callbacks are
// observational: mutating/reentrant calls are rejected. SDK command acceptance
// and remote AVRCP response are distinct. No automatic playback mapping.
class AvrcpController {
public:
  using Config = AvrcpTarget::Config;
  enum class Phase : uint8_t { Idle, PressPending, Held, ReleasePending };
  enum class EventKind : uint8_t {
    Connected,
    Disconnected,
    PressResponse,
    ReleaseResponse,
    Reset,
    Timeout,
    CustomError
  };
  struct Event {
    EventKind kind;
    Binding binding{};
    uint8_t operation = 0,
            response =
                0; // AV/C ctype: 9 accepted, 10 rejected, 8 not implemented.
  };
  enum class Query : uint8_t {
    UnitInfo,
    SubunitInfo,
    SupportedCompanies,
    SupportedEvents,
    PlayStatus,
    NowPlayingInfo,
    SettingAttributes,
    SettingAttributeText,
    SettingValueText,
    ShuffleAndRepeat
  };
  struct SdkEvent {
    Binding binding{};
    const uint8_t *bytes = nullptr;
    uint16_t size = 0;
    // Bounded, borrowed SDK event; valid only during callback. Nested fields
    // are not interpreted here. Copy bytes before retaining or asynchronous
    // use.
  };
  static constexpr uint8_t SettingsResponseSubevent = 0xf0;
  struct SettingsChunk {
    Binding binding{};
    uint8_t response = 0, pdu = 0;
    uint16_t total = 0, offset = 0;
    const uint8_t *bytes = nullptr;
    uint16_t length = 0;
    bool final() const { return uint32_t(offset) + length == total; }
  };
  struct Status {
    bool started = false, stopped = false;
    uint32_t malformed = 0, ignored = 0, connection_errors = 0, responses = 0;
    int last_error = 0;
  };
  AvrcpController() = default;
  ~AvrcpController();
  AvrcpController(const AvrcpController &) = delete;
  AvrcpController &operator=(const AvrcpController &) = delete;
  bool begin(const Config &, bool advertise_browsing = false);
  void poll();
  // Absolute command budget from first timed poll observing a busy transaction.
  bool setCommandTimeout(uint32_t milliseconds); // zero disables, <= 0x7fffffff
  void poll(uint32_t now_ms);
  void stop(); // Terminal; does not power off the shared radio.
  void setCallback(void (*cb)(const Event &, void *), void *context = nullptr);
  int connect(const Address &); // Local SDK request status, not remote success.
  int cancelConnection(const Address &); // Pending signalling only; affects both roles.
  bool cancellationPending(const Address &) const;
  int cancellationStatus(const Address &) const;
  int disconnect(const Binding &); // Shared signalling disconnect affects both
                                   // AVRCP roles.
  int press(const Binding &,
            uint8_t operation); // SDK hold repeats until release.
  int release(const Binding &); // May be requested before PressResponse.
  int query(const Binding &, Query);
  int group(const Binding &,
            bool next); // Vendor-unique next/previous group click.
  int notification(const Binding &, avrcp_notification_event_id_t, bool enable);
  int elementAttributes(const Binding &, const avrcp_media_attribute_id_t *,
                        uint8_t count);
  int settingValues(const Binding &,
                    avrcp_player_application_setting_attribute_id_t);
  int absoluteVolume(const Binding &, uint8_t volume);
  int batteryStatus(const Binding &, avrcp_battery_status_t);
  int addressedPlayer(const Binding &, uint16_t player);
  int maximumFragments(const Binding &, uint8_t count);
  int playItem(const Binding &, const std::array<uint8_t, 8> &uid,
               uint16_t counter, avrcp_browsing_scope_t);
  int addItem(const Binding &, const std::array<uint8_t, 8> &uid,
              uint16_t counter, avrcp_browsing_scope_t);
  void setSdkCallback(void (*)(const SdkEvent &, void *),
                      void *context = nullptr);
  // Explicit settings APIs require the reviewed SDK overlay; otherwise they
  // return UNSUPPORTED without calling known-defective upstream serializers.
  int displayableCharsets(const Binding &, const uint16_t *, uint8_t count);
  int settingAttributeText(const Binding &, const uint8_t *, uint8_t count);
  int settingValueText(const Binding &, uint8_t attribute, const uint8_t *,
                       uint8_t count);
  int currentSettings(const Binding &, const uint8_t *, uint8_t count);
  int setSettings(const Binding &, const uint8_t *ids, const uint8_t *values,
                  uint8_t count);
  int shuffleMode(const Binding &, avrcp_shuffle_mode_t);
  int repeatMode(const Binding &, avrcp_repeat_mode_t);
  void setSettingsCallback(void (*)(const SettingsChunk &, void *),
                           void *context = nullptr);
  using CustomChunk = SettingsChunk;
  static constexpr uint8_t CustomResponseSubevent = 0xf1;
  // Owned request <=502 bytes, AVCTP-fragmented for negotiated MTU>=48.
  // Raw response AVCTP and vendor continuation are bounded and owned.
  int customCommand(const Binding &, uint8_t command_type, uint8_t subunit_type,
                    uint8_t subunit_id, uint8_t pdu, uint32_t company,
                    const uint8_t *bytes, uint16_t length);
  void setCustomCallback(void (*)(const CustomChunk &, void *), void * = nullptr);
  Binding bindingForPeer(const Address &) const;
  Phase phase(const Binding &) const;
  const Status &status() const { return status_; }

private:
  struct Slot {
    Binding binding{};
    Phase phase = Phase::Idle;
    uint8_t operation = 0;
    bool closing = false, timeout_closing = false, timer_armed = false;
    uint32_t timer_start = 0, timer_budget = 0;
    uint16_t settings_next = 0, settings_total = 0;
    uint8_t settings_pdu = 0, settings_response = 0;
  };
  static AvrcpController *active_;
  static void packet(uint8_t, uint16_t, uint8_t *, uint16_t);
  static void hci(uint8_t, uint16_t, uint8_t *, uint16_t);
  void receive(uint8_t, uint8_t *, uint16_t);
  void emit(const Event &);
  bool commandAllowed(const Binding &);
  bool allowed(const Address &) const;
  Slot *find(const Binding &);
  Slot *cid(uint16_t);
  void reset();
  void pollImpl(bool timed, uint32_t now_ms);
  uint32_t command_timeout_ms_ = 0;
  void (*custom_callback_)(const CustomChunk &, void *) = nullptr;
  void *custom_context_ = nullptr;
  Config config_{};
  Status status_{};
  std::array<Slot, 2> slots_{};
  uint32_t generation_ = 0;
  bool attempted_ = false, dispatching_ = false, sdp_registered_ = false;
  uint8_t sdp_[200]{};
  uint32_t sdp_handle_ = 0;
  void (*callback_)(const Event &, void *) = nullptr;
  void *context_ = nullptr;
  void (*sdk_callback_)(const SdkEvent &, void *) = nullptr;
  void *sdk_context_ = nullptr;
  void (*settings_callback_)(const SettingsChunk &, void *) = nullptr;
  void *settings_context_ = nullptr;
};
} // namespace picow::avrcp
