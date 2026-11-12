#pragma once

#include "IPlugin.h"
#include "BTStackConnectionPlugin.h"
#include <functional>
#include "ConnectionInfo.h"
#include "IAddressConnectable.h"

// Forward declaration of bd_addr_t
typedef uint8_t bd_addr_t[6];

class AVRCPConnectionPlugin : public IPlugin, public IAddressConnectable {
public:
    explicit AVRCPConnectionPlugin(BTStackConnectionPlugin* connection_plugin);
    virtual ~AVRCPConnectionPlugin();

    // IPlugin interface
    void setup() override;
    // Dummy implementations of pure virtual methods from IPlugin
    void connect() override;
    void loop() override;

    // Custom connect method with remote BD_ADDR
    void connect(bd_addr_t address) override;
    void disconnect();
    bool isConnected();

    // Setters for callbacks
    void setOnAvrcpConnectedCallback(std::function<void(bd_addr_t)> callback);
// "A2DPConnectInfo"を渡すコールバック
    // void setOnA2dpConnectedCallback(std::function<void(const AVRCPConnectInfo&)> callback);
    void setOnAvrcpDisconnectedCallback(std::function<void(bd_addr_t)> callback);

    // Callback functions are public for direct assignment
    std::function<void(bd_addr_t)> on_avrcp_connected_callback_;
    std::function<void(bd_addr_t)> on_avrcp_disconnected_callback_;

private:
    BTStackConnectionPlugin* connection_plugin_;
    uint16_t avrcp_cid_;
    bool is_connected_;
    bd_addr_t remote_addr_;

    // Static packet handler
    static void packetHandler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size);
};