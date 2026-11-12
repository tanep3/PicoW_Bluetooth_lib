# Using A2DP and SPP in one application

[日本語](a2dp_spp_coexistence_jp.md)

A2DP and two SPP routes share one BluetoothRuntime and the same radio resources. Poll active profiles from one main loop. The application supplies SPP framing, retries and traffic control.

## Start from an integration example

Use the A2DP + SPP example in [DeveloperSamples](../examples/DeveloperSamples/README.md). For AVRCP as well, follow the [three-profile example](../examples/DeveloperSamples/a2dp_spp_avrcp.cpp). Joining an existing ACL uses explicit `connectOverExistingAcl()`; it is distinct from ordinary discovery through `connect()`.

Bounded tests observed coexistence with a fixed peer under selected traffic. They do not guarantee audio quality, latency, long-running stability or reconnection under every load. Exact byte counts or successful SBC decoding do not establish uninterrupted playback. Some earlier trials failed strict audio or duration criteria; later communication successes do not erase those failures. See [validation scope](validation.md).

## Send deferral and buffer ownership

CAN_SEND_NOW does not reserve the shared HCI buffer until a later send.
This SDK's payload_rtp helper sends after buffer reservation/RTP sequence updates, so it cannot be used unchanged for retry if capacity disappears by execution time.
The current adapter uses the public raw media packet API. The basic L2CAP path checks actual capacity/remote MTU before reserving the buffer.
The adapter constructs RTP headers and advances sequence/timestamp/packet count only on success.
Capacity accounting includes 12 RTP bytes, one SBC payload-header byte, and complete SBC frames.

AudioPipeline retains at most one unsent encoded batch. During deferral it neither reads new PCM nor re-encodes the same PCM. Partial PCM supply is retained until complete.
MediaDeferred does not advance transmission count, sample count, or pacing; a later Tick requests fresh send permission.
Configure, stream disable, or generation change discards the old batch.
Synchronous configure from source/encoder/transport callbacks is rejected before modification, preventing release of buffers in use.

`A2dpSourcePlayer::diagnostics()` returns actual transport RTP timestamp, next sequence, sent packets, last send status, media deferrals, and pending encoded bytes, independently comparable with public snapshot packet/sample counts.

The media deadline is not rebased to the current time merely because lateness exceeds 20 ms.
The 44.1/48 kHz rational media clock retains debt and does not skip source samples.
With fresh credit, at most one packet is sent per public poll; at most one encoded batch remains pending.
Observe `next_due_us/debt_us/max_lateness_us/pacing_rebases/pacing_discarded_us` and related fields.
Recovery of average supply rate is distinct from uniform packet spacing or continuous received PCM.
This does not guarantee large delays cannot underflow the receiver or that catch-up is contention-free.

## Applicability

Validation covers Pico SDK 2.1.1 / BTstack 1.6.2, one main loop, CYW 43 polling, and basic AVDTP.
In this configuration, no yield or other-owner interruption occurs between raw API readiness check and reserve/prepared-send.
Do not independently release SDK-owned buffers merely because a reserved flag is set.
No guarantee is made for arbitrary transports, ERTM, or concurrent calls from another core/IRQ.

Actual CYW 43 hardware-write errors take terminal paths such as SDK assertions, unlike ordinary resource deferral. General recovery is not guaranteed.
SDK callbacks contain no generation number; completely identifying old callbacks after CID/SEID reuse is also not guaranteed.
