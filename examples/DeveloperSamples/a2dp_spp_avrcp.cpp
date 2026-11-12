#include "SampleConsole.h"
#include "PeerPairing.h"
#include "SilenceSource.h"
#include "A2dpSample.h"
#include "AvrcpSample.h"
#include "PendingMessage.h"

// Receive two SPP routes first, then explicitly add audio to that live ACL.
int main() {
  sample::Address peer;
  if (!sample::start(peer)) sample::idle();
  static SilenceSource source;
  static A2dpSourcePlayer player(source, audioSettings());
  static spp::Client serial(spp::Config{peer},
    spp::Client::Security{bool(SAMPLE_ALLOW_PAIRING), true});
  static Buttons buttons;
  static picow::avrcp::AvrcpTarget remote;
  remote.setCallback(Buttons::receive, &buttons);
  if (player.begin() != A2dpSourcePlayer::BeginResult::Ok || !serial.begin() ||
      !remote.begin(remoteSettings(peer))) { puts("begin failed; reset"); sample::idle(); }
  std::array<PendingMessage, 2> pending{};
  sample::PairingWindow discovery;
  auto& runtime = BluetoothRuntime::instance();
  const auto closeDiscovery = [&] {
    discovery.close();
    gap_discoverable_control(0); // Do not change shared connectability or pairing policy.
  };
  bool listeners_requested = false, audio_attempted = false;
  puts("l=register 2 SPP listeners; peer connects to these; c=add audio once to live ACL");
  puts("p=60s discovery window after l (build pairing opt-in required)");
  puts("w=greeting on open routes; d=audio disconnect; ?=status. Audio lasts 10 seconds.");
  PeerPairing::configure(peer, true); // Existing A2DP policy allows this fixed peer.
  while (true) {
    PeerPairing::poll();
    player.poll();
    serial.poll();
    remote.poll();
    const auto state = serial.snapshot();
    const int command = getchar_timeout_us(0);
    const bool working = hci_get_state() == HCI_STATE_WORKING;
    const auto link = runtime.linkForPeer(peer);
    if (discovery.shouldClose(time_us_64(), link.active) ||
        (discovery.active && (!working || state.runtime != spp::Runtime::Ready)))
      closeDiscovery();
    if (command == 'p') {
      const bool listening = state.endpoints[0].phase == spp::EndpointPhase::Listening &&
                             state.endpoints[1].phase == spp::EndpointPhase::Listening;
      if (discovery.start(time_us_64(), bool(SAMPLE_ALLOW_PAIRING), working,
                          !listening || link.active || state.runtime != spp::Runtime::Ready))
        gap_discoverable_control(1); // Explicit bounded discovery, not new pairing permission.
    }
    if (command == 'l' && state.runtime == spp::Runtime::Ready && !listeners_requested) {
      listeners_requested = true; // Inspect endpoints below; no implicit retries.
      printf("listeners queued=%u/%u\n",
        serial.listen(0, SAMPLE_CHANNEL_0, spp::Uuid::from16(0x1101)),
        serial.listen(1, SAMPLE_CHANNEL_1, spp::Uuid::from16(0x1101)));
    }
    if (command == 'c' && !audio_attempted) {
      if (!sample::liveAclUsable(link.active, link.epoch, link.handle) ||
          link.peer != peer || runtime.sdpOwned()) {
        puts("Need live configured peer ACL and free SDP; inspect ?, then retry c manually.");
      } else {
        audio_attempted = true;
        printf("audio accepted=%u (not connected/authenticated)\n",
          player.connectOverExistingAcl({link.peer, link.epoch, link.handle}));
      }
    }
    if (command == 'd' || buttons.stop_requested) {
      buttons.stop_requested = false;
      player.disconnect(); // Never powers off the shared radio or disconnects SPP.
    }
    if (command == '?') {
      audioStatus(player);
      remoteStatus(remote, buttons);
      printf("SPP runtime=%u error=%u status=%ld dropped=%llu discovery_window=%u\n", unsigned(state.runtime),
        unsigned(state.error), (long)state.status, (unsigned long long)serial.droppedEvents(), unsigned(discovery.active));
      for (unsigned i = 0; i < 2; ++i)
        printf("slot=%u endpoint=%u endpoint_status=%ld route=%u route_channel=%u gen=%lu error=%u status=%ld accepted=%llu tx=%llu rx=%llu\n",
          i, unsigned(state.endpoints[i].phase), (long)state.endpoints[i].status,
          unsigned(state.routes[i].phase), state.routes[i].channel, (unsigned long)state.routes[i].generation,
          unsigned(state.routes[i].error), (long)state.routes[i].status,
          (unsigned long long)state.routes[i].tx_accepted, (unsigned long long)state.routes[i].tx_sent, (unsigned long long)state.routes[i].rx_read);
    }
    for (unsigned i = 0; i < 2; ++i) {
      if (command == 'w' && state.routes[i].phase == spp::Phase::Open && !pending[i].active)
        pending[i] = {spp::handleOf(state, i), 0, true};
      pending[i].poll(serial, state, i);
      if (state.routes[i].phase == spp::Phase::Open) {
        uint8_t bytes[64];
        serial.read(spp::handleOf(state, i), bytes, sizeof(bytes)); // Replace with app parser.
      }
    }
    remote.setPlayback(UINT32_MAX, UINT32_MAX,
      player.snapshot().stream == a2dp::Stream::Playing ? 1 : 0);
  }
}
