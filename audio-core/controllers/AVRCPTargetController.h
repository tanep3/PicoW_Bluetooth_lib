#ifndef AVRCP_TARGET_CONTROLLER_H
#define AVRCP_TARGET_CONTROLLER_H

#include "controllers/IAVRCPEvents.h"
#include <cstdint>

// Forward declaration for BTstack types if needed

class AVRCPTargetController {
public:
    explicit AVRCPTargetController(IAVRCPEvents* event_handler);

    // Called from the BTstack AVRCP callback
    void onAVRCPCommand(uint8_t opcode);

    // Call this to notify the controller (e.g., phone) about status changes
    void notifyPlaybackStatusChanged(uint8_t status);

private:
    IAVRCPEvents* event_handler_;
};

#endif // AVRCP_TARGET_CONTROLLER_H
