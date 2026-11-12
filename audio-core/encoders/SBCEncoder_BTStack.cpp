#include "SBCEncoder_BTStack.h"
#include <cstring>
SBCEncoder_BTStack::SBCEncoder_BTStack() {
    configure(48000, 16, 8, SBC_ALLOCATION_METHOD_LOUDNESS, 53, SBC_CHANNEL_MODE_STEREO);
}
bool SBCEncoder_BTStack::configure(int rate, int blocks, int bands,
    btstack_sbc_allocation_method_t allocation, int pool, btstack_sbc_channel_mode_t mode) {
    if ((rate != 16000 && rate != 32000 && rate != 44100 && rate != 48000) ||
        (blocks != 4 && blocks != 8 && blocks != 12 && blocks != 16) ||
        (bands != 4 && bands != 8) || mode < SBC_CHANNEL_MODE_MONO || mode > SBC_CHANNEL_MODE_JOINT_STEREO ||
        (allocation != SBC_ALLOCATION_METHOD_LOUDNESS && allocation != SBC_ALLOCATION_METHOD_SNR) ||
        pool < 2 || pool > 250 || pool > (mode < SBC_CHANNEL_MODE_STEREO ? 16 : 32)*bands) return false;
    blocks_ = blocks; subbands_ = bands; channels_ = mode == SBC_CHANNEL_MODE_MONO ? 1 : 2;
    const int bits = mode < SBC_CHANNEL_MODE_STEREO ? blocks*channels_*pool : blocks*pool + (mode == SBC_CHANNEL_MODE_JOINT_STEREO ? bands : 0);
    frame_size_ = 4 + bands*channels_/2 + (bits+7)/8;
    btstack_sbc_encoder_init(&sbc_encoder_state_, SBC_MODE_STANDARD, blocks, bands, allocation, rate, pool, mode);
    return true;
}
size_t SBCEncoder_BTStack::requiredSamples() const { return samplesPerChannel()*channels_; }
size_t SBCEncoder_BTStack::maxEncodedSize() const { return frame_size_; }
std::vector<uint8_t> SBCEncoder_BTStack::encode(const int16_t* pcm, size_t samples) {
    std::vector<uint8_t> result(maxEncodedSize());
    result.resize(encode_into(pcm, samples, result.data(), result.size()));
    return result;
}
size_t SBCEncoder_BTStack::encode_into(const int16_t* pcm, size_t samples, uint8_t* out, size_t cap) {
    const size_t size = maxEncodedSize();
    if (!pcm || !out || samples != requiredSamples() || !size || cap < size) return 0;
    std::memcpy(pcm_copy_, pcm, samples*sizeof(int16_t));
    btstack_sbc_encoder_process_data(pcm_copy_);
    if (btstack_sbc_encoder_sbc_buffer_length() != size) return 0;
    std::memcpy(out, btstack_sbc_encoder_sbc_buffer(), size);
    return size;
}
