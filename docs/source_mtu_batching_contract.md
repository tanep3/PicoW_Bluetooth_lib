# A2DP Source complete-frame MTU batching

**Historical implementation checkpoint.**

**Historical complete-frame checkpoint.** The contract and evidence below describe that checkpoint. The current [Source SBC fragmentation contract](source_sbc_fragmentation_contract.md) supersedes the refusal when one frame cannot fit and the Source-specific minimum-payload setting, while retaining the one-to-four-whole-frame path. Original evidence and input manifests remain unchanged; use the separate revision2 results for the current implementation.

The `A2dpSourcePlayer` core now selects one to four complete SBC frames per media packet. This is library behavior, not an example feature. It does not introduce SBC fragmentation or additional codec formats.

For the supported stereo, 16-block, 8-subband SBC format, one encoded frame occupies `12 + 2 * negotiated_max_bitpool` bytes and represents 128 samples per channel. Both supported rates (44.1 and 48 kHz) use this size formula. The effective payload is the SDK media payload limit capped by the player's 1024-byte transport storage, excluding the 12-byte RTP header but including the one-byte SBC payload header. The selected count is `min(4, floor((payload - 1) / frame_bytes))`. Zero/negative SDK limits and limits that cannot hold one complete frame plus header fail before PCM is read. At bitpool 53 the thresholds are 119, 237, 355 and 473 bytes. At bitpool 2 a 17-byte payload is valid.

## Ordering and ownership

- Initial accepted codec configuration establishes the encoder's exact size. Before the MTU is known, buffers are provisionally bounded at four frames.
- Stream establishment reads the actual payload limit, chooses a batch, configures transport and replaces/reset pipeline buffers before the reducer enters Open.
- Accepted codec reconfiguration recomputes the count from the existing payload and new exact frame size. Local reconfiguration waits for the final ACK. Remote accepted changes occur while stopped. No automatic resume is introduced.
- The singleton pipeline owns one partial PCM batch and one unsent encoded batch. Its existing allocation is at most 4096 bytes of PCM storage (including its existing extra capacity) and 472 encoded bytes for this codec subset. Transport storage remains fixed at 1024 bytes with a 1036-byte RTP packet vector. These are single-configuration buffer sizes, not whole-process or peak allocation bounds: replacement allocates new buffers before freeing the old ones, so two sets may coexist transiently. Configuration is outside pumping, and resetting/replacing a configuration discards pending PCM/encoded data.
- A busy send retains identical encoded bytes. Retry does not reread PCM, encode again, increment packets, increment RTP sequence/timestamp, or advance the accepted sample clock. It needs a later poll and fresh credit. Pause discards pending audio; accepted RTP sequence/timestamp are retained across pause/reconfiguration.

Successful media completion explicitly reports the selected batch's samples per channel. The reducer adds that count and advances rational microsecond pacing by `samples * 1000000 / accepted_rate`, retaining the remainder. No fixed 512-sample increment remains in the adaptive path. The transport independently parses complete frames and counts their samples for RTP; its wire implementation is unchanged.

## Compatibility and limits

`a2dp::Config.minimum_payload` remains 473 for existing fixed-four-frame engine consumers. Only the adaptive player sets its floor to 17 and applies the actual codec-size check. `Event.accepted_samples` is appended after existing fields, defaults to 512 for legacy fixed-four-frame executors, and must be supplied explicitly by adaptive executors. The reducer accepts 128, 256, 384 or 512 on successful MediaResult; deferrals ignore this field. Applications must rebuild against the changed header; no binary ABI compatibility is promised.

Source format restrictions, main-loop affinity, admission/security, shared-peer policy and Sink behavior are unchanged. This feature is not a hardware/interoperability certification and does not close any broad acceptance group.

## Focused verification

Validation covered only six fixed cases through the real player, pipeline, transport and pinned BTstack SBC encoder with runtime/SDK event stubs. It freezes non-system transitive input hashes before compile, retains build/logs, uses ASan for all compiled sources and UBSan for project C++. Pinned upstream fixed-point SBC C is excluded from UBSan because of its signed-shift/arithmetic conventions; leak scanning is disabled in this execution environment.

The cases cover 1/2/3/4 wire frame counts, too-small rejection, low-bitpool minimum, exact accepted sample/RTP/cadence accounting, one deferred identical-byte retry per valid case, and the 237-byte case's pending-packet pause followed by rate/bitpool reconfiguration to four frames. This is not an exhaustive codec sweep, malformed-input corpus, broad Source lifecycle review, or radio test. An offline ARM compile/link is separate build evidence only; nothing is flashed or executed on Bluetooth hardware.


For included sample builds and runnable checks, see the [build guide](build_guide.md#compile-only). Those checks do not rerun every feature-specific validation described here.
