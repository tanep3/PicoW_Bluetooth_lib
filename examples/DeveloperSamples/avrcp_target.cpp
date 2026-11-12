#include "SampleConsole.h"
#include "AvrcpSample.h"
#include "PeerPairing.h"
int main() {
  sample::Address peer;
  if (!sample::start(peer)) sample::idle();
  static Buttons buttons;
  static picow::avrcp::AvrcpTarget remote;
  remote.setCallback(Buttons::receive, &buttons);
  remote.setPlayback(UINT32_MAX, UINT32_MAX, 0);
  if (!remote.begin(remoteSettings(peer))) { puts("AVRCP begin failed"); sample::idle(); }
  auto& runtime = BluetoothRuntime::instance();
  PeerPairing::configure(peer, false); // b alone never opens a pairing window.
  runtime.setPairingPolicy(BluetoothRuntime::Profile::Avrcp, peer, false);
  bool incoming_enabled = false;
  sample::PairingWindow window;
  const auto closeWindow = [&] {
    window.close();
    gap_discoverable_control(0);
    PeerPairing::disable();
    runtime.setPairingPolicy(BluetoothRuntime::Profile::Avrcp, peer, false);
  };
  puts("p=60s fixed-peer pairing window (build opt-in required); ?=status; x=terminal stop");
  while (true) {
    PeerPairing::poll();
    remote.poll();
    const bool working = hci_get_state() == HCI_STATE_WORKING;
    if (!remote.status().stopped && !incoming_enabled && working) {
      gap_connectable_control(1); // Standalone app owns these global settings.
      incoming_enabled = true;
      bd_addr_t local;
      gap_local_bd_addr(local);
      printf("Ready: Pico %02X:%02X:%02X:%02X:%02X:%02X; p opens discovery/pairing\n",
        local[0], local[1], local[2], local[3], local[4], local[5]);
    }
    if (window.shouldClose(time_us_64(), remote.core().status().active != 0) ||
        (window.active && (remote.status().stopped || !working)))
      closeWindow();
    if (remote.status().stopped && incoming_enabled) {
      gap_connectable_control(0);
      incoming_enabled = false;
    }
    buttons.stop_requested = false; // No player in this example.
    switch (getchar_timeout_us(0)) {
      case 'p':
        if (window.start(time_us_64(), bool(SAMPLE_ALLOW_PAIRING), working,
                         remote.status().stopped || remote.core().status().active != 0)) {
          PeerPairing::configure(peer, true);
          runtime.setPairingPolicy(BluetoothRuntime::Profile::Avrcp, peer, true);
          gap_discoverable_control(1);
          puts("Pairing/discovery open for configured peer, at most 60 seconds.");
        } else puts("p denied: opt-in off, not Working, stopped, connected, or already open.");
        break;
      case '?':
        remoteStatus(remote, buttons);
        printf("pairing_window=%u connectable=%u\n", window.active, incoming_enabled);
        break;
      case 'x':
        closeWindow();
        gap_connectable_control(0);
        incoming_enabled = false;
        remote.stop(); // Terminal, does not claim to release the runtime owner.
        break;
    }
  }
}
