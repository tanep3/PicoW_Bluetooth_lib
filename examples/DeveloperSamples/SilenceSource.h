#pragma once
#include "IAudioSource.h"
#include <algorithm>
// int16 interleaved stereo samples, not stereo frame count. No resampling.
// Replace this class with your PCM source. Silence avoids an unexpected tone.
class SilenceSource final : public IAudioSource {
public:
  bool hasSamples() const override { return true; }
  size_t read(int16_t* destination, size_t samples) override {
    std::fill_n(destination, samples, int16_t{0});
    return samples;
  }
};
