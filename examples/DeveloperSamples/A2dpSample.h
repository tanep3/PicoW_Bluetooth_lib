#pragma once
#include "A2dpSourcePlayer.h"
#include <cstdio>
inline A2dpSourcePlayer::Settings audioSettings() {
  A2dpSourcePlayer::Settings settings;
  settings.target_address = SAMPLE_PEER;
  settings.sample_rate = 48000;
  settings.auto_connect = false;
  settings.play_duration_us = 10000000; // Ten seconds after stream starts.
  return settings;
}
inline void audioStatus(A2dpSourcePlayer& player) {
  const auto s = player.snapshot();
  const auto d = player.diagnostics();
  printf("audio power=%u inquiry=%u signaling=%u stream=%u error=%u status=%d packets=%lu drops=%llu\n",
    unsigned(s.power), unsigned(s.inquiry), unsigned(s.signaling), unsigned(s.stream),
    unsigned(s.error), s.status, (unsigned long)d.sent_packets,
    (unsigned long long)player.droppedEvents());
}

inline void connectAudioOnce(A2dpSourcePlayer& player) {
  const auto s = player.snapshot();
  if (s.power != a2dp::Power::Working) { puts("Wait for power=2 (Working), then press c again."); return; }
  if (s.inquiry != a2dp::Inquiry::Idle || s.signaling != a2dp::Signaling::Idle) {
    puts("Audio connection/discovery already active; inspect ? or disconnect with d."); return;
  }
  if (s.error != a2dp::Error::None) {
    audioStatus(player);
    puts("Audio error is terminal for this owner; reset required.");
    return;
  }
  player.connect();
  puts("One discovery request submitted; inspect ? for asynchronous result.");
}
