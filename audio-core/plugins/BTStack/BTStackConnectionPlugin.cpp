#include "BTStackConnectionPlugin.h"
#include "pico/cyw43_arch.h"
#include "btstack_hci_transport_cyw43.h"
// #include "btstack.h"
// #include "btstack_cyw43.h"
// #include "btstack_chipset_cyw43.h"
// #include "btstack_chipset.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_embedded.h"
#include "btstack_memory.h"
#include "hci.h"
#include "hci_event.h"
#include "l2cap.h"
#include "sdp_client.h"
#include "gap.h"
#include "DebugLog.h"
#include <cstring>
#include <map>

// LGFX display;
// extern LGFX display;

BTStackConnectionPlugin* BTStackConnectionPlugin::instance = nullptr;

// Helper to order bd_addr_t keys in std::map.
using BdAddr = std::array<uint8_t, 6>;
struct BdAddrComp {
    bool operator()(const BdAddr &a, const BdAddr &b) const {
        return memcmp(a.data(), b.data(), 6) < 0;
    }
};

static std::map<BdAddr, hci_con_handle_t, BdAddrComp> s_connection_handles;
static std::map<BdAddr, bool, BdAddrComp> s_connection_status;
static std::map<hci_con_handle_t, BdAddr> s_handle_to_address;

BTStackConnectionPlugin::BTStackConnectionPlugin(btstack_context_t* context)
    : btstack_context_(context), is_scanning_(false), selected_uuid_(0), target_address_set_(false) {
    instance = this;
}

BTStackConnectionPlugin::~BTStackConnectionPlugin() {
    instance = nullptr;
}

void BTStackConnectionPlugin::setup() {
    is_connected_ = false;

    if (cyw43_arch_init()) {
        // handle error
        while(1) {}
    }

    // Global BTstack settings from sample/main.cpp
    hci_set_master_slave_policy(0); // Request role change on reconnecting headset to always use them in slave mode
    hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR); // Set inquiry mode for better device discovery

    // btstack_run_loop_init(btstack_run_loop_embedded_get_instance());
    // btstack_memory_init();
    
    // hci_init(hci_transport_cyw43_instance(), nullptr);
    // hci_init(hci_get_transport(), hci_get_chipset(), NULL);
    l2cap_init();
    sdp_init();

    gap_set_local_name("PicoW-Echo 00:00:00:00:00:00");
    gap_discoverable_control(1);
    // デバイスのクラスを設定します。
    gap_set_class_of_device(0x200408);

    hci_event_callback_registration_.callback = &BTStackConnectionPlugin::packetHandler;
    hci_add_event_handler(&hci_event_callback_registration_);

    // hci_power_control(HCI_POWER_ON);
}

void BTStackConnectionPlugin::connect() {
    startScan();
}

void BTStackConnectionPlugin::loop() {
    // The main loop is now handled by AudioManager::execute()
    btstack_run_loop_embedded_execute_once();
}

void BTStackConnectionPlugin::connectToAddress(bd_addr_t address) {
    if (is_scanning_) {
        gap_inquiry_stop();
        is_scanning_ = false;
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "connectToAddress() addr=%02X:%02X:%02X:%02X:%02X:%02X", address[0],address[1],address[2],address[3],address[4],address[5]);
    PWB_LOG_MSG(msg);
    PWB_SLEEP_MS(1000);
    // 既接続・接続試行中判定はイベント側で処理。ここではACL接続を開始する（Classic用に BD_ADDR_TYPE_ACL を指定）。
    // gap_connect(address, BD_ADDR_TYPE_ACL);
}

void BTStackConnectionPlugin::disconnect() {
    // Implementation for disconnection
    for (auto it = s_connection_handles.begin(); it != s_connection_handles.end(); ++it) {
        hci_con_handle_t handle = it->second;
        // issue disconnect for each active handle
        gap_disconnect(handle);
    }
    // Clear stored handles; the disconnection complete event will update clients via callback.
        s_connection_handles.clear();
    }

    void BTStackConnectionPlugin::performServiceDiscovery(bd_addr_t address) {
    // Implementation for service discovery
}

void BTStackConnectionPlugin::selectServiceUUID(uint16_t uuid) {
    selected_uuid_ = uuid;
}

bool BTStackConnectionPlugin::isConnected(bd_addr_t address) {
    BdAddr key;
    memcpy(key.data(), address, 6);
    auto it = s_connection_status.find(key);
    if (it != s_connection_status.end()) {
        return it->second;
    }
    return false;
}

void BTStackConnectionPlugin::setTargetDeviceAddress(bd_addr_t address) {
    memcpy(target_address_, address, sizeof(bd_addr_t));
    target_address_set_ = true;
}

void BTStackConnectionPlugin::setOnReadyCallback(std::function<void()> callback) {
    on_ready_callback_ = callback;
}

void BTStackConnectionPlugin::setOnDeviceFoundCallback(std::function<void(bd_addr_t)> callback) {
    on_device_found_callback_ = callback;
}

void BTStackConnectionPlugin::setOnConnectedCallback(std::function<void(bd_addr_t)> callback) {
    on_connected_callback_ = callback;
}

void BTStackConnectionPlugin::setOnDisconnectedCallback(std::function<void(bd_addr_t)> callback) {
    on_disconnected_callback_ = callback;
}

void BTStackConnectionPlugin::setOnPinCodeRequestCallback(std::function<void(bd_addr_t, uint32_t)> callback) {
    on_pin_code_request_callback_ = callback;
}

void BTStackConnectionPlugin::setOnUUIDDiscoveredCallback(std::function<void(bd_addr_t, std::vector<uint16_t>)> callback) {
    on_uuid_discovered_callback_ = callback;
}

void BTStackConnectionPlugin::startScan() {
    if (!is_scanning_) {
        is_scanning_ = true;
        gap_inquiry_start(10); // Scan for 5 * 1.28s
    }
}

void BTStackConnectionPlugin::packetHandler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    char msg[64];
    if (!instance) return;

    if (packet_type != HCI_EVENT_PACKET) return;

    if (hci_event_packet_get_type(packet) != 110 && hci_event_packet_get_type(packet) != 14) {
        snprintf(msg, sizeof(msg), "パケットハンドル: %d", hci_event_packet_get_type(packet));
        // display.display_msg(msg);
        // sleep_ms(1000);
    }

    uint8_t status = 0;
    bd_addr_t addr{};
    hci_con_handle_t handle = 0;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                if (instance->on_ready_callback_) {
                    instance->on_ready_callback_();
                }
            }
            break;
        case HCI_EVENT_PIN_CODE_REQUEST:
            PWB_LOG_MSG("pin code kitaaaaaaaa");
            PWB_SLEEP_MS(2000);
            hci_event_pin_code_request_get_bd_addr(packet, addr);
            if (instance->on_pin_code_request_callback_) {
                PWB_LOG_MSG("pin code コールバックを採用");
                PWB_SLEEP_MS(2000);
                instance->on_pin_code_request_callback_(addr, 0);
            } else {
                PWB_LOG_MSG("pin code デフォを採用");
                gap_pin_code_response(addr, "0000");
            }
            break;
        case GAP_EVENT_INQUIRY_RESULT: {
            gap_event_inquiry_result_get_bd_addr(packet, addr);
            uint32_t cod = gap_event_inquiry_result_get_class_of_device(packet);
            snprintf(msg, sizeof(msg), "with COD: %06" PRIx32, cod);
            PWB_LOG_MSG(msg);
            PWB_SLEEP_MS(2000);
            if (gap_event_inquiry_result_get_name_available(packet)){
                char name_buffer[240];
                int name_len = gap_event_inquiry_result_get_name_len(packet);
                memcpy(name_buffer, gap_event_inquiry_result_get_name(packet), name_len);
                name_buffer[name_len] = 0;
                snprintf(msg, sizeof(msg), ", name '%s'", name_buffer);
                PWB_LOG_MSG(msg);
                PWB_SLEEP_MS(2000);
            }
            // if (instance->target_address_set_ && bd_addr_cmp(addr, instance->target_address_) == 0) {
            //     if (instance->is_scanning_){
            //         gap_inquiry_stop();
            //         instance->is_scanning_ = false;
            //     }
            //     // instance->connectToAddress(addr);
            // }
            PWB_LOG_MSG("GAP_EVENT_INQUIRY_RESULT");
            instance->is_connected_ = true;
            PWB_SLEEP_MS(2000);
            if (instance->on_device_found_callback_) {
                instance->on_device_found_callback_(addr);
            }
            break;
        }
        case GAP_EVENT_INQUIRY_COMPLETE:
            PWB_LOG_MSG("GAP_EVENT_INQUIRY_COMPLETE");
            // まだ接続してなければ再スキャン
            if (!instance->is_connected_) {  // ← 自前の接続フラグなど
                PWB_LOG_MSG("未接続なので再スキャン");
                PWB_SLEEP_MS(1000); // 軽くウェイト
                if (!instance->is_scanning_) {
                    gap_inquiry_start(10); // ← スキャン再開
                    instance->is_scanning_ = true;
                }
            }
            break;
        case HCI_EVENT_CONNECTION_COMPLETE:
            hci_event_connection_complete_get_bd_addr
            (packet, addr);
            status = hci_event_connection_complete_get_status(packet);
            if (status == ERROR_CODE_SUCCESS) {
                BdAddr key;
                memcpy(key.data(), addr, 6);
                s_connection_status[key] = true;
                handle = hci_event_connection_complete_get_connection_handle(packet);
                s_handle_to_address[handle] = key;
                // ☆ 接続成功の通知
                if (instance->on_connected_callback_) {
                    instance->on_connected_callback_(addr);
                    // instance->is_connected_ = true;
                }
            }
            break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            {
                instance->is_connected_ = false;
                PWB_LOG_MSG("Disconnected! 再スキャン準備");
                PWB_SLEEP_MS(1000);
                // ★ 切断理由で挙動を変える（自分で切ったならスキャンしない 等）
                const uint8_t reason = hci_event_disconnection_complete_get_reason(packet);
                // 例: LOCAL_HOST_TERMINATED_CONNECTION(0x16)等はアプリ都合の切断
                const bool should_rescan =
                    (reason != ERROR_CODE_REMOTE_USER_TERMINATED_CONNECTION) &&
                    (reason != ERROR_CODE_CONNECTION_TERMINATED_BY_LOCAL_HOST);

                // ★ 逆引きして状態落とし
                handle = hci_event_disconnection_complete_get_connection_handle(packet);
                auto it = s_handle_to_address.find(handle);
                if (it != s_handle_to_address.end()) {
                    s_connection_status[it->second] = false;

                    if (instance->on_disconnected_callback_) {
                        bd_addr_t disconnected_addr;
                        memcpy(disconnected_addr, it->second.data(), 6);
                        instance->on_disconnected_callback_(disconnected_addr);
                    }
                    s_handle_to_address.erase(it);
                }

                // ★ コントローラ状態とフラグを見てから inquiry 再開
                if (should_rescan) {
                    if (!instance->is_scanning_ && hci_get_state() == HCI_STATE_WORKING) {
                        PWB_LOG_MSG("再スキャン開始");
                        gap_inquiry_start(10);
                        instance->is_scanning_ = true;
                    }
                }
                break;
            }
        default:
            break;
    }
}
