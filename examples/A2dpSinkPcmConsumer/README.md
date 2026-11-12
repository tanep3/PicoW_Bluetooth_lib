# A2DP Sink: bounded PCM consumer

[日本語](README_jp.md) / [Sink API overview](../A2DPSinkReceiver/README.md)

Send Bluetooth audio from a phone or computer to Pico W and **check received frames and peak magnitude in a USB terminal**. This sample copies PCM received through the public `A2dpSinkReceiver` / `IAudioSink` API into a fixed FIFO, then processes it in the main loop.

**There is no speaker output; silence at the Pico is normal.** Start by checking reception without recording. The optional section below saves up to five seconds to a WAV file on your computer. The sample has no microphone input or AVRCP controls.

<a id="first-use"></a>
## First use: receive without recording

You need a Pico W, a USB data cable, a USB serial terminal, and a phone or computer that can send Bluetooth Classic SBC audio. Follow the [build prerequisites](../../docs/build_guide.md#prerequisites) and use the full repository checkout.

### Build and flash

Find the [source's Classic Bluetooth address](../../docs/build_guide.md#peer-address) and set the Bash variable `SOURCE_BT_ADDRESS` to it. **Do not use the Pico address or a Wi-Fi MAC address.** Set `PICO_SDK_PATH` to your prepared SDK directory and run these commands from the repository root:

```sh
: "${PICO_SDK_PATH:?Set this to your Pico SDK directory}"
: "${SOURCE_BT_ADDRESS:?Set this to the verified source Bluetooth address}"
cmake -S examples/A2dpSinkPcmConsumer -B .work/builds/sink-pcm-first-use \
  -DSINK_PCM_ENABLE_RADIO=ON -DSINK_PCM_USB_CAPTURE=OFF \
  -DSINK_PCM_ALLOW_PAIRING=ON -DSINK_PCM_DISCOVERABLE_LISTEN=ON \
  -DSINK_PCM_PEER="$SOURCE_BT_ADDRESS"
cmake --build .work/builds/sink-pcm-first-use --target a2dp_sink_pcm_consumer --parallel 2
```

[Flash via BOOTSEL and open the USB terminal](../../docs/build_guide.md#flash) using `.work/builds/sink-pcm-first-use/a2dp_sink_pcm_consumer.uf2`. If you want to keep the current firmware, first verify its backup and restoration procedure. This build **starts radio initialization at boot**. It uses neither `b` nor `START`.

### Connect and check reception

Commands are lowercase single characters, except `?` for status. Send them one at a time and check the result.

1. Send `?` and check `working=1 failed=0`. This also works if you missed the one-time `Ready` message.
2. Send `i` to check Pico's Bluetooth address. Its Bluetooth name starts with `PicoW Bluetooth` and includes the address.
3. Open Bluetooth settings on the source, then send `l` to Pico. Select and pair the intended Pico within 60 seconds. Only the source specified at build time is allowed.
4. Select Pico as the source's audio output and play a track. Send `?` twice, a short time apart. Check `streaming=1` and increasing `delivered`, `accepted` and `drained`. Also check `failed=0` and whether `dropped` or `rejected` increase. Silence may give `peak=0`.
5. Send `d` when finished. Use `?` to wait for `closing=0 signaling=0 connecting=0`. This does not power off radio. Unplug the board or restore the original firmware, and restore the source's audio output.

These checks show PCM reception and processing. `accepted` counts frames copied into the FIFO; `drained` counts frames taken out, not frames played through a speaker.

## Troubleshooting

- **No USB console:** check that the UF2 was built with `SINK_PCM_ENABLE_RADIO=ON`. The OFF build does not start USB serial.
- **Pico is missing from the device list:** check `working=1 failed=0`, then send `l`. If the 60-second window expires, use `?` to check that admission and connection have ended before sending another `l`. Repeated `l` does not extend the deadline.
- **Cannot reconnect after reboot:** keys are RAM-only. Remove only Pico's saved pairing on the source and pair again.
- **Connected but counters do not increase:** check the source's audio output and playback. `listen accepted=1` or `connect accepted=1` does not mean connection or audio reception is complete.
- **`failed=1`:** inspect status. The sample does not retry initialization or reconnect automatically. `c` requests one outgoing connection attempt.

**This exact public sample has host/ARM checks but has not been run on hardware (NOT_RUN).** Trials using other firmware are separate; see the [Validation scope](../../docs/validation.md).

## Optional five-second USB WAV capture

Use this only if you want recording after checking reception. Use audio you are allowed to record and tell affected people. Keep the same source address and enable capture in a separate build directory:

```sh
: "${PICO_SDK_PATH:?Set this to your Pico SDK directory}"
: "${SOURCE_BT_ADDRESS:?Set this to the verified source Bluetooth address}"
cmake -S examples/A2dpSinkPcmConsumer -B .work/builds/sink-usb \
  -DSINK_PCM_ENABLE_RADIO=ON -DSINK_PCM_USB_CAPTURE=ON \
  -DSINK_PCM_ALLOW_PAIRING=ON -DSINK_PCM_DISCOVERABLE_LISTEN=ON \
  -DSINK_PCM_PEER="$SOURCE_BT_ADDRESS"
cmake --build .work/builds/sink-usb --parallel 2
```

Flash `.work/builds/sink-usb/a2dp_sink_pcm_consumer.uf2`. Install `pyserial` in your Python environment and close any USB terminal using that port. Verify Pico's actual port and choose a **new private output directory that does not exist yet**. Create its parent first, on a local filesystem supporting hard links, such as NTFS or ext4.

Linux example, with `PICO_SERIAL_PORT` set to the verified port:

```sh
: "${PICO_SERIAL_PORT:?Set this to the verified Pico USB serial port}"
python3 examples/A2dpSinkPcmConsumer/capture_usb.py \
  --port "$PICO_SERIAL_PORT" --output-dir /tmp/sink-take-01
```

Windows PowerShell example; replace `COM7` and the output path for your setup:

```powershell
python examples/A2dpSinkPcmConsumer/capture_usb.py --port COM7 --output-dir 'C:\private\sink-take-01'
```

1. The collector opens only the specified port at 115200 baud. Opening a port can affect DTR/RTS on some devices, so verify the target first. No Bluetooth command is sent automatically.
2. Enter `i`, then `l` in the collector. Select Pico on the source and start playback. Use `?` to check `streaming=1`, the expected rate/channels and increasing `measured_frames`. An empty Enter refreshes output without sending a command.
3. When ready, enter **`CAPTURE`**. This sends `r` to Pico and records the next five seconds at most. Nothing is recorded before this command. Do not open the port in another terminal during transfer.
4. The collector finishes after one capture, waiting at most 15 seconds. Check the result below. Closing it does not disconnect Bluetooth; reopen the same port and use `d` and `?` to confirm disconnection.

Capture results:

- Exit code 0: `received-5s.wav` and `capture-result.json`, with all frames present and no reported loss.
- Exit code 2: `received-INCOMPLETE.wav`, with a validated END but short duration, loss or another reported error.
- Exit code 1: error. Do not treat it as a complete recording; check the message and output directory. CRC, frame-accounting or missing-END validation errors produce no WAV. File-operation failures may leave `.partial` or other output files. Existing files are never overwritten.

**Keep recorded audio out of Git, distribution packages and external uploads.** The collector does not write a raw dump. Its checks cover packets and counters, not audible quality. Incomplete WAVs concatenate received PCM and are shorter by the missing duration.

<details>
<summary>Implementation and data-format details</summary>

## Lifecycle and controls

The receiver and consumer have static lifetime. Call `begin()` once, without recreating owners on failure, then keep polling in one main loop. Connection commands require a working, idle receiver. `l` admits an incoming connection; `c` requests one outgoing attempt. Neither successful admission nor `begin()` means connected. `d` closes admission and requests disconnection; keep polling while it completes.

The FIFO cannot be shared across threads, IRQs or cores. Callbacks do not allocate, wait, perform I/O or call receiver APIs. USB stdout wait is zero; output can be dropped. This is not a real-time throughput guarantee.

## PCM and FIFO contract

`BoundedPcmSink.h` depends only on SDK-independent `IAudioSink.h`. `BoundedPcmSink<8192>` uses 8192 frames × 2 channels × 2 bytes = 32 KiB, plus bookkeeping and a separate 2 KiB pull buffer. After each receiver poll, the main loop drains the FIFO in chunks of at most 512 frames. Capacity is fixed, not universally lossless.

- `write()` takes borrowed, signed interleaved `int16_t` samples. Stereo order is L0,R0,L1,R1,…; frame count is per-channel, not bytes or sample elements. The pointer must remain valid during the call and is not retained.
- It copies the prefix that fits, preserving FIFO order, and rejects the rest (**drop-new**). The receiver counts rejected frames as dropped and does not retry them. A full queue returns zero; it does not pause the source.
- `pull()` capacity is in `int16_t` elements. It copies complete frames only and returns `{frames, sample_rate, channels, epoch}`. Unused destination elements are untouched. Null or zero-capacity pulls consume nothing.
- The consumer accepts positive rates and one or two channels. Nonempty null input, zero rate or invalid channels are rejected without changing valid queued data. Empty writes change nothing.
- Process each block's actual rate/channels. Consumer capacity does not extend the [Sink SBC configuration contract](../../docs/sink_sbc_configurations_contract.md); resampling, channel conversion and an output clock are not supplied.

## Boundaries and counters

`reset()` discards queued PCM, clears the format and advances `epoch`. It runs at receiver boundaries and may run synchronously during `disconnect()`. A valid nonempty write with a changed format also flushes the queue. Initial format assignment is not a format change. The meter follows epoch changes even when no PCM arrives.

Epoch is a local unsigned 32-bit boundary counter, not a Bluetooth session ID, and can wrap. Finish processing before the next poll. Any added DMA/driver queue needs its own reset cancellation; reset cannot revoke previously copied PCM. Do not retain borrowed pointers, send old PCM after a boundary or call receiver mutators from callbacks.

Cumulative statistics are `accepted_frames`, `rejected_frames`, `drained_frames`, `flushed_frames`, `reset_calls` and `format_changes`. Receiver `delivered_frames` counts consumer acceptance. Accepted frames discarded before draining are counted separately as flushed, not playback. Counters can wrap. Choose capacity and processing budgets for your application; the library's reserved decoder stack is not a universal stack guarantee.

For AVRCP, see [AvrcpMediaControlSample](../AvrcpMediaControlSample/README.md). It discards PCM and demonstrates PLAY/PAUSE; it is not integrated with this recorder. Do not concatenate the two mains: preserve the shared runtime, one main loop and owner lifetimes.

## Opt-in discovery for a source UI

`SINK_PCM_DISCOVERABLE_LISTEN` defaults to OFF. Even when ON, startup and `i` only display the address. An accepted `l` opens discovery; connection, generation change, failure, loss of readiness/admission, `d` or `c` closes it. At 60 seconds, the next poll closes discovery and calls `disconnect()`. This is a polled deadline, not an exact timing guarantee, and never reopens automatically.

Ordinary `listen()` changes connectability, not discoverability. Discovery and fixed-peer pairing permission are independent. `SINK_PCM_ALLOW_PAIRING` also defaults to OFF; enabling it permits SSP confirmation for the configured peer only.

## Status framing v1

Each of the three `?` lines starts with `SINK_PCM v=1 report=N line=L/3 `. L is state=1, PCM=2, receiver/FIFO=3, with the same N for all three. N is canonical decimal from 1 to 4294967295. Dropped output consumes its ID. After exhaustion, no framed report is emitted; the sample prints `Sink status report ID exhausted; no framed status available.` It does not wrap, restart or reconnect.

Status IDs restart on reboot. They do not establish freshness, replay detection, session identity, serial request/response matching or reliable USB delivery. Check line order and frame counters when interpreting a report.

### Transport and failure bounds

Capture alone uses main-loop-owned TinyUSB: a 512-byte endpoint buffer, 512-byte RX FIFO, 4096-byte TX FIFO, a fixed queue of 32 packets (32-byte header + up to 1024-byte payload + 4-byte CRC each), and a 1 KiB CRC table. Statistics-only builds retain normal SDK USB processing. The reset vendor interface and baud-rate reset callback are preserved; see [provenance](PROVENANCE.md). Capture firmware does not allocate or perform blocking I/O; callbacks populate only the PCM FIFO.

Packets contain `PCMUSB01`, six little-endian uint32 fields (type, sequence, payload bytes, rate, channels, epoch), payload and CRC-32 covering header/payload. Types are BEGIN=1, PCM=2, END=3. PCM is signed little-endian 16-bit interleaved. END contains eight uint32 values: attempted frames, USB-lost frames, receiver-dropped frames (including FIFO rejection), decode failures, reason, elapsed ms, target frames and END sequence. Reasons are 0=target reached, 1=format/epoch changed, 2=timeout, 3=stream stopped.

Recording stops at rate × 5 frames, on a format/epoch change or stream stop, or after seven seconds. Transmission stops after twelve seconds total even if the host cannot drain it. Queue overflow advances sequence and lost counts; partial USB writes resume from their offset. Console output is disabled during transfer, and incoming commands are discarded. Missing END is never success. The host checks CRC, sequence, format, duration and frame accounting; counter deltas use unsigned 32-bit arithmetic over the recording interval.

</details>

<a id="compile-only"></a>
<a id="build-without-using-hardware"></a>
## Optional compile-only build

**This is not required before first use.** It builds the same sample with radio OFF. USB serial does not start either; do not flash it to try the controls.

```sh
cmake -S examples/A2dpSinkPcmConsumer -B .work/builds/sink-pcm-offline \
  -DSINK_PCM_ENABLE_RADIO=OFF -DSINK_PCM_ALLOW_PAIRING=OFF \
  -DSINK_PCM_USB_CAPTURE=OFF -DSINK_PCM_DISCOVERABLE_LISTEN=OFF \
  -DSINK_PCM_PEER=00:00:00:00:00:00
cmake --build .work/builds/sink-pcm-offline --parallel 2
```

All four options default to OFF. The OFF build type-checks code but does not establish enabled-radio linkage or behavior. Radio ON requires a nonzero six-octet source address.

## Verification scope

```sh
bash tests/host/run_sink_usb_capture_tests.sh
bash tests/host/run_sink_pcm_sample_tests.sh
bash tests/host/run_sink_discoverable_listen_tests.sh
```

These host tests use synthetic data without hardware, USB, radio, flashing or recording. They do not establish USB throughput or Bluetooth/audio hardware behavior. See the [FIFO test README](../../tests/host/sink_pcm_sample/README.md).
