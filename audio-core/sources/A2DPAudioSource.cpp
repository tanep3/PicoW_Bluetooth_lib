#include "sources/A2DPAudioSource.h"
#include <algorithm>

// --- CircularBuffer implementation ---
template<typename T>
CircularBuffer<T>::CircularBuffer(size_t capacity)
    : buffer_(capacity) {}

template<typename T>
size_t CircularBuffer<T>::write(const T* data, size_t items) {
    size_t written = 0;
    for (size_t i = 0; i < items; ++i) {
        if (size_ == buffer_.size()) break; // Buffer is full
        buffer_[tail_] = data[i];
        tail_ = (tail_ + 1) % buffer_.size();
        size_++;
        written++;
    }
    return written;
}

template<typename T>
size_t CircularBuffer<T>::read(T* data, size_t items) {
    size_t read_items = 0;
    for (size_t i = 0; i < items; ++i) {
        if (size_ == 0) break; // Buffer is empty
        data[i] = buffer_[head_];
        head_ = (head_ + 1) % buffer_.size();
        size_--;
        read_items++;
    }
    return read_items;
}

template<typename T>
size_t CircularBuffer<T>::size() const {
    return size_;
}

template<typename T>
size_t CircularBuffer<T>::capacity() const {
    return buffer_.size();
}

// --- A2DPAudioSource implementation ---
A2DPAudioSource::A2DPAudioSource(size_t buffer_size)
    : pcm_buffer_(buffer_size) {
    mutex_init(&mutex_);
}

size_t A2DPAudioSource::read(int16_t* data, size_t samples) {
    mutex_enter_blocking(&mutex_);
    size_t n = pcm_buffer_.read(data, samples);
    mutex_exit(&mutex_);
    return n;
}

void A2DPAudioSource::write(const int16_t* data, size_t samples) {
    mutex_enter_blocking(&mutex_);
    pcm_buffer_.write(data, samples);
    mutex_exit(&mutex_);
}

bool A2DPAudioSource::hasSamples() const {
    mutex_enter_blocking(&mutex_);
    bool available = pcm_buffer_.size() > 0;
    mutex_exit(&mutex_);
    return available;
}
