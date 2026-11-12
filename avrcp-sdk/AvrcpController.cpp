#include "AvrcpController.h"
#include "AvrcpSdkExtensions.h"
#include "AvrcpShared.h"
#include <algorithm>
#include <limits>
#if !defined(HAVE_MALLOC) &&                                                   \
    (!defined(MAX_NR_AVRCP_CONNECTIONS) || MAX_NR_AVRCP_CONNECTIONS < 4)
#error "Two-peer AVRCP requires MAX_NR_AVRCP_CONNECTIONS >= 4"
#endif
namespace picow::avrcp {
AvrcpController *AvrcpController::active_ = nullptr;
AvrcpController::~AvrcpController() {
  stop();
  if (active_ == this) {
    AvrcpShared::instance().detach(AvrcpShared::Controller);
    avrcp_controller_register_packet_handler(
        [](uint8_t, uint16_t, uint8_t *, uint16_t) {});
    active_ = nullptr;
  }
}
bool AvrcpController::allowed(const Address &p) const {
  for (unsigned i = 0; i < config_.peer_count; ++i)
    if (config_.peers[i] == p)
      return true;
  return false;
}
AvrcpController::Slot *AvrcpController::cid(uint16_t id) {
  for (auto &s : slots_)
    if (id && s.binding.cid == id)
      return &s;
  return nullptr;
}
AvrcpController::Slot *AvrcpController::find(const Binding &b) {
  auto *s = cid(b.cid);
  const auto link = BluetoothRuntime::instance().linkForPeer(b.peer);
  return s && !s->closing && s->binding == b && link.active &&
                 link.epoch == b.epoch
             ? s
             : nullptr;
}
Binding AvrcpController::bindingForPeer(const Address &p) const {
  for (const auto &s : slots_)
    if (s.binding.cid && s.binding.peer == p)
      return s.binding;
  return {};
}
AvrcpController::Phase AvrcpController::phase(const Binding &b) const {
  for (const auto &s : slots_)
    if (s.binding.cid && s.binding == b)
      return s.phase;
  return Phase::Idle;
}
void AvrcpController::setCallback(void (*cb)(const Event &, void *),
                                  void *ctx) {
  if (dispatching_)
    return;
  callback_ = cb;
  context_ = ctx;
}
void AvrcpController::emit(const Event &e) {
  if (!callback_)
    return;
  dispatching_ = true;
  callback_(e, context_);
  dispatching_ = false;
}
bool AvrcpController::begin(const Config &c, bool advertise_browsing) {
#if !defined(PICOW_AVRCP_BROWSING)
  if (advertise_browsing) return false;
#endif
  if (dispatching_ || attempted_ || active_ || !c.peer_count ||
      c.peer_count > 2)
    return false;
  for (unsigned i = 0; i < c.peer_count; ++i) {
    if (c.peers[i] == Address{})
      return false;
    for (unsigned j = 0; j < i; ++j)
      if (c.peers[i] == c.peers[j])
        return false;
  }
  attempted_ = true;
  active_ = this;
  config_ = c;
  status_.last_error = AvrcpShared::instance().attach(
      AvrcpShared::Controller,
      {packet, hci,
       [](const Address &p) {
         return active_ && active_->status_.started &&
                !active_->status_.stopped && active_->allowed(p);
       },
       [](uint16_t id) { return active_ && active_->cid(id); }});
  if (status_.last_error) {
    stop();
    return false;
  }
  avrcp_controller_register_packet_handler(packet);
  sdp_handle_ = sdp_create_service_record_handle();
  avrcp_controller_create_sdp_record(
      sdp_, sdp_handle_, AVRCP_FEATURE_MASK_CATEGORY_PLAYER_OR_RECORDER |
      (advertise_browsing ? AVRCP_FEATURE_MASK_BROWSING : 0),
      nullptr, nullptr);
  status_.last_error = sdp_register_service(sdp_);
  if (status_.last_error) {
    stop();
    return false;
  }
  sdp_registered_ = true;
  status_.last_error = BluetoothRuntime::instance().powerOn();
  status_.started = !status_.last_error;
  if (!status_.started)
    stop();
  return status_.started;
}
int AvrcpController::connect(const Address &p) {
  if (dispatching_ || !status_.started || !allowed(p) || bindingForPeer(p).cid)
    return ERROR_CODE_COMMAND_DISALLOWED;
  return status_.last_error = AvrcpShared::instance().connect(p);
}
int AvrcpController::cancelConnection(const Address &peer) {
  if (dispatching_ || !status_.started || !allowed(peer)) return ERROR_CODE_COMMAND_DISALLOWED;
  return status_.last_error = AvrcpShared::instance().cancelConnection(peer);
}
bool AvrcpController::cancellationPending(const Address &peer) const {
  return allowed(peer) && AvrcpShared::instance().cancellationPending(peer);
}
int AvrcpController::cancellationStatus(const Address &peer) const {
  return allowed(peer) ? AvrcpShared::instance().cancellationStatus(peer) : ERROR_CODE_COMMAND_DISALLOWED;
}
int AvrcpController::disconnect(const Binding &b) {
  if (dispatching_ || !status_.started || !find(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  auto *slot = find(b);
  slot->closing = true;
  const auto result = avrcp_disconnect(b.cid);
  if (result && slot->binding == b)
    slot->closing = false;
  return status_.last_error = result;
}
int AvrcpController::press(const Binding &b, uint8_t op) {
  if (dispatching_ || !status_.started)
    return ERROR_CODE_COMMAND_DISALLOWED;
  auto *s = find(b);
  if (!s || s->phase != Phase::Idle)
    return ERROR_CODE_COMMAND_DISALLOWED;
  // All standard no-operand operation IDs in the pinned SDK enum. Reserved
  // values and vendor-unique (requires operands) are not disguised as buttons.
  if (!(op <= 0x0d || (op >= 0x20 && op <= 0x2c) ||
        (op >= 0x30 && op <= 0x38) || op == 0x3c ||
        (op >= 0x40 && op <= 0x4c) || op == 0x50 || op == 0x51 ||
        (op >= 0x71 && op <= 0x75)))
    return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
  s->timer_armed = false;
  s->phase = Phase::PressPending;
  s->operation = op;
  const auto result = avrcp_controller_start_press_and_hold_cmd(
      b.cid, static_cast<avrcp_operation_id_t>(op));
  if (result && s->binding == b) {
    s->phase = Phase::Idle;
    s->operation = 0;
  }
  return status_.last_error = result;
}
int AvrcpController::release(const Binding &b) {
  if (dispatching_ || !status_.started)
    return ERROR_CODE_COMMAND_DISALLOWED;
  auto *s = find(b);
  if (!s || (s->phase != Phase::Held && s->phase != Phase::PressPending))
    return ERROR_CODE_COMMAND_DISALLOWED;
  const auto previous = s->phase;
  s->timer_armed = false;
  s->phase = Phase::ReleasePending;
  const auto result = avrcp_controller_release_press_and_hold_cmd(b.cid);
  if (result && s->binding == b)
    s->phase = previous;
  return status_.last_error = result;
}
bool AvrcpController::commandAllowed(const Binding &b) {
  auto *slot = find(b);
  if (!dispatching_ && slot) {
    auto *sdk = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER, b.cid);
    if (sdk && sdk->state == AVCTP_CONNECTION_OPENED) slot->timer_armed = false;
  }
  return !dispatching_ && status_.started && slot && slot->phase == Phase::Idle;
}
void AvrcpController::setSdkCallback(void (*cb)(const SdkEvent &, void *),
                                     void *context) {
  if (dispatching_)
    return;
  sdk_callback_ = cb;
  sdk_context_ = context;
}
int AvrcpController::group(const Binding &b, bool next) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  auto *slot = find(b);
  slot->phase = Phase::ReleasePending;
  slot->operation = 0x7e;
  const auto result = next ? avrcp_controller_next_group(b.cid)
                           : avrcp_controller_previous_group(b.cid);
  if (result && slot->binding == b) {
    slot->phase = Phase::Idle;
    slot->operation = 0;
  }
  return status_.last_error = result;
}
int AvrcpController::query(const Binding &b, Query q) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  uint8_t (*fn)(uint16_t) = nullptr;
  switch (q) {
  case Query::UnitInfo:
    fn = avrcp_controller_unit_info;
    break;
  case Query::SubunitInfo:
    fn = avrcp_controller_subunit_info;
    break;
  case Query::SupportedCompanies:
    fn = avrcp_controller_get_supported_company_ids;
    break;
  case Query::SupportedEvents:
    fn = avrcp_controller_get_supported_events;
    break;
  case Query::PlayStatus:
    fn = avrcp_controller_get_play_status;
    break;
  case Query::NowPlayingInfo:
    fn = avrcp_controller_get_now_playing_info;
    break;
  case Query::SettingAttributes:
    fn = avrcp_controller_query_player_application_setting_attributes;
    break;
  case Query::SettingAttributeText:
    fn = avrcp_controller_query_player_application_setting_attribute_text;
    break;
  case Query::SettingValueText:
    fn = avrcp_controller_query_player_application_setting_value_text;
    break;
  case Query::ShuffleAndRepeat:
    fn = avrcp_controller_query_shuffle_and_repeat_modes;
    break;
  default:
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  }
  return status_.last_error = fn(b.cid);
}
int AvrcpController::notification(const Binding &b,
                                  avrcp_notification_event_id_t id,
                                  bool enable) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  if (unsigned(id) < 1 || unsigned(id) > 13)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return status_.last_error =
             enable ? avrcp_controller_enable_notification(b.cid, id)
                    : avrcp_controller_disable_notification(b.cid, id);
}
int AvrcpController::elementAttributes(const Binding &b,
                                       const avrcp_media_attribute_id_t *ids,
                                       uint8_t count) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  if (count >= AVRCP_MEDIA_ATTR_NUM || (count && !ids))
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  std::array<avrcp_media_attribute_id_t, AVRCP_MEDIA_ATTR_NUM> copy{};
  for (unsigned i = 0; i < count; ++i) {
    if (ids[i] <= AVRCP_MEDIA_ATTR_ALL || ids[i] >= AVRCP_MEDIA_ATTR_NUM)
      return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    copy[i] = ids[i];
  }
  // Pinned SDK copies IDs into its bitmask synchronously, retaining no pointer.
  return status_.last_error =
             avrcp_controller_get_element_attributes(b.cid, count, copy.data());
}
int AvrcpController::settingValues(
    const Binding &b, avrcp_player_application_setting_attribute_id_t id) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  if (unsigned(id) < 1 || unsigned(id) > 4)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return status_.last_error =
             avrcp_controller_query_player_application_setting_values(b.cid,
                                                                      id);
}
int AvrcpController::absoluteVolume(const Binding &b, uint8_t volume) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  if (volume > 127)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return status_.last_error =
             avrcp_controller_set_absolute_volume(b.cid, volume);
}
int AvrcpController::batteryStatus(const Binding &b,
                                   avrcp_battery_status_t battery) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  if (unsigned(battery) > 4)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return status_.last_error =
             avrcp_controller_inform_battery_status(b.cid, battery);
}
int AvrcpController::addressedPlayer(const Binding &b, uint16_t player) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  return status_.last_error =
             avrcp_controller_set_addressed_player(b.cid, player);
}
int AvrcpController::maximumFragments(const Binding &b, uint8_t count) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  if (!count)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return status_.last_error =
             avrcp_controller_set_max_num_fragments(b.cid, count);
}
int AvrcpController::playItem(const Binding &b,
                              const std::array<uint8_t, 8> &uid,
                              uint16_t counter, avrcp_browsing_scope_t scope) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  if (unsigned(scope) < 1 || unsigned(scope) > 3)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  auto copy = uid; // Pinned SDK copies all eight bytes synchronously.
  return status_.last_error = avrcp_controller_play_item_for_scope(
             b.cid, copy.data(), counter, scope);
}
int AvrcpController::addItem(const Binding &b,
                             const std::array<uint8_t, 8> &uid,
                             uint16_t counter, avrcp_browsing_scope_t scope) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  if (unsigned(scope) < 1 || unsigned(scope) > 3)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  auto copy = uid;
  return status_.last_error =
             avrcp_controller_add_item_from_scope_to_now_playing_list(
                 b.cid, copy.data(), counter, scope);
}
void AvrcpController::setSettingsCallback(void (*cb)(const SettingsChunk &,
                                                     void *),
                                          void *context) {
  if (dispatching_)
    return;
  settings_callback_ = cb;
  settings_context_ = context;
}
int AvrcpController::displayableCharsets(const Binding &b, const uint16_t *ids,
                                         uint8_t count) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
  if (ids == nullptr || count == 0 || count > 5)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  std::array<uint16_t, 5> copy{};
  for (unsigned i = 0; i < count; ++i)
    copy[i] = ids[i];
  return status_.last_error = avrcp_controller_inform_displayable_characterset(
             b.cid, count, copy.data());
#else
  (void)ids;
  (void)count;
  return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
#endif
}
int AvrcpController::settingAttributeText(const Binding &b, const uint8_t *ids,
                                          uint8_t count) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
  if (ids == nullptr || count == 0 || count > 10)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return status_.last_error =
             picow_avrcp_controller_setting_attribute_text(b.cid, count, ids);
#else
  (void)ids;
  (void)count;
  return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
#endif
}
int AvrcpController::settingValueText(const Binding &b, uint8_t attribute,
                                      const uint8_t *ids, uint8_t count) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
  if (!attribute || ids == nullptr || count == 0 || count > 9)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return status_.last_error = picow_avrcp_controller_setting_value_text(
             b.cid, attribute, count, ids);
#else
  (void)attribute;
  (void)ids;
  (void)count;
  return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
#endif
}
int AvrcpController::currentSettings(const Binding &b, const uint8_t *ids,
                                     uint8_t count) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
  if (ids == nullptr || count == 0 || count > 10)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return status_.last_error =
             picow_avrcp_controller_current_settings(b.cid, count, ids);
#else
  (void)ids;
  (void)count;
  return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
#endif
}
int AvrcpController::setSettings(const Binding &b, const uint8_t *ids,
                                 const uint8_t *values, uint8_t count) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
  if (ids == nullptr || values == nullptr || count == 0 || count > 5)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return status_.last_error =
             picow_avrcp_controller_set_settings(b.cid, count, ids, values);
#else
  (void)ids;
  (void)values;
  (void)count;
  return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
#endif
}
int AvrcpController::shuffleMode(const Binding &b, avrcp_shuffle_mode_t value) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
  if (value < AVRCP_SHUFFLE_MODE_OFF || value > AVRCP_SHUFFLE_MODE_GROUP)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return status_.last_error = avrcp_controller_set_shuffle_mode(b.cid, value);
#else
  (void)value;
  return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
#endif
}
int AvrcpController::repeatMode(const Binding &b, avrcp_repeat_mode_t value) {
  if (!commandAllowed(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
  if (value < AVRCP_REPEAT_MODE_OFF || value > AVRCP_REPEAT_MODE_GROUP)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return status_.last_error = avrcp_controller_set_repeat_mode(b.cid, value);
#else
  (void)value;
  return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
#endif
}
void AvrcpController::reset() {
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
  for (const auto &s : slots_) if (s.binding.cid) picow_avrcp_controller_forget_settings(s.binding.cid);
#endif
  slots_ = {};
  emit({EventKind::Reset, {}});
}
void AvrcpController::stop() {
  if (dispatching_ || !attempted_ || status_.stopped)
    return;
  status_.started = false;
  status_.stopped = true;
  if (sdp_registered_) {
    sdp_unregister_service(sdp_handle_);
    sdp_registered_ = false;
  }
  const auto old = slots_;
  reset();
  for (const auto &s : old)
    if (s.binding.cid) {
      // Stop SDK repeat timer even when the target retains this shared channel.
      int release_error = 0;
      if (s.phase == Phase::PressPending || s.phase == Phase::Held)
        release_error =
            avrcp_controller_release_press_and_hold_cmd(s.binding.cid);
      // Failed release cannot leave the SDK repeating commands indefinitely.
      // Signalling is shared: this safety disconnect also ends the target role.
      const int result = release_error
                             ? avrcp_disconnect(s.binding.cid)
                             : AvrcpShared::instance().disconnectUnlessShared(
                                   AvrcpShared::Controller, s.binding.cid);
      if (release_error) {
        status_.last_error = release_error;
        ++status_.connection_errors;
      }
      if (result) {
        status_.last_error = result;
        ++status_.connection_errors;
      }
    }
}
bool AvrcpController::setCommandTimeout(uint32_t milliseconds) {
  if (dispatching_ || status_.stopped || milliseconds > 0x7fffffffu) return false;
  command_timeout_ms_ = milliseconds;
  return true;
}
void AvrcpController::poll() { pollImpl(false, 0); }
void AvrcpController::poll(uint32_t now_ms) { pollImpl(true, now_ms); }
void AvrcpController::pollImpl(bool timed, uint32_t now_ms) {
  if (dispatching_) return;
  BluetoothRuntime::instance().poll();
  if (BluetoothRuntime::instance().failed() && !status_.stopped) {
    status_.last_error = -1;
    stop();
  }
  if (!status_.started) return;
  for (auto &slot : slots_) {
    if (!slot.binding.cid) continue;
    const auto link = BluetoothRuntime::instance().linkForPeer(slot.binding.peer);
    if (!link.active || link.epoch != slot.binding.epoch) continue;
    auto *sdk = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER, slot.binding.cid);
    if (!sdk) continue;
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
    if (!slot.closing) picow_avrcp_controller_custom_poll(slot.binding.cid);
#endif
    const bool busy = slot.phase == Phase::PressPending || slot.phase == Phase::ReleasePending ||
        sdk->state > AVCTP_CONNECTION_OPENED;
    if (!busy && !slot.timeout_closing) slot.timer_armed = false;
    if (timed && busy && !slot.closing && !slot.timeout_closing) {
      if (!slot.timer_armed && command_timeout_ms_) {
        slot.timer_start = now_ms; slot.timer_budget = command_timeout_ms_;
        slot.timer_armed = true;
      } else if (slot.timer_armed && uint32_t(now_ms-slot.timer_start) >= slot.timer_budget) {
        slot.timeout_closing = true; slot.closing = true;
        status_.last_error = -1004;
        emit({EventKind::Timeout, slot.binding, slot.operation, 0});
      }
    }
    if (slot.timeout_closing) {
      // Quarantine both signalling roles, retaining SDK state until release.
      // Do not reset a transaction or permit a late reply to become a new one.
      const int result = avrcp_disconnect(slot.binding.cid);
      if (!result) slot.timeout_closing = false;
      else status_.last_error = result;
    }
  }
}
void AvrcpController::setCustomCallback(void (*cb)(const CustomChunk &, void *), void *context) {
  if (!dispatching_) { custom_callback_ = cb; custom_context_ = context; }
}
int AvrcpController::customCommand(const Binding &b, uint8_t command, uint8_t subunit,
    uint8_t subunit_id, uint8_t pdu, uint32_t company, const uint8_t *bytes, uint16_t length) {
  if (!commandAllowed(b)) return ERROR_CODE_COMMAND_DISALLOWED;
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
  return status_.last_error = picow_avrcp_controller_custom(b.cid, command, subunit,
      subunit_id, pdu, company, bytes, length);
#else
  (void)command;(void)subunit;(void)subunit_id;(void)pdu;(void)company;(void)bytes;(void)length;
  return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
#endif
}
void AvrcpController::packet(uint8_t t, uint16_t, uint8_t *p, uint16_t n) {
  if (active_)
    active_->receive(t, p, n);
}
void AvrcpController::hci(uint8_t t, uint16_t, uint8_t *p, uint16_t n) {
  if (!active_ || t != HCI_EVENT_PACKET || !p || n < 2 ||
      unsigned(p[1]) + 2 != n)
    return;
  auto &s = *active_;
  if (p[0] == BTSTACK_EVENT_STATE && n >= 3 &&
      btstack_event_state_get_state(p) == HCI_STATE_OFF) {
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
    for (const auto &slot : s.slots_) if (slot.binding.cid)
      picow_avrcp_controller_forget_custom(slot.binding.cid);
#endif
    s.reset();
    return;
  }
  if (p[0] == HCI_EVENT_DISCONNECTION_COMPLETE && n >= 6 &&
      !hci_event_disconnection_complete_get_status(p)) {
    const auto link = BluetoothRuntime::instance().linkForHandle(
        hci_event_disconnection_complete_get_connection_handle(p));
    for (auto &slot : s.slots_)
      if (slot.binding.cid && slot.binding.peer == link.peer &&
          slot.binding.epoch == link.epoch) {
        const auto b = slot.binding;
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
        picow_avrcp_controller_forget_settings(b.cid);
        picow_avrcp_controller_forget_custom(b.cid);
#endif
        slot = {};
        s.emit({EventKind::Disconnected, b});
      }
  }
}
void AvrcpController::receive(uint8_t t, uint8_t *p, uint16_t n) {
  if (t != HCI_EVENT_PACKET)
    return;
  if (!p || n < 3 || unsigned(p[1]) + 2 != n) {
    ++status_.malformed;
    return;
  }
  if (p[0] != HCI_EVENT_AVRCP_META)
    return;
  const auto kind = p[2];
  if (kind == AVRCP_SUBEVENT_CONNECTION_ESTABLISHED) {
    if (n < 14) {
      ++status_.malformed;
      return;
    }
    const auto error = avrcp_subevent_connection_established_get_status(p);
    if (error) {
      status_.last_error = error;
      ++status_.connection_errors;
      return;
    }
    Address peer{};
    avrcp_subevent_connection_established_get_bd_addr(p, peer.data());
    const auto id = avrcp_subevent_connection_established_get_avrcp_cid(p);
    if (!status_.started || !allowed(peer)) {
      if (!AvrcpShared::instance().accepted(peer))
        avrcp_disconnect(id);
      return;
    }
    const auto link = BluetoothRuntime::instance().linkForPeer(peer);
    if (!id || !link.active ||
        link.handle !=
            avrcp_subevent_connection_established_get_con_handle(p)) {
      ++status_.connection_errors;
      if (id)
        avrcp_disconnect(id);
      return;
    }
    if (cid(id) || bindingForPeer(peer).cid) {
      ++status_.ignored;
      return;
    }
    for (auto &slot : slots_)
      if (!slot.binding.cid &&
          generation_ != std::numeric_limits<uint32_t>::max()) {
        slot.binding = {peer, link.epoch, id, ++generation_};
        emit({EventKind::Connected, slot.binding});
        return;
      }
    ++status_.connection_errors;
    avrcp_disconnect(id);
    return;
  }
  if (kind == AVRCP_SUBEVENT_CONNECTION_RELEASED) {
    if (n < 5) {
      ++status_.malformed;
      return;
    }
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
    picow_avrcp_controller_forget_custom(avrcp_subevent_connection_released_get_avrcp_cid(p));
#endif
    auto *s = cid(avrcp_subevent_connection_released_get_avrcp_cid(p));
    if (!s) {
      ++status_.ignored;
      return;
    }
    const auto b = s->binding;
#if defined(PICOW_AVRCP_SDK_OVERLAY) && PICOW_AVRCP_SDK_OVERLAY
    picow_avrcp_controller_forget_settings(b.cid);
#endif
    *s = {};
    emit({EventKind::Disconnected, b});
    return;
  }
  if ((kind == AVRCP_SUBEVENT_OPERATION_START ||
       kind == AVRCP_SUBEVENT_OPERATION_COMPLETE) &&
      n != 7) {
    ++status_.malformed;
    return;
  }
  if (kind == 0xf2) {
    if (n != 7 || p[5] < 1 || p[5] > 4) { ++status_.malformed; return; }
    auto *slot = cid(little_endian_read_16(p,3));
    if (!status_.started || !slot || !find(slot->binding)) { ++status_.ignored; return; }
    status_.last_error = -1005;
    emit({EventKind::CustomError, slot->binding, p[6], p[5]});
  }
  if (kind == SettingsResponseSubevent || kind == CustomResponseSubevent) {
    const bool custom = kind == CustomResponseSubevent;
    if (n < 11) {
      ++status_.malformed;
      return;
    }
    auto *slot = cid(little_endian_read_16(p, 3));
    if (!status_.started || !slot || !find(slot->binding)) {
      ++status_.ignored;
      return;
    }
    const uint16_t total = little_endian_read_16(p, 7),
                   offset = little_endian_read_16(p, 9);
    const uint16_t length = n - 11;
    const bool response_ok = custom ? (p[5] >= 8 && p[5] <= 15 && p[5] != 14) :
        (p[5] == 8 || p[5] == 9 || p[5] == 10 || p[5] == 12);
    const bool pdu_ok = custom ||
        (p[6] >= 0x11 && p[6] <= 0x13) || (p[6] >= 0x15 && p[6] <= 0x18);
    if (!response_ok || !pdu_ok || uint32_t(offset) + length > total ||
        length != std::min<unsigned>(246, unsigned(total) - offset) ||
        offset != slot->settings_next ||
        (offset &&
         (total != slot->settings_total || p[5] != slot->settings_response ||
          p[6] != slot->settings_pdu))) {
      slot->settings_next = 0;
      ++status_.malformed;
      return;
    }
    slot->settings_total = total;
    slot->settings_response = p[5];
    slot->settings_pdu = p[6];
    slot->settings_next =
        offset + length == total ? 0 : uint16_t(offset + length);
    const auto cb = custom ? custom_callback_ : settings_callback_;
    if (cb) {
      dispatching_ = true;
      cb(
          {slot->binding, p[5], p[6], total, offset, p + 11, length},
          custom ? custom_context_ : settings_context_);
      dispatching_ = false;
    }
  }
  bool raw_delivered = false;
  if (n >= 5 && sdk_callback_ && status_.started) {
    auto *slot = cid(little_endian_read_16(p, 3));
    if (slot && find(slot->binding)) {
      dispatching_ = true;
      sdk_callback_({slot->binding, p, n}, sdk_context_);
      raw_delivered = true;
      dispatching_ = false;
    }
  }
  if (kind == AVRCP_SUBEVENT_OPERATION_START ||
      kind == AVRCP_SUBEVENT_OPERATION_COMPLETE) {
    if (n != 7) {
      ++status_.malformed;
      return;
    }
    const uint16_t id = little_endian_read_16(p, 3);
    auto *s = cid(id);
    if (!status_.started || !s || !find(s->binding) ||
        s->operation != (p[6] & 0x7f)) {
      ++status_.ignored;
      return;
    }
    const bool complete = kind == AVRCP_SUBEVENT_OPERATION_COMPLETE;
    if ((complete && s->phase != Phase::ReleasePending) ||
        (!complete && s->phase != Phase::PressPending &&
         s->phase != Phase::Held)) {
      ++status_.ignored;
      return;
    }
    const auto b = s->binding;
    const auto op = s->operation;
    s->phase = complete ? Phase::Idle : Phase::Held;
    s->timer_armed = false;
    if (complete)
      s->operation = 0;
    ++status_.responses;
    emit({complete ? EventKind::ReleaseResponse : EventKind::PressResponse, b,
          op, p[5]});
    // A rejected press still puts this SDK into hold state. Release it to stop
    // automatic repeats; expose its eventual response separately.
    if (!complete && p[5] != AVRCP_CTYPE_RESPONSE_ACCEPTED)
      release(b);
    return;
  }
  if (!raw_delivered)
    ++status_.ignored;
}
} // namespace picow::avrcp
