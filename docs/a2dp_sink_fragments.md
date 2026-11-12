# Bounded SBC fragment reception

[日本語](a2dp_sink_fragments_jp.md) · [PCM consumer](a2dp_sink_pcm.md)

`A2dpSinkReceiver` now reassembles fragmented SBC in its production receive path. No new application API is needed: continue polling and copy borrowed PCM during `IAudioSink::write`. There is no PCM callback until one complete frame has passed structural validation and the SDK CRC/decoder checks.

## Contract

The live receiver now uses the [expanded SBC parameter contract](sink_sbc_configurations_contract.md): 44.1/48 kHz, all four channel modes, 4/8/12/16 blocks, 4/8 subbands and both allocation methods. The advertised bitpool envelope is 2–86, subject to negotiated and per-format limits. The largest admitted frame is 185 bytes; conservative assembly storage is 524 bytes. These are **not total implementation RAM**: RTP/output scratch storage, decoder padding and per-packet loss metadata are additional. The earlier 119-byte restricted-format checkpoint remains historical evidence below.

The wire contract follows Bluetooth SIG [A2DP 1.4.1, sections 4.3.3–4.3.4](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/a2dp_v1-4-1_1752513648/A2DP_v1.4.1/out/en/index-en.html): fragment packets share a timestamp, the low nibble counts remaining fragments including the current packet, S/L identify first/last, and nonfinal packets have equal total size. Sequence numbers advance for each RTP packet ([RFC 3550 section 5.1](https://www.rfc-editor.org/rfc/rfc3550.html#section-5.1)). The new fragment path requires dynamic PT, M=0 and X=0; it ignores SBC reserved bit 4 as required by A2DP section 1.5.2.

Additional **bounded receiver policies**, not universal protocol claims:

- Exactly one pending frame; 2–15 nonempty fragments, consecutive sequence numbers with 16-bit wrap, and strictly decreasing remaining count.
- SSRC, PT and CSRC list stay fixed within a group. RTP padding is rejected; the last packet cannot exceed the first packet's size.
- Assembly never exceeds its 524-byte fixed storage. Final bytes must describe exactly one negotiated, bitrate-valid SBC frame (currently at most 185 bytes), without trailing bytes. New valid starts replace incomplete groups; complete-frame packets discard any pending group.
- Malformed packets, sequence rejection, stale queued generation/state, configuration/start/suspend/stop/release, explicit disconnect and HCI lifecycle retirement discard pending assembly. No partial bytes survive these boundaries.
- Queue overflow or oversize adds an ordered loss marker to subsequent copied packets. A prefix queued before a lost callback cannot combine with a suffix after it, even if that suffix reuses a seemingly consecutive sequence number.
- No reorder window, retransmission, partial decode, concealment, deadline timer or remote backpressure. An incomplete frame can remain in bounded storage until another packet or lifecycle boundary invalidates it.

The existing complete-frame parser keeps its previous policy, including its reserved-bit and RTP extension handling; this change does not claim complete A2DP/RTP conformance. Fragmented frames too large for the negotiated restricted codec are rejected. Synthetic host packet acceptance does not verify real MTU negotiation or a peer's fragmentation behavior.

## Ownership and diagnostics

BTstack callback bytes are copied into the existing bounded queue; assembly and completed packet bytes have receiver-owned fixed storage. Decoder input is copied into its padded scratch buffer. PCM is still borrowed only for the synchronous `write` call. A codec reset now also clears external synthesis history, preventing prior-stream PCM from leaking into a new decode.

`received_packets` counts callbacks. `processed_packets` counts accepted RTP packets, including incomplete fragments. `decoded_frames` increments only after a successful complete decode; one encoded frame produces blocks × subbands PCM frames per channel (16–128), with one or two channels. Expected audio timestamp advances by that frame count only on the final fragment; intervening accepted fragments advance sequence only. Malformed fragment groups use existing `parse_errors` counters; queue and sequence diagnostics retain their separate meanings. A lost malformed/orphan fragment is not promised to increment `missing_packets` immediately, because only accepted packets update sequence state.

## Evidence and limits

The original restricted-format checkpoint includes public-facade diagnostics and production callback/queue/real-codec tests under ASan/UBSan, nonzero PCM equality against complete-frame decoding, boundary/failure cases, and a reachable enabled Pico W ARM build. That fragment-reassembly checkpoint does not establish real-peer fragmentation, acoustic quality or hardware acceptance of this path.

The expanded format evidence is separate; the old checkpoint does not prove the newly added formats. Use the [current format contract](sink_sbc_configurations_contract.md) for the wider configuration test.

Run the focused fragment regression with installed SDK inputs and a fresh build directory.
Choose a new directory name on each run; the example name below must not contain
previous build objects. The runner does not remove stale objects from a reused directory.

```sh
mkdir -p .work/sink-fragment-local/tmp
PATH=/usr/bin:/bin TMPDIR="$PWD/.work/sink-fragment-local/tmp" \
  BUILD_DIR="$PWD/.work/sink-fragment-local/build" \
  PICO_SDK_PATH=/path/to/pico-sdk \
  bash tests/host/run_sink_fragment_boundary_tests.sh
```

[Validation scope](validation.md)
