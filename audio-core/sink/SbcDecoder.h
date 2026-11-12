#pragma once
#include "MediaReceiver.h"
#include "../IAudioSink.h"
#include "classic/btstack_sbc_bluedroid.h"
#include <algorithm>
namespace picow::sink {
class Decoder {
public:
  uint64_t attempts = 0, good = 0, failed = 0, pcm_callbacks = 0, pcm_frames = 0, pcm_samples = 0, pcm_invalid = 0, nonzero_samples = 0;
  uint32_t peak = 0; int last_status = 0;
  IAudioSink *output = nullptr;
  uint64_t delivered_frames = 0, dropped_frames = 0, invalid_output_results = 0;
  void configure(Config c) { config_ = c; resetCodec(); }
  void decode(const uint8_t *p, size_t packet_size, const Frame &f) {
    ++attempts;
    if (!p || !ready_ || !config_.valid() || f.offset > packet_size || f.size > packet_size - f.offset || f.size < 4 || f.size > MaxSbcFrameBytes) { ++failed; return; }
    const auto *frame = p + f.offset;
    if (!config_.matches(frame) || config_.frameBytes(frame[2]) != f.size) { ++failed; return; }
    // Use the SDK codec's strict one-frame API, not its streaming wrapper:
    // the latter silently searches for another syncword after a CRC failure.
    // The SDK bit reader prefetches beyond the final encoded byte. Supply
    // fixed zero padding without increasing the semantic input frame length.
    frame_data_.fill(0);
    std::memcpy(frame_data_.data(), frame, f.size);
    const OI_BYTE *input = frame_data_.data();
    OI_UINT32 remaining = OI_UINT32(f.size), pcm_bytes = sizeof(pcm_data_);
    last_status = OI_CODEC_SBC_DecodeFrame(&state_, &input, &remaining, pcm_data_.data(), &pcm_bytes);
    const auto &info = state_.common.frameInfo;
    if (last_status != OI_STATUS_SUCCESS || remaining != 0 || input != frame_data_.data() + f.size ||
        pcm_bytes != config_.samples() * config_.channels() * sizeof(int16_t) || info.nrof_blocks != config_.blocks || info.nrof_subbands != config_.subbands ||
        info.nrof_channels != config_.channels() || info.frequency != config_.rate) {
      ++failed; resetCodec(); return;
    }
    ++good;
    pcm(pcm_data_.data(), config_.samples(), config_.channels(), int(config_.rate), this);
  }
private:
  Config config_{};
  OI_CODEC_SBC_DECODER_CONTEXT state_{};
  std::array<OI_UINT32, (DECODER_DATA_SIZE + 3) / 4> decoder_data_{};
  std::array<int16_t, 256> pcm_data_{};
  std::array<uint8_t, MaxSbcFrameBytes + 16> frame_data_{};
  bool ready_ = false;
  void resetCodec() {
    state_ = {};
    // SDK DecoderReset/Alloc rebinds synthesis storage without clearing it.
    // A new stream (or CRC recovery) must not inherit old PCM filter history.
    decoder_data_.fill(0);
    ready_ = OI_CODEC_SBC_DecoderReset(&state_, decoder_data_.data(), sizeof(decoder_data_), 2, config_.channels(), FALSE) == OI_STATUS_SUCCESS;
  }
  static void pcm(int16_t *data, int frames, int channels, int rate, void *ctx) {
    auto &d = *static_cast<Decoder *>(ctx);
    if (!data || frames != d.config_.samples() || channels != d.config_.channels() || uint32_t(rate) != d.config_.rate) { ++d.pcm_invalid; return; }
    ++d.pcm_callbacks; d.pcm_frames += unsigned(frames); d.pcm_samples += unsigned(frames * channels);
    for (int i = 0; i < frames * channels; ++i) {
      const int32_t sample = data[i];
      d.peak = std::max(d.peak, uint32_t(sample < 0 ? -sample : sample));
      d.nonzero_samples += sample != 0;
    }
    size_t accepted = d.output ? d.output->write(data, size_t(frames), uint32_t(rate), uint8_t(channels)) : 0;
    if (accepted > size_t(frames)) { ++d.invalid_output_results; accepted = 0; }
    d.delivered_frames += accepted;
    d.dropped_frames += size_t(frames) - accepted;
    // Callback storage is borrowed for this call only. No hidden recording.

  }
};
}
