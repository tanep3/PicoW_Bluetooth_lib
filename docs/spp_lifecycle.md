# SPP peer selection and ERTM integration

[日本語](spp_lifecycle_jp.md) · [Bond backend selection](bluetooth_bond_storage.md)

Use quiescent peer selection and caller-owned RFCOMM ERTM buffers to control the client lifecycle. Host tests and SDK builds are separate from radio interoperability. **ERTM retransmission, peer switching over the radio, power loss and persistent bond recovery have not been hardware-qualified.**

## Change the allowed peer without resetting other profiles

`Client::selectPeer(Address)` queues selection of the one peer that this SPP facade permits at a time. It does not select an A2DP/AVRCP peer or change their pairing policy.

1. Disconnect every SPP route and process callbacks with `poll()` until all are Closed.
2. Cancel/drain any outstanding SDP query. Cancelled is not drained while `query.busy` remains true.
3. Wait for pending SDK bindings and any ERTM multiplexer buffer leases to be released. Closing DLCs alone does not mean their multiplexer buffer is free.
4. Call `selectPeer`. False means not queued. True is queue acceptance; inspect `snapshot().config.peer` and history after polling for the actual result.

Zero addresses are rejected. Core rules also reject route activity, busy SDP and endpoint registration/withdrawal in progress. Listening endpoints stay registered, but future incoming connections are admitted only for the new peer. Old offers are still rejected against their original SDK identity, not relabelled as the new peer.

A successful change retires route generations and old query results. Late old-peer ACL/SDP/data notifications cannot become new-peer events. Existing other-profile ACLs are **not disconnected**. If the new peer already has an ACL owned by another profile, the adapter imports that actual link/epoch. Its epoch need not be newer than the previous peer's epoch. This is sequential one-peer reconfiguration, not simultaneous multi-peer SPP, automatic discovery, reconnect policy or bond deletion.

## ERTM compile configuration

The same consumer and all its BTstack sources must have both defines:

```c
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE_FOR_RFCOMM
```

`Client::ertmCompiled()` reports that build configuration. An AVRCP browsing application may need only the first flag. Do not enable the RFCOMM flag merely because browsing needs L2CAP ERTM. Resource pools, callbacks and buffers for browsing and RFCOMM are separate, while their HCI/L2CAP limits remain shared.

The [SPP CMake helper](../cmake/picow_spp.cmake) also selects the validated transport overlay. A RFCOMM ERTM build without `PICOW_SPP_SDK_OVERLAY` is rejected: use the helper rather than defining this marker manually. Without both transport flags, `configureErtm` returns false and ordinary SPP remains Basic mode. With both flags but no configured buffer pool, the SDK can still use Basic mode. Compiling ERTM therefore does not prove it was negotiated.

## Caller-owned memory and immutable setup

Before `begin`, call `configureErtm(const ErtmPool::Config&)`. Configuration is accepted once, while the engine is stopped and has no pending events. The configuration is copied; the caller owns one or two backing buffers and must keep them valid for the entire client lifetime. Never free/reuse an active lease on disconnect request, timeout or radio fault. Wait for the SDK's matching release notification; after terminal failure/reset, do not invent a release that was not observed.

One lease belongs to an RFCOMM multiplexer, not to each route. Two routes to one peer normally share it. Lease IDs prevent duplicate/foreign releases from freeing another buffer. Peer switching waits for all leases to return. The fixed capacity is two leases; no heap allocation occurs.

The default link proposal is optional ERTM, eight retransmissions, 2000ms retransmission timeout, 12000ms monitor timeout, local MTU1000, four RX and four TX buffers and FCS disabled. Explicitly configure buffers and `count`. The adapter validates:

- Non-null, non-overlapping buffers and pointer/size overflow.
- MTU48–1029, valid FCS and mandatory flags, nonzero retransmit count, SDK minimum timeouts.
- Independent RX/TX buffer counts, each 1–63, matching enhanced-control sequence bounds.
- The SDK's actual 1–16-byte alignment advance, state arrays, reassembly buffer and minimum MPS48.
- Maximum MPS constrained by shared `HCI_ACL_PAYLOAD_SIZE`, accounting for L2CAP header, enhanced control and FCS. An oversized backing allocation is deliberately clipped to a safe usable size.

`ertmStatus()` reports configuration, requests, granted/released/current leases, capacity misses, foreign peers and unexpected releases. These are allocation observations, **not negotiated-mode or retransmission counters**.

`transport(Handle)` observes the actual currently-open RFCOMM and Classic L2CAP transport: `Client::Transport::Basic`, `Ertm` or `Unknown`. Invalid/stale/closed handles, a retired ACL or a transport not confirmed open report `Unknown`. It does not infer mode from a retained buffer or requested configuration. Use it from the same main loop as the facade.

## Fallback and terminal failures

With `link.ertm_mandatory=0`, lack of a lease may fall back to Basic mode; remote negotiation may also select Basic and release the ERTM buffer. With `ertm_mandatory=1`, a local capacity failure cannot silently succeed as Basic: the facade posts a terminal runtime fault. This powers off the shared radio and affects other profiles. Size the pool for the application, and do not select mandatory mode if that failure policy is unsuitable.

Transport overlay v3 immediately accepts a peer's explicit Basic-mode RFC option when ERTM is optional, without importing its zero ERTM parameters. It releases the ERTM lease once. The mode of an outstanding configuration request is tracked separately from the channel's selected mode: a crossed response to an earlier ERTM proposal cannot be mistaken for rejection of a Basic retry, or open mismatched modes. Stale and duplicate responses are ignored. A rejection of the actual Basic retry still closes the channel; mandatory ERTM still rejects a Basic-only peer.

A peer may already have a Basic RFCOMM multiplexer established while it was not the selected SPP peer. Reusing that mux after `selectPeer` bypasses the SDK ERTM request callback. Before exposing a successful required-mode route, the facade checks the actual open transport. Basic or Unknown produces a terminal runtime fault (`0x11`), not an Open route. As with mandatory local exhaustion, this stops the shared radio. A mismatched/retired ACL callback cannot trigger this check against a different connection.

The pinned SDK gives its ERTM request callback no reject result, so mandatory local exhaustion remains terminal. Separately, the original outbound L2CAP creation-failure path leaked list membership and omitted the ERTM release. The validated overlay now unlinks both failed objects and emits exactly one release before returning. The facade records a route-local failure, keeps the shared runtime alive and permits a clean retry. It does not invent another release or shut down other profiles for this repaired path.

## Sending with either compiled transport mode

In a Basic-only build, prepared send reserves/releases the shared L2CAP buffer. In a RFCOMM-ERTM-enabled build, BTstack has its own 1030-byte RFCOMM scratch buffer and its reserve/release functions must not be called. The adapter makes those operations no-ops, still checks readiness, and uses the actual prepared-send result. BTstack then copies into L2CAP-owned transport storage. Transient errors retain the application FIFO for retry. Do not replace this with `rfcomm_send`, which masks a final send error in this SDK.

## Validated transport overlay and remaining acceptance

`picow_enable_spp_sdk_overlay(target)` generates hash-checked `l2cap.c` and `rfcomm.c` into the consumer's build directory. SDK originals are not edited. Only consumers with its target property select these sources; unrelated consumers keep the original sources, and the AVRCP overlay can coexist. Unknown hashes, changed source layout and in-place output are rejected. Original attribution remains intact.

The overlay validates remote MPS, six-bit transmit-window bounds, minimum MTU and complete L2CAP option headers/payloads before arithmetic or state changes. It uses wider TX sizing arithmetic, caps stored TX slots, corrects the TX extent calculation, preserves optional Basic fallback, and prevents stale configuration flags from reopening a rejected channel. It also repairs RFCOMM immediate-allocation cleanup and the MSC event's incorrect length byte. Tests compile the actual generated SDK state machines and include baseline counterexamples; these are not replacement-model-only tests.

This is targeted hardening, not a complete Bluetooth-stack audit or radio certification. The local buffer contract, malformed negotiation fixtures and SDK build do not by themselves prove retransmission timing, loss recovery, interoperability or shared-profile performance. Those hardware acceptance rows remain open.

Other known bounds remain: two SPP routes, one selected peer at a time, maximum RFCOMM frame1024 and application-owned retry/policy. These are explicit resource contracts, not unlimited-device support. Shared-profile coexistence needs a combined build and radio validation, not independent role builds alone.

## Tests and required hardware checks

For included checks, see the [build guide](build_guide.md#compile-only).

The ERTM pool suite extracts and compiles the validated SDK overlay's actual setup/configure functions across alignment, independent RX/TX counts, capacity limits and maximum windows. See retained logs for exact case counts. Adapter tests use real SDK declarations and mocked transport with both build modes: FIFO retry, no ERTM reservation calls, lease release, synchronous creation failure and safe retry, required-mode exhaustion, reused Basic/unknown mux rejection and peer-generation retirement. These tests do not simulate Bluetooth retransmission.

The v3 functional negotiation regression runs the real L2CAP dispatcher, configuration state machine and signaling serializer. It covers both crossed request/response orders, Basic Open with one release, mandatory rejection, valid ERTM, pending/positive responses to superseded ERTM proposals, stale/duplicate identifiers and unknown CIDs. Fallback fix evidence records the v2 counterexample and v3 host/ARM checks separately from radio results.

Before closing the feature rows, verify public-API ERTM initiation and acceptance against a peer with independently confirmed ERTM; Basic fallback versus mandatory rejection; two-route full duplex under pressure; loss/retransmission and resource failures; disconnect/reconnect without leaked leases; selection between two real peers without disturbing A2DP/AVRCP; and shared browsing/A2DP/SPP memory limits. Persistent-bond reset/power-loss tests belong to the selected storage backend and must not log real keys. See [bond storage](bluetooth_bond_storage.md).

Evidence: host/ARM results and hashes.

[Offline-by-default control and peer-switching example](spp_examples.md)

## Hardware validation scope

Bounded trials cover Basic fallback, actual two-route ERTM traffic, normal disconnect/reconnect and one fifteen-minute workload with a limited peer. They do not qualify retransmission under loss, power failure, switching between two real peers, a fresh Pico-initiated ERTM L2CAP connection or other-profile coexistence. Interrupted application data is not replayed automatically. Measurements and verdicts, including prior failures, are retained in [Validation scope](validation.md).
