#pragma once
#include <cstddef>
#include <cstdint>

// PCM consumer for A2dpSinkReceiver. Calls occur synchronously in its main-loop
// poll() (and reset() also from disconnect()), never from IRQ or directly inside
// BTstack's media callback. Keep this
// object alive for the program lifetime; do not reenter receiver APIs here.
class IAudioSink {
public:
  virtual ~IAudioSink() = default;
  // Borrowed signed int16 interleaved PCM. frames is per-channel frame count,
  // not bytes/int16 element count. Copy accepted frames before returning.
  // Return 0..frames. Any unaccepted suffix is counted as dropped, never retried:
  // A2DP reception cannot promise backpressure to the remote source.
  virtual size_t write(const int16_t *pcm, size_t frames, uint32_t sample_rate,
                       uint8_t channels) = 0;
  // Flush application-owned queued PCM on format/session/start/stop boundaries,
  // so audio from a previous stream cannot leak into a new stream.
  virtual void reset() = 0;
};
