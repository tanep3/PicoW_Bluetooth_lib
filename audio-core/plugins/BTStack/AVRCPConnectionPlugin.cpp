#include "AVRCPConnectionPlugin.h"
#include "btstack_debug.h"
#include "btstack_event.h"
#include "avrcp.h"
#include "avrcp_controller.h"
#include "avrcp_target.h"

static AVRCPConnectionPlugin* s_instance = nullptr;

AVRCPConnectionPlugin::AVRCPConnectionPlugin(BTStackConnectionPlugin* connection_plugin)
    : connection_plugin_(connection_plugin),
      avrcp_cid_(0),
      is_connected_(false) {
    s_instance = this;
    memset(remote_addr_, 0, sizeof(remote_addr_));
}

AVRCPConnectionPlugin::~AVRCPConnectionPlugin() {
    s_instance = nullptr;
}

void AVRCPConnectionPlugin::setup() {
    // Initialize AVRCP core and roles. Both controller and target roles must
    // be initialized because avrcp_connect asserts that their packet handlers
    // are registered【320212118550112†L1453-L1456】.
    avrcp_init();
    avrcp_controller_init();
    avrcp_target_init();

    // Register generic and controller packet handlers. The generic handler
    // receives all AVRCP meta events, while the controller handler is used
    // for controller-specific responses【23124594318658†L290-L304】.
    avrcp_register_packet_handler(&AVRCPConnectionPlugin::packetHandler);
    avrcp_controller_register_packet_handler(&AVRCPConnectionPlugin::packetHandler);
}

void AVRCPConnectionPlugin::connect() {
    // ダミー。必要なら何か、要らなければ空でOK
}

void AVRCPConnectionPlugin::connect(bd_addr_t address) {
    // Ensure a Bluetooth ACL link exists before establishing AVRCP signalling.
    if (!connection_plugin_->isConnected(address)) {
        connection_plugin_->connectToAddress(address);
    }
    bd_addr_copy(remote_addr_, address);
    // Initiate AVRCP connection. A corresponding subevent will inform about
    // success or failure via the packet handler【23124594318658†L390-L398】.
    avrcp_connect(remote_addr_, &avrcp_cid_);
}

void AVRCPConnectionPlugin::loop() {
    // ダミー。必要なら何か、要らなければ空でOK
}

void AVRCPConnectionPlugin::disconnect() {
    if (avrcp_cid_ != 0) {
        avrcp_disconnect(avrcp_cid_);
        avrcp_cid_ = 0;
    }
}

bool AVRCPConnectionPlugin::isConnected() {
    return is_connected_;
}

void AVRCPConnectionPlugin::setOnAvrcpConnectedCallback(std::function<void(bd_addr_t)> callback) {
    on_avrcp_connected_callback_ = callback;
}

void AVRCPConnectionPlugin::setOnAvrcpDisconnectedCallback(std::function<void(bd_addr_t)> callback) {
    on_avrcp_disconnected_callback_ = callback;
}

void AVRCPConnectionPlugin::packetHandler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    if (!s_instance || packet_type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) {
        return;
    }
    bd_addr_t address;
    switch (hci_event_avrcp_meta_get_subevent_code(packet)) {
        case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED: {
            // Get status and address from the event. Only update state on success.
            uint8_t status = avrcp_subevent_connection_established_get_status(packet);
            avrcp_subevent_connection_established_get_bd_addr(packet, address);
            uint16_t local_cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
            if (status == ERROR_CODE_SUCCESS) {
                s_instance->avrcp_cid_ = local_cid;
                s_instance->is_connected_ = true;
                bd_addr_copy(s_instance->remote_addr_, address);
                if (s_instance->on_avrcp_connected_callback_) {
                    s_instance->on_avrcp_connected_callback_(address);
                }
            } else {
                // Connection failed
                s_instance->avrcp_cid_ = 0;
            }
            break;
        }
        case AVRCP_SUBEVENT_CONNECTION_RELEASED: {
            s_instance->is_connected_ = false;
            s_instance->avrcp_cid_ = 0;
            if (s_instance->on_avrcp_disconnected_callback_) {
                s_instance->on_avrcp_disconnected_callback_(s_instance->remote_addr_);
            }
            break;
        }
        default:
            break;
    }
}
