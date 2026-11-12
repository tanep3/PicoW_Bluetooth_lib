#pragma once

#include "IAudioSink.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace sink_pcm_sample {

// SDK-independent, single-main-loop FIFO. Not safe for concurrent IRQ/core use.
// write/reset are callbacks: no allocation, waiting, I/O or receiver API calls.
template <size_t CapacityFrames> class BoundedPcmSink final : public IAudioSink {
public:
    static constexpr size_t capacity_frames = CapacityFrames;
    static constexpr uint8_t max_channels = 2;
    static_assert(CapacityFrames > 0, "PCM FIFO must have positive capacity");
    static_assert(CapacityFrames <= std::numeric_limits<size_t>::max() / max_channels,
                  "PCM FIFO index arithmetic must fit size_t");

    struct Statistics {
        uint64_t accepted_frames = 0;
        uint64_t rejected_frames = 0;
        uint64_t drained_frames = 0;
        uint64_t flushed_frames = 0;
        uint64_t reset_calls = 0;
        uint64_t format_changes = 0;
    };
    struct PullResult {
        size_t frames = 0;
        uint32_t sample_rate = 0;
        uint8_t channels = 0;
        uint32_t epoch = 0;
    };

    size_t write(const int16_t *pcm, size_t frames, uint32_t sample_rate,
                 uint8_t channels) override {
        if (!frames) return 0; // Empty input never changes format or counters.
        if (!pcm || !sample_rate || channels == 0 || channels > max_channels) {
            stats_.rejected_frames += frames;
            return 0; // Preserve valid queued PCM on malformed input.
        }
        if (sample_rate_ && (sample_rate != sample_rate_ || channels != channels_)) {
            discardQueued();
            ++epoch_;
            ++stats_.format_changes;
        }
        sample_rate_ = sample_rate;
        channels_ = channels;
        const size_t room = CapacityFrames - count_;
        const size_t accepted = frames < room ? frames : room;
        for (size_t frame = 0; frame < accepted; ++frame) {
            auto &slot = pcm_[(head_ + count_ + frame) % CapacityFrames];
            for (uint8_t channel = 0; channel < channels; ++channel)
                slot[channel] = pcm[frame * channels + channel];
        }
        count_ += accepted;
        stats_.accepted_frames += accepted;
        stats_.rejected_frames += frames - accepted;
        // The receiver counts this unaccepted suffix as dropped, never retries.
        // Returning fewer frames does NOT pause the remote A2DP source.
        return accepted;
    }

    void reset() override {
        discardQueued();
        sample_rate_ = 0;
        channels_ = 0;
        ++epoch_;
        ++stats_.reset_calls;
    }

    // destination_samples is int16_t element capacity, not bytes or frames.
    // Copies only complete interleaved frames into caller-owned storage. The
    // returned format/epoch describes that copy, including on an empty pull.
    PullResult pull(int16_t *destination, size_t destination_samples) {
        PullResult result{0, sample_rate_, channels_, epoch_};
        if (!destination || !channels_) return result;
        const size_t room = destination_samples / channels_;
        result.frames = count_ < room ? count_ : room;
        for (size_t frame = 0; frame < result.frames; ++frame) {
            const auto &slot = pcm_[(head_ + frame) % CapacityFrames];
            for (uint8_t channel = 0; channel < channels_; ++channel)
                destination[frame * channels_ + channel] = slot[channel];
        }
        head_ = (head_ + result.frames) % CapacityFrames;
        count_ -= result.frames;
        stats_.drained_frames += result.frames;
        return result;
    }

    size_t queuedFrames() const { return count_; }
    uint32_t sampleRate() const { return sample_rate_; }
    uint8_t channels() const { return channels_; }
    uint32_t epoch() const { return epoch_; }
    Statistics statistics() const { return stats_; }

private:
    void discardQueued() {
        stats_.flushed_frames += count_;
        head_ = count_ = 0;
    }
    std::array<std::array<int16_t, max_channels>, CapacityFrames> pcm_{};
    size_t head_ = 0, count_ = 0;
    uint32_t sample_rate_ = 0, epoch_ = 0;
    uint8_t channels_ = 0;
    Statistics stats_{};
};

} // namespace sink_pcm_sample
