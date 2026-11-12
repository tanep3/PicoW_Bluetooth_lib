#include "SampleConsole.h"
#include "PeerPairing.h"
#include "SilenceSource.h"
#include "A2dpSample.h"
#include "AvrcpSample.h"
int main() {
  sample::Address peer;
  if (!sample::start(peer)) sample::idle();
  static SilenceSource source;
  static A2dpSourcePlayer player(source, audioSettings());
  static Buttons buttons;
  static picow::avrcp::AvrcpTarget remote;
  remote.setCallback(Buttons::receive, &buttons);
  if (player.begin() != A2dpSourcePlayer::BeginResult::Ok ||
      !remote.begin(remoteSettings(peer))) { puts("begin failed; reset required"); sample::idle(); }
  puts("c=connect A2DP; d=disconnect audio; ?=status. AVRCP STOP disconnects audio.");
  PeerPairing::configure(peer, true); // Existing A2DP policy allows this fixed peer.
  while (true) {
    PeerPairing::poll();
    player.poll();
    remote.poll(); // Both owners share one runtime, all on the same main loop.
    if (buttons.stop_requested) {
      buttons.stop_requested = false;
      player.disconnect(); // Explicit application mapping, not AVRCP's default.
    }
    remote.setPlayback(UINT32_MAX, UINT32_MAX,
      player.snapshot().stream == a2dp::Stream::Playing ? 1 : 0);
    switch (getchar_timeout_us(0)) {
      case 'c': connectAudioOnce(player); break;
      case 'd': player.disconnect(); break;
      case '?': audioStatus(player); remoteStatus(remote, buttons); break;
    }
  }
}
