# Start with small public-API examples

[日本語](README_jp.md)

These standalone examples teach the public APIs; each `.cpp` builds a separate firmware. A2DP sends silent PCM so you can study connection and control flow without an audio-output implementation. Hardware observations are limited in scope and do not establish audio quality or general interoperability.

## Choose an example

| Target / source | What it teaches |
|---|---|
| `sample_a2dp_source` / `a2dp_source.cpp` | `IAudioSource` → settings → begin → poll; manual connection, ten seconds of silent PCM, disconnect, state |
| `sample_avrcp_target` / `avrcp_target.cpp` | Receive-only Target; operation IDs and press/release; application handling; terminal stop |
| `sample_spp_client` / `spp_client.cpp` | One SPP route; channel/UUID connection, candidate selection, writes/reads |
| `sample_spp_two_routes` / `spp_two_routes.cpp` | Two routes on different RFCOMM channels to one peer; generation-aware sending |
| `sample_a2dp_avrcp` / `a2dp_avrcp.cpp` | Shared runtime; application mapping from AVRCP STOP to A2DP disconnect |
| `sample_a2dp_spp_avrcp` / `a2dp_spp_avrcp.cpp` | Two SPP listeners, A2DP over an existing ACL, receive-only AVRCP, explicit once-only audio start |
| `sample_spp_reconnect` / `spp_reconnect.cpp` | Explicit start, at most three attempts, about five seconds after timeout-disconnect enqueue, ten-second deadline from connect issue; no infinite retry |

SPP's shared implementation is `SppSample.h`; small entry points select route count/retry policy. Read one-route logic first, then two-route/retry branches. Protocol framing, priorities, and echo behavior are not imposed by the library.

## Build

For SDK setup, read the [build guide](../../docs/build_guide.md). The short version below assumes Pico W, Pico SDK, CMake, and ARM GCC. From the repository root:

```sh
export PICO_SDK_PATH="$HOME/pico/pico-sdk" # Use your SDK location.
cmake -S examples/DeveloperSamples -B /tmp/picow-samples \
  -DSAMPLE_PEER=12:34:56:78:9A:BC \
  -DSAMPLE_CHANNEL_0=1 -DSAMPLE_CHANNEL_1=2
cmake --build /tmp/picow-samples -j2
```

Replace the illustrative address with an authorized peer's actual Bluetooth address, and channels with its services. Channels 1 and 2 do not imply reserved services. Values must be 1..30 and distinct for two routes. This does not connect one `spp::Client` to two different devices.

`SAMPLE_PEER` defaults to empty. Missing, invalid, all-zero, or all-FF addresses prevent begin. Builds still succeed, but radio does not start. Outputs are `/tmp/picow-samples/sample_*.uf2`. Neither these commands nor the check script flashes hardware. Flash **one** selected UF2 for use.

## Console and first use

Pico USB CDC serial is default. No begin/radio power-on occurs until terminal command `b`. Startup output may be lost before terminal attachment; send `b` once, then observe with `?`. USB stdout timeout is zero; diagnostics can be dropped when disconnected or congested, so logs are not complete records.

For UART, reconfigure with `-DSAMPLE_USB_CONSOLE=OFF`: UART0, GP0=TX, GP1=RX, shared GND, 115200 baud, 3.3 V TTL. Never directly connect RS-232 voltage. UART printf waits for transmission, so avoid repeated `?`. Both variants read only one character per loop. For a real-time product, use low-rate status or a bounded trace queue.

- A2DP: `b` → `?` until power=2 (Working) → `c` → `?`. An early `c` reports startup pending; manually retry later. `d` disconnects. `c` is one manual attempt, not automatic retry. It can be used after normal disconnect, but a non-None A2DP error cannot recover with the same owner and requires reset.
- Standalone AVRCP: build with `SAMPLE_ALLOW_PAIRING=ON`; `b` → Ready → `p`; then use the specified Controller to discover/pair/connect AVRCP. `?` displays latest event kind/operation/pressed/result. `x` is terminal stop; reset to restart.
- SPP: `b` → `?` until Ready → `c` → `?` until Open → `w`. `w` sends one short greeting; verify reception on the peer.
- SPP `u` requests route 0 using standard SPP UUID `0x1101`. If multiple candidates yield Selecting, inspect the list with `?`; press `s` only if candidate 0 is the intended channel. A product needing other indexes must provide a UI and `select(handle,index)`.
- SPP `l` registers local listening services. Connect from the peer to the configured local channels. Inspect endpoint Listening/Failed/status separately from route Open/error. The listen bool is queue acceptance only. Endpoint and incoming route indexes are not fixed pairs: incoming connections take free routes; use snapshot `route.channel` to identify the service. `d` does not remove listeners. There is no public stop-listening API, so reset this example to remove them. Simultaneous outgoing/listening requests can conflict; learn one direction first.
- `d` disconnects/cancels SPP connection/discovery and disables automatic retry mode. `r` exists only in the reconnect example.

## Explicit pairing permission

Standalone SPP/AVRCP deny automatic confirmation by default. For initial SSP pairing, verify the peer and rebuild with `-DSAMPLE_ALLOW_PAIRING=ON`. This permits confirmation for **only that fixed peer**. SPP uses `Client::Security{true,true}`; standalone AVRCP uses `p`, the application HCI handler in `PeerPairing.h`, and runtime policy. All examples' `PeerPairing.h` handles explicit IO-capability requests, rejecting peers other than the authorized fixed address. Confirmation requests are checked against shared runtime profile policy; authentication requests are not disabled.

Make the peer connectable and, when needed, pairable. AVRCP is receive-only: this example neither searches for the Controller nor forces it to initiate AVRCP. After WORKING, the standalone app enables connectability. `b` alone does not permit discoverability/pairing; explicit `p` opens a maximum 60-second window. Expiry, AVRCP connection, or `x` closes discoverability and this owner's pairing permission; `x` also disables connectability. Repeated `p` during a window does not extend it. Controllers that can connect by address may use the displayed Pico address. Some devices cannot connect standalone, depending on their implementation/connectivity.

**The existing A2DP facade has a different contract.** `A2dpSourcePlayer::begin()` registers SSP confirmation permission for its configured peer. `SAMPLE_ALLOW_PAIRING=OFF` does not disable this. Understand it before `b` / `c` in A2DP examples; coexistence affects shared policy for the same peer.

Link keys are RAM-only and do not persist across reboot. If only the peer retains an old bond, remove the relevant previous registration there and pair again. Legacy PIN pairing is unsupported. There is no indiscriminate confirmation or security-level downgrade fallback.

## States, errors, and lifetimes

- Create one process-lifetime owner per profile. Call begin/poll/APIs from one main loop, never IRQ, a second core, or another thread. PCM sources and callback contexts must outlive their owner; examples use static objects.
- Leave CYW43/HCI/L2CAP/SDP initialization to `BluetoothRuntime`. Do not mix separate `cyw43_arch_init()`, run loops, legacy plugins, or background-style legacy library targets. Each facade's `poll()` advances runtime; call all facades every iteration.
- `begin()==true`, `connect()==true`, and `tryWrite().bytes>0` do not prove completed communication. SPP bools mean queued acceptance. Distinguish Ready/Open, error/status, tx_accepted/tx_sent/tx_discarded, and droppedEvents. A2DP begin success is not connection/playback success; inspect Playing and packet counts.
- Obtain SPP `{slot,generation}` handles from snapshots; never reuse old handles after reconnect. Discard pending application messages on disconnect/generation change. Advance offsets only by accepted bytes; retry WouldBlock remainders after the next poll. Acceptance is not peer-application acknowledgment.
- SPP reads at most 64 bytes/route/loop. These examples consume data and observe counters; replace that with your processing. RFCOMM is a byte stream: write/read boundaries need not match. Implement length prefixes/delimiters, ACKs, retransmission, and priorities as your application needs.
- AVRCP callbacks store intent and the latest event only. Later events overwrite earlier ones before `?`. `pressed=false` means release. Operations include `0x44` PLAY, `0x45` STOP, `0x46` PAUSE. Products tracking held buttons should clear them on Disconnected/Reset without fabricating release events.
- Combined examples explicitly map STOP presses to A2DP disconnect. They do not automatically forward PLAY/PAUSE or invent facade pause/seek APIs. Playback status is playing only while Playing; length/position remain unknown.
- A2DP uses signed int16, interleaved stereo, 48000 Hz. `IAudioSource::read` counts int16 samples, not stereo frames. Silence is intentional. Change source and settings together for another sample rate; there is no resampler.

### Main numeric state values

Diagnostics use public enums. A2DP power: 0=Off, 1=Starting, 2=Working. Signaling: 0=Idle, 1=Connecting, 2=Connected. Stream: 0=Idle, 1=Configuring, 2=Open, 3=StartRequested, 4=Playing. SPP runtime: 0=Stopped, 1=Starting, 2=Ready, 3=Failed. Route phase: 0=Closed, 1=Resolving, 2=Selecting, 3=Connecting, 4=Open, 5=Closing. Endpoint phase: 0=Disabled, 1=Registering, 2=Listening, 3=Failed. error=0 means None. See enums in `audio-core/connection/A2dpConnection.h`, `spp-core/SppCore.h`, `avrcp-core/AvrcpTargetCore.h`, and each owner's `history()`.

## Bounded reconnect

`r` allows at most three queued attempts per boot, a ten-second connection/session deadline from issuing connect, and about five seconds of cooldown after enqueuing timeout disconnect. These are not ten seconds after Open or five seconds after physical socket close. This **demonstration policy** requests disconnect at the deadline; it is not a recommended indefinite-operation policy. Queue rejection consumes an attempt. Repeated `r` cannot replenish the budget. If asynchronous disconnect never completes and Closing remains, no next connection starts. Failed runtime requires reset, not repeated begin on the same owner.

The peer needs an RFCOMM server and authentication readiness. There is no firmware watchdog: `d` and exhausting attempts do not power off the radio.

## Progress to A2DP + SPP + AVRCP

Start simple coexistence with `a2dp_avrcp.cpp`. Add one `spp::Client` on the same runtime and poll every profile each loop, for example A2DP → SPP → AVRCP. Never block one profile's polling while waiting for another. Multiple SPP routes are slots 0/1 of the same client, as in `spp_two_routes`.

The readable three-profile entry point is `a2dp_spp_avrcp.cpp`: `b` → SPP Ready → `l` → both endpoints Listening → `p` if initial discovery is needed → peer connects to both channels → `?` verifies Open → `c`. It obtains the current same-peer ACL `{peer, epoch, handle}` and passes it once to `connectOverExistingAcl`. Acceptance is not connection/authentication success; inspect Playing and error/status. ACL loss/replacement or radio OFF makes this opt-in path terminal and requires reset. `c` is not automatically resent. `d` / AVRCP STOP disconnect audio only, retaining SPP and radio. `w` sends a greeting once to each Open route. Keep the all-profile main loop running.

Here `p` explicitly enables discoverability only. It opens at most 60 seconds only when `SAMPLE_ALLOW_PAIRING=ON`, radio Working, both listeners Ready, and no fixed-peer ACL is connected. Repetition does not extend it. A live fixed-peer ACL, expiry, or radio/SPP stop/failure closes it. Check `discovery_window` in `?`. It does not change shared connectability or A2DP/SPP fixed-peer pairing policy. A peer able to connect directly to the known address does not need `p`.

For the three-profile integration, read [a2dp_spp_avrcp.cpp](a2dp_spp_avrcp.cpp) and [CMakeLists.txt](CMakeLists.txt). Shared SDP has one active discovery slot, so avoid simultaneous SPP UUID and A2DP discovery. A2DP on an ACL already held by SPP differs from normal inquiry connection. The combined example uses `connectOverExistingAcl` and `a2dp::AclBinding`: handle 0 is valid; verify active, nonzero epoch, and matching peer. Repeated ordinary `connect()` is not a workaround.

For received PCM, use the [A2DP Sink example](../A2DPSinkReceiver/README.md). It requires your own I2S/DAC output implementation for audible playback. HFP/FastStream are outside these learning APIs.

## Offline checks

```sh
PICO_SDK_PATH=/path/to/pico-sdk examples/DeveloperSamples/check.sh
```

Host checks cover peer-parser rejection, retry budget/cooldown, SPP generation changes, partial writes, preventing duplicate bytes after WouldBlock, and discarding remaining data on disconnect/generation change. Then all seven targets build against the actual SDK for ARM. No radio, flash, pairing, microphone, or speaker operation occurs. The script defaults to no peer. Real security negotiation, audio quality, reachability, and reconnect behavior need separate checks.

## Troubleshooting diagnostics

USB stdout timeout remains zero so console output does not stall radio polling. CDC TX buffer grows from the SDK default 64 to a fixed 1024 bytes (+960 bytes per USB target), enough for one combined `?` status group. Slow readers may still lose output; query `?` again. UART buffer settings are unchanged.

In three-profile route status, `accepted` is cumulative bytes accepted into the library TX queue; `tx` is cumulative SDK-send-success bytes. Acceptance does not prove peer reception. The greeting helper retains remaining data on WouldBlock and on Busy caused by pending callbacks, retrying after the next main-loop poll. Generation changes, disconnection, or fatal errors discard the remainder.

STOP requests only audio disconnection at the application level. AVRCP may also disconnect as a consequence on hardware. Do not promise AVRCP retention; verify continued SPP separately.

[Validation scope](../../docs/validation.md) are kept in the development record.
