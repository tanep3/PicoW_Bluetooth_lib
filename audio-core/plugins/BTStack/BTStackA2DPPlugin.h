#ifndef BTSTACK_A2DP_PLUGIN_H
#define BTSTACK_A2DP_PLUGIN_H

#pragma once

#include "IPlugin.h"
#include "IAudioSource.h"
#include "IAudioEncoder.h"
#include "ITransport.h"
#include <memory>

// 前方宣言
struct btstack_context_t;

class BTStackA2DPPlugin : public IPlugin {
public:
    BTStackA2DPPlugin(IAudioSource* source, IAudioEncoder* encoder, ITransport* transport, btstack_context_t* context);
    ~BTStackA2DPPlugin() override;

    void setup() override; // No arguments
    void connect() override;
    void loop() override;

    // transportを後から注入するためのsetter
    void setTransport(std::unique_ptr<ITransport> transport);

private:
    IAudioSource* source_ = nullptr;
    IAudioEncoder* encoder_ = nullptr;
    std::unique_ptr<ITransport> transport_;
    btstack_context_t* context_ = nullptr;
};

#endif // BTSTACK_A2DP_PLUGIN_H
