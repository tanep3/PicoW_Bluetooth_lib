#ifndef BTSTACK_CONNECTION_PLUGIN_H
#define BTSTACK_CONNECTION_PLUGIN_H

#pragma once

#include "IPlugin.h"
#include "btstack_config.h"
#include "btstack.h"
#include <functional>
#include <vector>
#include <map>

// Forward declaration
struct btstack_context_t;

class BTStackConnectionPlugin : public IPlugin {
public:
    explicit BTStackConnectionPlugin(btstack_context_t* context);
    ~BTStackConnectionPlugin() override;

    void setup() override;
    void connect() override; // Starts inquiry
    void loop() override;

    void connectToAddress(bd_addr_t address);
    void disconnect();
    void performServiceDiscovery(bd_addr_t address);
    void selectServiceUUID(uint16_t uuid);
    bool isConnected(bd_addr_t address);
    void setTargetDeviceAddress(bd_addr_t address);

    void setOnReadyCallback(std::function<void()> callback);
    void setOnDeviceFoundCallback(std::function<void(bd_addr_t)> callback);
    void setOnConnectedCallback(std::function<void(bd_addr_t)> callback);
    void setOnDisconnectedCallback(std::function<void(bd_addr_t)> callback);
    void setOnPinCodeRequestCallback(std::function<void(bd_addr_t, uint32_t)> callback);
    void setOnUUIDDiscoveredCallback(std::function<void(bd_addr_t, std::vector<uint16_t>)> callback);

private:
    btstack_packet_callback_registration_t hci_event_callback_registration_;
    btstack_context_t* btstack_context_;

    bd_addr_t target_address_;
    bool target_address_set_ = false;

    bool is_scanning_;
    bool is_connected_;
    
    // Connection status and discovered UUIDs for each device
    std::map<bd_addr_t, bool> connection_status_;
    std::map<bd_addr_t, std::vector<uint16_t>> discovered_uuids_;
    uint16_t selected_uuid_;

    std::function<void()> on_ready_callback_;
    std::function<void(bd_addr_t)> on_device_found_callback_;
    std::function<void(bd_addr_t)> on_connected_callback_;
    std::function<void(bd_addr_t)> on_disconnected_callback_;
    std::function<void(bd_addr_t, uint32_t)> on_pin_code_request_callback_;
    std::function<void(bd_addr_t, std::vector<uint16_t>)> on_uuid_discovered_callback_;

    void startScan();

    // Static handler to be registered with BTStack
    static void packetHandler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
    static BTStackConnectionPlugin* instance; // Add this line
};

#endif // BTSTACK_CONNECTION_PLUGIN_H
