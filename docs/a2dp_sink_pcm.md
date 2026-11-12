# A2DP Sink: consuming PCM without blocking reception

[日本語](a2dp_sink_pcm_jp.md) | [Public receiver example](../examples/A2DPSinkReceiver/README.md)

The existing receiver example counts accepted frames. The [bounded PCM consumer example](../examples/A2dpSinkPcmConsumer/README.md) shows how to copy them into application-owned storage and drain them later on the same main loop. It does not drive a DAC/I2S device or implement a real-time player. Its default binary does not start Bluetooth. The separate `SINK_PCM_USB_CAPTURE=ON` option adds explicit five-second USB PCM recording; recording remains OFF by default and requires the `CAPTURE` command in the [serial collector](../examples/A2dpSinkPcmConsumer/capture_usb.py).

## Borrowed input and frame units

`IAudioSink::write` runs synchronously during receiver `poll`, not in the BTstack media callback or an interrupt. Its `const int16_t*` is borrowed only for the call. A frame contains one signed 16-bit sample for each channel, in interleaved order. For stereo, N frames contain 2N samples; a frame count is neither a byte count nor an element count. Use the callback's sample rate/channel count rather than assuming the source format.

Copy only the prefix you can store, then return that accepted frame count in `0..frames`. The receiver counts an unaccepted suffix as dropped and never retries it. There is no remote-source backpressure contract. Do not block until buffer space appears, retain the borrowed pointer or claim all frames accepted when only some were copied. The sample consumes copied frames in bounded chunks outside the callback. Its diagnostic processing is not a real-time audio output driver.

## Stream boundaries and lifetime

`reset()` flushes queued PCM on the library's session/format/start/stop boundaries. Treat old queued samples as invalid even when the next stream has the same nominal format. Keep receiver, consumer and storage alive for program lifetime. Invoke all receiver methods and consumer operations from one loop, not an IRQ or another core. Do not call receiver APIs, wait, allocate unbounded memory or perform file/device I/O from `write`/`reset`.

A real output device needs a separate reviewed contract for copying/DMA ownership and completion. Passing a temporary drain buffer to asynchronous hardware is unsafe unless it copies or retains storage with sufficient lifetime. A sample FIFO alone does not supply a resampler, latency correction, underrun concealment or clock synchronization.

## Receiving path and current limits

After `begin` returns `Ok`, keep polling until `snapshot().working`. Then either permit the configured peer with `listen` or request one outgoing `connect`. Local acceptance is not a signaling/configuration/streaming result; observe snapshots and diagnostics. Listening enables shared connectability, not global discoverability. Do not silently enable pairing confirmation. Disconnect/reset do not make an asynchronous transport instantly closed or authorize destroying the owners.

The current public receive implementation supports the [expanded SBC formats](sink_sbc_configurations_contract.md): 44.1/48 kHz, mono/dual/stereo/joint, all four block lengths, 4/8 subbands and loudness/SNR. Each callback carries 16–128 frames per channel and one or two channels. A consumer accepting a broader PCM layout does not extend radio codec support. Full codec coverage, broad facade interoperability and hardware speaker playback remain unverified. See [SDK coverage](a2dp_sdk_coverage.md).

## Validation scope

The integrated public PCM consumer remains hardware-unverified. A short receive test in a separate application does not establish other formats/peers, endurance, source-waveform equality or physical output timing. Recorded results are in [Validation scope](validation.md).

## Optional USB recording example

The public example drains an 8,192-frame FIFO in bounded 512-frame chunks on the main loop. With `SINK_PCM_USB_CAPTURE=ON`, it uses main-loop-owned TinyUSB and EP512/RX512/TX4096 transfer buffers while retaining 64-byte full-speed USB packets. The recording path adds fixed packet storage and CRC/sequence/frame accounting. It is test/application code above `IAudioSink`, not a change to the decoder or a library-level lossless-output guarantee.

`capture_usb.py --port <exact-device> --output-dir <new-directory>` uses one explicitly selected serial port. The operator sends `l`/`c`/`?`/`d` explicitly, operates the Source separately and enters `CAPTURE` only when ready to record the next five seconds. The collector performs no device discovery, flashing or Source control. A validated complete capture becomes `received-5s.wav`; validated missing data is labeled `received-INCOMPLETE.wav`; invalid framing/CRC or missing END is not promoted to a valid recording. Closing the serial port does not disconnect Bluetooth. See the [example instructions](../examples/A2dpSinkPcmConsumer/README.md) for setup, cleanup, dependencies and limits.

Copyrighted recordings and raw audio are local listening data, excluded from Git/publication/distribution. Only statistics and hashes belong in shared evidence.

## Explicit delay reporting (host-verified, hardware acceptance open)

`reportDelay100us(delay_100us, expected_generation)` requests one AVDTP delay
report for this receiver's configured fixed peer. Values are **0..65535 in
100 µs units**, inclusive (0..6553.5 ms). A wider input type lets the API reject
65536 rather than silently truncate it. Zero is transmitted as zero, not used as
a special "unknown" marker. The application measures its receive-to-render
latency; the library does not infer latency from PCM counts or report periodically.

Capture `snapshot().generation` with the measurement. Pass that same generation
even if the request happens later. A measurement from a disconnected session
must not be relabeled for a new one. For a measured microsecond value, see the
[compiled one-shot helper](../examples/A2DPSinkReceiver/DelayReportExample.h),
which rounds upward and rejects overflow. It belongs in your main loop after
`poll`, never in `IAudioSink::write`/`reset`, an IRQ or a second core.

- `Accepted`: BTstack admitted/queued the transaction only. It does not prove
  that bytes were sent, the peer accepted them, or latency compensation occurred.
- `InvalidDelay`: outside the representable range.
- `StaleSession`: generation or live session identity no longer matches.
- `NotReady`: lifecycle, pending state event, ownership or unstable endpoint
  prevents safe admission. Includes reentry during this receiver's own poll/output
  callbacks and shared-peer operation. Callers must keep every call on the main
  loop outside all callbacks; callbacks belonging to other profiles are not
  dynamically detected by this receiver.
- `NotNegotiated`: delay reporting was not selected in SET_CONFIGURATION.
  Advertising the capability alone is insufficient.
- `SdkRejected`: `sdk_status` is the exact nonzero BTstack return (including
  busy). There is no retry, queued application request, or new disconnect policy.
  `sdk_status` is meaningful only for `Accepted`/`SdkRejected`.

Only stable CONFIGURED, OPENED or STREAMING endpoints are eligible; closing,
opening and endpoint control transactions are rejected. BTstack arbitrates the
signaling initiator and may still return busy. Check the returned result directly;
this call intentionally preserves `snapshot().last_operation_status` and other
receiver state. There is no per-report delivery/remote-acceptance receipt in this
API. Same-peer Source+Sink remains rejected for this operation even in experimental
builds; distinct-peer ownership does not authorize shared-peer reporting.
