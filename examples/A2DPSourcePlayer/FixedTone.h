#pragma once
#include "IAudioSource.h"
// 440 Hz, about -30 dBFS, interleaved stereo. 'samples' counts int16_t values.
class FixedTone : public IAudioSource {
public:
    void configure(uint32_t rate) { step_ = static_cast<uint32_t>((uint64_t{440} << 32) / rate); phase_ = 0; }
    bool hasSamples() const override { return true; }
    size_t read(int16_t* out, size_t samples) override {
        if (!out || (samples & 1u)) return 0;
        static constexpr int16_t wave[] = {
            0,200,392,569,724,851,946,1004,1024,1004,946,851,724,569,392,200,
            0,-200,-392,-569,-724,-851,-946,-1004,-1024,-1004,-946,-851,-724,-569,-392,-200};
        for (size_t i = 0; i < samples; i += 2) {
            out[i] = out[i + 1] = wave[phase_ >> 27]; phase_ += step_;
        }
        return samples;
    }
private:
    uint32_t phase_ = 0, step_ = static_cast<uint32_t>((uint64_t{440} << 32) / 48000);
};
