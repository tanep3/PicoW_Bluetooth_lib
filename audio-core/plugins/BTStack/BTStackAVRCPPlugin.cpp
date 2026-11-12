#include "btstack_config.h" // Added
#include "plugins/BTStack/BTStackAVRCPPlugin.h"
#include "AVRCPTargetController.h"

// Required BTstack headers
#include "avrcp_target.h"
#include "btstack_run_loop.h"
#include "btstack_event.h" // For btstack_packet_handler_t

static AVRCPTargetController* g_avrcp_ctrl = nullptr;

// Corrected signature for btstack_packet_handler_t
extern "C" void btstack_internal_avrcp_cb(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;

    // Extract opcode from packet (assuming it's a pass-through command)
    // This part might need more sophisticated parsing depending on AVRCP command type
    uint8_t opcode = packet[2]; // Placeholder, actual opcode parsing is more complex

    if (g_avrcp_ctrl) {
        g_avrcp_ctrl->onAVRCPCommand(opcode); // Corrected method name
    }
}

BTStackAVRCPPlugin::BTStackAVRCPPlugin() = default;

BTStackAVRCPPlugin::~BTStackAVRCPPlugin() = default;

void BTStackAVRCPPlugin::setup(AVRCPTargetController* controller, btstack_context_t* context) {
    controller_ = controller;
    context_ = context; // Store context for future use
    g_avrcp_ctrl = controller_;

    // btstack_run_loop_init(btstack_run_loop_posix_get_instance()); // Temporarily commented out
    avrcp_target_init();
    avrcp_target_register_packet_handler(&btstack_internal_avrcp_cb);
}

void BTStackAVRCPPlugin::connect() {
    // AVRCPは通常はパッシブ接続なので、特にやることがなければ空でもOK
}

void BTStackAVRCPPlugin::loop() {
    // AVRCP用の定期処理が必要ならここ。通常は空でもOK。
}
