# Public A2DP sink / PCM handoff sample

[日本語](README_jp.md)

Use this example to learn SBC reception and PCM handoff. It does not include an audio-output driver. See [SDK coverage](../../docs/a2dp_sdk_coverage.md) for supported functionality.

`A2dpSinkReceiver` receives SBC and calls your `IAudioSink` from its main-loop `poll()`. The supplied `Counter` consumes only frame counts: no DAC/I2S output, microphone, recording or audible-playback claim. Replace it with your bounded output-buffer/driver implementation.

## Build and use

Use Pico W, an installed Pico SDK, CMake and ARM GCC. Run from the repository root; see the [build guide](../../docs/build_guide.md) for setup.

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S examples/A2DPSinkReceiver -B build/a2dp-sink \
  -DSINK_PEER_ADDRESS=00:11:22:33:44:55
cmake --build build/a2dp-sink --parallel 2
```

Replace the placeholder with the intended source. The default empty peer keeps radio initialization disabled. Building does not flash. After flashing your Pico W:

1. `b`: call `begin()` once. Wait for `?` to show `working=1`.
2. `l`: allow the configured peer to initiate; enables shared-radio connectability, not discoverability. Alternatively `c` requests one outgoing connection to that source. It does not retry automatically.
3. Start playback on the source. `?` reports signaling, stream, negotiated rate and decoded/delivered/dropped frame counters.
4. `d`: disconnect this A2DP signaling session and stop incoming admission. Wait for closure before another `l` or `c`. This does not power the radio off or stop SPP/AVRCP.

The sample explicitly allows SSP confirmation for its configured peer. Runtime uses memory-only keys. Its configuration uses the SDK's normal IO-capability reply path. If an application defines `ENABLE_EXPLICIT_IO_CAPABILITIES_REPLY`, it must install its own matching capability-response handler; `allow_pairing_confirmation` alone does not implement that callback. USB stdout has zero wait timeout and bounded buffering; dropped console output is preferable to blocking radio polling.

## Application contract

Include `A2dpSinkReceiver.h` and link with `picow_target_a2dp_sink(app config_directory)` from `cmake/picow_a2dp_sink.cmake` after `pico_sdk_init()`. Settings require exactly one nonzero six-byte peer. `begin()` returns `Ok`, `InvalidSettings`, `RuntimeAlreadyOwned` or `InitializationFailed`; `Ok` is not connection completion.

Construct one receiver and keep it and the output object alive for the program lifetime. Call all methods on one main loop, never an IRQ/other core. Every `poll()` services the shared runtime then drains copied control/media in callback order, with at most two media packets decoded per call. No application callback runs directly inside a BTstack media callback. Do not reenter receiver mutation APIs from output callbacks.

`IAudioSink::write(const int16_t*, size_t frames, uint32_t rate, uint8_t channels)` receives borrowed interleaved signed-16 PCM. A frame contains one sample per channel; this implementation supplies blocks×subbands frames (16..128) per valid SBC frame, with one or two channels. Copy accepted data before returning. Return accepted frames in `0..frames`. A partial/zero return counts the unaccepted suffix as dropped; it is not retried. A2DP cannot promise remote-source backpressure. A return larger than offered is an invalid result, counted as complete rejection.

`IAudioSink::reset()` must flush queued application PCM on stream/configuration/start/stop boundaries. This prevents old-session audio from leaking across reconnects. The library never records PCM and does not provide an output driver or resampler.

`snapshot()` and `diagnostics()` are read-only copies. Diagnostics distinguish received/processed packets, queue overflow/oversize/discard, sequence gaps/reordering, timestamp/SSRC changes, parse/decode failures, decoded SBC frames, PCM frames, delivered/dropped PCM and invalid consumer returns. `parse_errors` order: None, Short, Rtp, Fragmented, Count, Codec, Bounds, Trailing. Missing packets and discarded PCM are not hidden as success.

## Supported formats and limits

- SBC only: mandatory44.1/48kHz combinations, all four modes, 4/8/12/16 blocks, 4/8 subbands, loudness/SNR, bitpool envelope2..86 with per-format bitrate limits. Whole and bounded fragmented SBC are supported. See the [exact SBC contract](../../docs/sink_sbc_configurations_contract.md), and the [raw-configuration guard](../../docs/sink_raw_configuration_contract.md).
- Eight 1024-byte media slots and sixteen 64-byte control slots. Media overflow is counted; control overflow terminally fails this sink and closes its A2DP session without powering off other profiles.
- Single fixed peer, incoming or one outgoing attempt, no automatic reconnect or outgoing timeout policy. Wrong-peer A2DP is rejected without tearing down unrelated profiles' ACLs.
- Distinct-peer Source/Sink registration and raw AVDTP role gating are under validation. Same-peer registration remains guarded because the installed SDK keeps one configured media stream per peer connection. Destroy/recreate and mode switching are not supported recovery paths yet.
- The CMake helper conservatively reserves a separate 32 KiB stack on the installed RP2040 linker layout and caps the heap below it. It fails if that layout changes. This is not a measured universal peak-stack guarantee; keep consumer callbacks bounded.
- Historical sink-probe hardware results do not automatically validate this new facade. New facade host tests and cross-builds are separate from its still-required hardware acceptance.

## Related examples

[Bounded PCM consumer example with owned copies and partial acceptance](../A2dpSinkPcmConsumer/README.md). Radio is off by default; this is not a playback driver.

For explicit application-measured latency, see [DelayReportExample.h](DelayReportExample.h)
and [the delay-report contract](../../docs/a2dp_sink_pcm.md#explicit-delay-reporting-host-verified-hardware-acceptance-open).
This one-shot helper preserves the measurement's session generation and rounds
microseconds upward to 100 µs units. The frame-counter demo has no output device
or measured render latency and therefore does not send invented delay values.

## Troubleshooting

If `working=1` never appears, check the peer setting and begin result. If connected but frame counts do not grow, check source playback and SBC negotiation. Counter does not produce sound: implement an output driver when needed and monitor partial acceptance and dropped frames.
