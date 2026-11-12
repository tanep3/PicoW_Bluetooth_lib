#include "AvrcpController.h"
#include "AvrcpTarget.h"
// Compile/link probe only. Does not initialize the radio when booted.
// Keep a reachable wrapper so linker verifies all adapter SDK dependencies.
volatile bool initialize_for_link_check = false;
int main() {
  static picow::avrcp::AvrcpTarget target;
  if (initialize_for_link_check) {
    picow::avrcp::AvrcpTarget::Config config;
    config.peer_count = 1;
    config.peers[0] = {{1, 2, 3, 4, 5, 6}};
    target.begin(config);
    static picow::avrcp::AvrcpController controller;
    controller.begin(config);
    controller.connect(config.peers[0]);
    const auto binding = controller.bindingForPeer(config.peers[0]);
    controller.press(binding, 0x44);
    controller.release(binding);
    controller.group(binding, true);
    for (unsigned i = 0; i < 10; ++i)
      controller.query(binding,
                       static_cast<picow::avrcp::AvrcpController::Query>(i));
    controller.notification(binding, AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED,
                            true);
    const avrcp_media_attribute_id_t attributes[]{AVRCP_MEDIA_ATTR_TITLE};
    controller.elementAttributes(binding, attributes, 1);
    controller.settingValues(
        binding,
        static_cast<avrcp_player_application_setting_attribute_id_t>(1));
    const uint16_t charsets[]{106};
    const uint8_t settingIds[]{2}, settingValues[]{1};
    controller.displayableCharsets(binding, charsets, 1);
    controller.settingAttributeText(binding, settingIds, 1);
    controller.settingValueText(binding, 2, settingValues, 1);
    controller.currentSettings(binding, settingIds, 1);
    controller.setSettings(binding, settingIds, settingValues, 1);
    controller.shuffleMode(binding, static_cast<avrcp_shuffle_mode_t>(1));
    controller.repeatMode(binding, static_cast<avrcp_repeat_mode_t>(1));
    controller.absoluteVolume(binding, 64);
    controller.batteryStatus(binding, static_cast<avrcp_battery_status_t>(0));
    controller.addressedPlayer(binding, 1);
    controller.maximumFragments(binding, 2);
    const std::array<uint8_t, 8> uid{};
    controller.playItem(binding, uid, 0,
                        static_cast<avrcp_browsing_scope_t>(1));
    controller.addItem(binding, uid, 0, static_cast<avrcp_browsing_scope_t>(1));
    controller.poll();
    controller.disconnect(binding);
    controller.stop();
    target.poll();
    target.stop();
  }
  return 0;
}
