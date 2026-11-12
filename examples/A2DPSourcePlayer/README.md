# A compact A2DP Source application

[日本語](README_jp.md)

This example connects `IAudioSource → SBCEncoder_BTStack → BTStackA2DPTransport` through `AudioPipeline`, leaving Bluetooth connection procedures to the library. The application does not need BTstack event handling, SDP, SEID, or packet construction.

For your first integration, also read the [developer sample index](../../docs/developer_samples.md) and [DeveloperSamples instructions](../DeveloperSamples/README.md). Build results and hardware validation scope are separate.

```cpp
FixedTone source;
source.configure(48000);
A2dpSourcePlayer::Settings settings;
settings.target_address = "00:11:22:33:44:55";
settings.sample_rate = 48000;
A2dpSourcePlayer player(source, settings);
if (player.begin() != A2dpSourcePlayer::BeginResult::Ok) { /* Report error. */ }
while (true) player.poll();
```

## What to replace

- Source: replace `FixedTone` with your own `IAudioSource`, implementing `hasSamples()` and `read()`.
- PCM: signed 16-bit, interleaved stereo. `samples` counts `int16_t` values across both channels. Supply the requested 1024 values (512 stereo frames). The current pipeline retains partial input and waits until enough is available. It neither pads silence nor resamples.
- Rate: set the source and `settings.sample_rate` to the same 48000 or 44100. Only the selected rate is advertised; it is not silently changed.
- Peer: replace `target_address` with the authorized device's Bluetooth address. Inquiry looks only for that address.
- Normally `play_duration_us=0` means continuous playback. For hardware comparison, this example's `main.cpp` starts explicitly with USB `c`, waits four seconds after OPEN, then plays for three seconds and disconnects. These are not universal library timeouts.
- With `auto_connect=false`, call `connect()` after the radio reaches Working to begin discovery. `disconnect()` explicitly stops; there is no automatic retry. If a live ACL to the same peer already exists, use the explicit opt-in [connectOverExistingAcl contract](../../docs/a2dp_source_lifecycle.md). Ordinary `connect()` remains discovery-based. Acceptance does not mean connection, authentication, or Playing succeeded.

## One owner, one loop

`begin()` registers an A2DP owner with the shared `BluetoothRuntime`. The runtime owns common CYW43/HCI/L2CAP/SDP/power management; SPP and the new AVRCP Target share it. Do not combine independent `cyw43_arch_init()`, BTstack initialization, or the legacy connection plugin. A second A2DP player or conflicting owner claim is rejected as `RuntimeAlreadyOwned`.

Keep the player and source alive for the program lifetime. Destruction/reinitialization are unsupported. Use one main loop, polling all active profiles every iteration. Do not mix IRQ, another core, or background polling.

SPP and AVRCP Target can share the runtime. HFP is [unsupported](../../docs/HFP_UNSUPPORTED_REASON_en.md); Wi-Fi coexistence is unverified.

## State and history

`snapshot()` is a pure copy exposing power/inquiry/signaling/stream/credit, CID/SEID, codec, cumulative packets/samples, error/status, and rejection counts.

`snapshot().error/status` (connection core) differs from `diagnostics().last_send_status` (latest SDK send result). The current basic transport classifies `0x57 = BTSTACK_ACL_BUFFERS_FULL` as `Deferred`, retaining the unsent encoded batch. A nonzero status is not automatically a terminal core failure, but other nonzero statuses are not universally acceptable. Observe pending resolution, packet growth, core errors, and binding together.

`historySize()` and `history(i)` return bounded history oldest first. Entries contain the event, before/after states, generated actions, and sequence. Asynchronous completion/failure appears as later events. A zero API return value is distinct from peer acceptance. Check `overwrittenHistory()` and `droppedEvents()` for missing observations.

Each poll reads new history sequences and emits important transitions as CONTROL lines: power, inquiry, negotiation, start, stop, disconnect, failures, and actions. STATE totals cover high-frequency media sends. `control_lost` counts history missed by the reader; `trace_lost` counts USB queue losses. CONTROL `result_status` is the post-transition state value, not advance confirmation that an asynchronous action succeeded.

Logging never waits for USB attachment. `?` shows state, `h` history, `d` stops, and `c` requests connection. The bounded USB queue reports overflow in `trace_lost` rather than accumulating unlimited logs.

## Build

```sh
PICO_SDK_PATH=/path/to/pico-sdk cmake -S examples/A2DPSourcePlayer \
  -B build/a2dp-player -DPLAYER_TARGET_ADDRESS=00:11:22:33:44:55
cmake --build build/a2dp-player -j4
```

Replace the SDK path and peer with your own authorized setup. This uses SDK 2.1.1's `pico_btstack_sbc_encoder`. Nonblocking USB CDC coexists with the SDK reset interface, retaining the normal picotool reflashing path. UF2 generation and listening on hardware are separate validations.

## Run and inspect state

Run the build commands from the repository root and replace the SDK path and illustrative peer address. You need Pico W, Pico SDK, CMake and ARM GCC; see the [build guide](../../docs/build_guide.md) for setup and UF2 deployment.

After deployment/startup, use the USB CDC console: send `?`, wait for power=2 (Working), then send `c` once. The sample waits four seconds after OPEN, sends its tone for three seconds and disconnects. Check Playing and growing packet counts with `?`; use `d` to stop early and `h` to inspect transitions. Start with low volume on the receiving device.

If connection fails, check the configured peer, its connection readiness and error/status. Repeated `c` is not a recovery policy. Inspect `trace_lost` / `control_lost` for missing diagnostics and query `?` again for current values.

## Use from another project

Include the helper after `pico_sdk_init()` and apply it once to your application target. Do not link the legacy `PicoWBluetooth` target, background architecture, or connection plugin.

```cmake
# After pico_sdk_import.cmake, before pico_sdk_init():
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
pico_sdk_init()
add_executable(my_player main.cpp)
include(/path/to/PicoW_Bluetooth_lib/cmake/picow_a2dp_source.cmake)
picow_target_a2dp_source(my_player ${CMAKE_CURRENT_SOURCE_DIR})
pico_add_extra_outputs(my_player)
```

For standalone A2DP, copy this example's `btstack_config.h` into the app directory. For SPP/AVRCP combinations, do not reuse it unchanged: check [shared sample settings](../DeveloperSamples/btstack_config.h) and the [SPP integration contract](../../docs/spp_sdk_integration.md) for HCI capacity and profile resources.

The helper collects audio sources and polling-style SDK dependencies. USB is the app's responsibility; ordinary SDK stdio USB is fine. Only if reusing this example's custom CDC must you also carry over `usb_descriptors.c`, `tusb_config.h`, and CMake reset-interface settings.

Keep the source/player alive in the main loop. The shortest code connects automatically and plays continuously; for short trials use this example's `auto_connect=false` and `play_duration_us=3000000`.

## Reading order and verification scope

1. `main.cpp`: source, settings, begin, poll.
2. `audio-core/A2dpSourcePlayer.cpp`: BTstack notification normalization and action execution.
3. `audio-core/connection/A2dpConnection.h`: State/Event/Rules/Engine.
4. [Connection transitions](../../docs/connection_state_transitions.md): observations mapped to rules.

Common stack initialization belongs to the runtime. A2DP-specific initialization/SDP registration map to `execute(Initialize)`, packet handling to `normalize()`, playback timing to `Rules::apply(Tick)`, and can-send handling to credit rules and `execute(PumpAudioOnce)`. The current pipeline also follows the [coexistence send-deferral contract](../../docs/a2dp_spp_coexistence.md), retaining unsent encoded batches and partial PCM.

This example does not establish audible output, device compatibility or long-duration operation. [Validation scope](../../docs/validation.md) are separate development records.
