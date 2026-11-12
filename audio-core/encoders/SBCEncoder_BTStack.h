// SBCエンコーダ実装
#pragma once
#include "IAudioEncoder.h"
#include "btstack_sbc.h" // btstack_sbc_encoder_state_t のために必要

// SBCエンコード用バッファ長（BTstackに依存）
#define SBC_PCM_FRAME_SIZE 256
#define SBC_NUM_CHANNELS   2

class SBCEncoder_BTStack : public IAudioEncoder {
public:
    SBCEncoder_BTStack();

    bool configure(int sample_rate, int blocks, int subbands,
        btstack_sbc_allocation_method_t allocation, int bitpool, btstack_sbc_channel_mode_t mode);
    size_t samplesPerChannel() const { return blocks_ * subbands_; }
    size_t requiredSamples() const override;
    size_t maxEncodedSize() const override;

    std::vector<uint8_t> encode(const int16_t* pcm, size_t samples) override;
    size_t encode_into(const int16_t* pcm, size_t samples, uint8_t* out, size_t out_cap) override;

private:
    size_t frame_size_ = 0;
    int blocks_ = 16, subbands_ = 8, channels_ = 2;
    int16_t pcm_copy_[256];
    btstack_sbc_encoder_state_t sbc_encoder_state_; // 追加
};
