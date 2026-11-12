#include "A2dpSinkReceiver.h"
#include "pico/stdlib.h"
#include <cstdio>

#ifndef SINK_PEER_ADDRESS
#define SINK_PEER_ADDRESS ""
#endif
#include "btstack.h"
// Demonstrates the PCM handoff only. Replace this consumer with a bounded
// application output buffer/driver. This sample does NOT play or record audio.
class Counter : public IAudioSink {
public:
  uint64_t frames = 0, resets = 0;
  size_t write(const int16_t *, size_t n, uint32_t, uint8_t) override {
    frames += n; return n;
  }
  void reset() override { ++resets; }
};
int main() {
  stdio_init_all();
  A2dpSinkReceiver::Settings settings;
  if (!sscanf_bd_addr(SINK_PEER_ADDRESS, settings.peer.data())) {
    puts("Set SINK_PEER_ADDRESS to the intended source; radio remains off.");
    while (true) tight_loop_contents();
  }
  // Explicit sample policy; no persistent bond storage is provided here.
  settings.allow_pairing_confirmation = true;
  static Counter output;
  static A2dpSinkReceiver receiver(output, settings);
  puts("b=begin, l=listen, c=one outgoing attempt, d=disconnect, ?=status");
  while (true) {
    receiver.poll();
    switch (getchar_timeout_us(0)) {
      case 'b': printf("begin=%u\n", unsigned(receiver.begin())); break;
      case 'l': printf("listen=%u\n", receiver.listen()); break;
      case 'c': printf("connect=%u\n", receiver.connect()); break;
      case 'd': receiver.disconnect(); break;
      case '?': {
        const auto s = receiver.snapshot(); const auto d = receiver.diagnostics();
        printf("SBC channels=%u mode=%u blocks=%u subbands=%u allocation=%u bitpool=%u..%u\n",
          unsigned(s.channels),unsigned(s.sbc_channel_mode),unsigned(s.sbc_blocks),unsigned(s.sbc_subbands),
          unsigned(s.sbc_allocation),unsigned(s.sbc_min_bitpool),unsigned(s.sbc_max_bitpool));
        printf("working=%u signaling=%u streaming=%u cid=%u rate=%lu failed=%u decoded=%llu delivered=%llu dropped=%llu\n",
          s.working,s.signaling,s.streaming,s.cid,(unsigned long)s.sample_rate,s.failed,
          (unsigned long long)d.decoded_frames,(unsigned long long)d.delivered_frames,(unsigned long long)d.dropped_frames);
        break;
      }
    }
  }
}
