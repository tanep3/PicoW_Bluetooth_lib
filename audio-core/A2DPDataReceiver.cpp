#include "A2DPDataReceiver.h"

A2DPDataReceiver::A2DPDataReceiver(A2DPAudioSource* audio_source)
    : target_source_(audio_source) {}

void A2DPDataReceiver::onPCMDataReceived(const int16_t* data, size_t samples) {
    if (target_source_) {
        target_source_->write(data, samples);
    }
}
