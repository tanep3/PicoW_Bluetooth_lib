#ifndef A2DP_AUDIO_SOURCE_H
#define A2DP_AUDIO_SOURCE_H

#include "IAudioSource.h"
#include <vector>
#include <cstddef>
#include <pico/mutex.h>

// A simple circular buffer for audio data
template<typename T>
class CircularBuffer {
public:
    explicit CircularBuffer(size_t capacity);
    size_t write(const T* data, size_t items);
    size_t read(T* data, size_t items);
    size_t size() const;
    size_t capacity() const;

private:
    std::vector<T> buffer_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t size_ = 0;
};

class A2DPAudioSource : public IAudioSource {
public:
    explicit A2DPAudioSource(size_t buffer_size = 4096);

    // --- IAudioSource implementation (Pull interface) ---
    size_t read(int16_t* data, size_t samples) override;
    bool hasSamples() const override;

    // --- Public method for BTstack to push data (Push interface) ---
    void write(const int16_t* data, size_t samples);

private:
    CircularBuffer<int16_t> pcm_buffer_;
    mutable mutex_t mutex_;
};

#endif // A2DP_AUDIO_SOURCE_H
