#include "controllers/AVRCPTargetController.h"

// These opcodes would typically be included from a BTstack header like <avrcp.h>
// Defining them here as placeholders.
#define AVRCP_OPERATION_ID_PLAY 0x44
#define AVRCP_OPERATION_ID_PAUSE 0x46
#define AVRCP_OPERATION_ID_STOP 0x45
#define AVRCP_OPERATION_ID_NEXT_GROUP 0x4B
#define AVRCP_OPERATION_ID_PREVIOUS_GROUP 0x4C
#define AVRCP_OPERATION_ID_FF 0x49
#define AVRCP_OPERATION_ID_REWIND 0x48

// Placeholder for the actual BTstack notification function
extern "C" void avrcp_target_set_playback_status(uint8_t status);

AVRCPTargetController::AVRCPTargetController(IAVRCPEvents* event_handler)
    : event_handler_(event_handler) {}

void AVRCPTargetController::onAVRCPCommand(uint8_t opcode) {
    if (!event_handler_) return;

    switch (opcode) {
        case AVRCP_OPERATION_ID_PLAY:
            event_handler_->onPlayPressed();
            break;
        case AVRCP_OPERATION_ID_PAUSE:
            event_handler_->onPausePressed();
            break;
        case AVRCP_OPERATION_ID_STOP:
            event_handler_->onStopPressed();
            break;
        case AVRCP_OPERATION_ID_NEXT_GROUP:
            event_handler_->onNextPressed();
            break;
        case AVRCP_OPERATION_ID_PREVIOUS_GROUP:
            event_handler_->onPreviousPressed();
            break;
        case AVRCP_OPERATION_ID_FF:
            event_handler_->onFastForwardPressed();
            break;
        case AVRCP_OPERATION_ID_REWIND:
            event_handler_->onRewindPressed();
            break;
        default:
            // Other opcodes not handled
            break;
    }
}

void AVRCPTargetController::notifyPlaybackStatusChanged(uint8_t status) {
    // This function would call the actual BTstack API to send the notification
    // For example: avrcp_target_set_playback_status(status);
}
