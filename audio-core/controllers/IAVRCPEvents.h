#ifndef I_AVRCP_EVENTS_H
#define I_AVRCP_EVENTS_H

#include <cstdint>

class IAVRCPEvents {
public:
    virtual ~IAVRCPEvents() = default;

    // --- Commands from Controller (e.g., Smartphone) ---
    virtual void onPlayPressed() = 0;
    virtual void onPausePressed() = 0;
    virtual void onStopPressed() = 0;
    virtual void onNextPressed() = 0;
    virtual void onPreviousPressed() = 0;
    virtual void onFastForwardPressed() = 0;
    virtual void onRewindPressed() = 0;

    // --- Notifications to Controller ---
    // Example: You might want to get a query for the current status
    virtual uint8_t getPlaybackStatus() = 0;
    virtual uint32_t getSongLength() = 0;
    virtual uint32_t getSongPosition() = 0;
};

#endif // I_AVRCP_EVENTS_H
