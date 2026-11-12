#include "SampleConsole.h"
#include "PeerPairing.h"
#include "SilenceSource.h"
#include "A2dpSample.h"
int main() {
  sample::Address peer;
  if (!sample::start(peer)) sample::idle();
  static SilenceSource source; // Must outlive player.
  static A2dpSourcePlayer player(source, audioSettings());
  const auto result = player.begin(); // Initializes and powers on shared radio.
  printf("begin=%u\n", unsigned(result));
  if (result != A2dpSourcePlayer::BeginResult::Ok) sample::idle();
  puts("c=one connection attempt, d=disconnect, ?=status; silence for 10 seconds");
  PeerPairing::configure(peer, true); // Existing A2DP policy allows this fixed peer.
  while (true) {
    PeerPairing::poll();
    player.poll();
    switch (getchar_timeout_us(0)) {
      case 'c': connectAudioOnce(player); break; // No automatic retry.
      case 'd': player.disconnect(); break;
      case '?': audioStatus(player); break;
    }
  }
}
