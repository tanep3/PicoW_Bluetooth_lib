#pragma once

#include <functional>
#include "ConnectionInfo.h"

class AudioManager;
class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual void setup() = 0;     // 初期化
    virtual void connect() = 0;   // 接続開始（Bluetoothなど）
    virtual void loop() = 0;      // 定期呼び出し

    // 汎用接続コールバック
    virtual void setOnConnectedCallback(std::function<void(const PluginConnectionInfo&)> cb) {}

    AudioManager* manager_ = nullptr;
};
