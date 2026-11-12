#ifndef A2DP_DATA_RECEIVER_H
#define A2DP_DATA_RECEIVER_H

#include "sources/A2DPAudioSource.h"

class A2DPDataReceiver {
public:
    explicit A2DPDataReceiver(A2DPAudioSource* audio_source);

    // This method will be called from the BTstack's PCM callback
    void onPCMDataReceived(const int16_t* data, size_t samples);

private:
    A2DPAudioSource* target_source_; // The destination for the received PCM data
};

#endif // A2DP_DATA_RECEIVER_H
