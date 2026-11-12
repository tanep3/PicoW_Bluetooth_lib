#pragma once

#include "IPlugin.h"
#include "BTStackConnectionPlugin.h"
#include "btstack_event.h"
#include "btstack_debug.h"
#include <functional>
#include "ConnectionInfo.h"
#include "IAddressConnectable.h"
#include "BTStackA2DPTransport.h"

// Forward declaration of bd_addr_t
typedef uint8_t bd_addr_t[6];

class A2DPConnectionPlugin : public IPlugin, public IAddressConnectable {
public:
    explicit A2DPConnectionPlugin(BTStackConnectionPlugin* connection_plugin);
    virtual ~A2DPConnectionPlugin();

    // IPlugin interface
    void setup() override;
    // Dummy implementations of pure virtual methods from IPlugin.
    // These allow the class to be instantiated via std::make_unique.
    void connect() override;
    void loop() override;

    // Custom connect method that specifies the remote device address.
    void connect(bd_addr_t address) override;
    void disconnect();
    bool isConnected();

    // Setters for connection/disconnection callbacks
    void setOnA2dpConnectedCallback(std::function<void(const A2DPConnectInfo&)> callback);
    void setOnA2dpDisconnectedCallback(std::function<void(bd_addr_t)> callback);

    // Callback function objects are public so main.cpp can assign to them
    std::function<void(const A2DPConnectInfo&)> on_a2dp_connected_callback_;
    std::function<void(bd_addr_t)> on_a2dp_disconnected_callback_;

private:
    BTStackConnectionPlugin* connection_plugin_;
    uint16_t a2dp_cid_;
    uint8_t local_seid_;
    bool is_connected_ = false;
    bd_addr_t remote_addr_;

    // Established stream parameters
    int max_media_payload_size_ = 0;
    uint32_t rtp_timestamp_ = 0;
    uint8_t* sbc_storage_ = nullptr;
    uint16_t sbc_storage_count_ = 0;
    BTStackA2DPTransport* transport_ = nullptr; 

    // Static packet handler for A2DP events
    static void packetHandler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size);
};
