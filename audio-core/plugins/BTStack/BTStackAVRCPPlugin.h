#ifndef BTSTACK_AVRCP_PLUGIN_H
#define BTSTACK_AVRCP_PLUGIN_H

#pragma once

#include "IPlugin.h"

class AVRCPTargetController;
struct btstack_context_t; // Forward declaration

class BTStackAVRCPPlugin : public IPlugin {
public:
    BTStackAVRCPPlugin();
    ~BTStackAVRCPPlugin() override;

    void setup(AVRCPTargetController* controller, btstack_context_t* context);
    void connect() override;
    void loop() override;

private:
    AVRCPTargetController* controller_ = nullptr;
    btstack_context_t* context_ = nullptr;
};

#endif // BTSTACK_AVRCP_PLUGIN_H
