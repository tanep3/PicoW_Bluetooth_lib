# Raw SPP and multiple connection routes: design review proposal

[日本語](spp_generic_protocol_design_jp.md)

- Created: 2026-11-08 UTC / 2026-11-09 JST.
- Revision: **r5 (2026-11-09). Retains approved requirements and incorporates stage 3 shared runtime, SPP facade and hardware SDP validation.**
- Status: **Stage 2 host validation, stage 3 real-SDK integration, and API-based hardware tests of UUID connection, multiple candidates and two routes are complete. Long-duration and simultaneous-audio validation remain incomplete at this stage.**
- The existing document path is retained for existing references. The earlier proposal for a library-internal generic protocol is no longer the policy.

## Current progress (added 2026-11-10)

The status above and the following section record stage 3. Later work implemented and tested dual-SPP load/reconnection, concurrent A2DP communication, a basic AVRCP target, approximately 182 seconds of three-profile coexistence, and seven developer samples.
See [Validation scope](validation.md) for current results and unverified areas, and [developer samples](developer_samples.md) for usage.
Success for a fixed peer and finite duration is not a guarantee of arbitrary endurance, universal interoperability, latency or throughput. Earlier failed trials and stage-specific unverified items remain as history.

## Stage 3 implementation status (added 2026-11-09)

Real-SDK integration and the public `spp::Client` were added. See the [SDK integration record](spp_sdk_integration.md) for current contracts, SDP event format, cancellation draining and MTU/RAM updates.
The stage 2 MTU of 256 and Engine size of 15,304 bytes below are historical values for that configuration.
The stage 3 values are MaxFrame 1024, outgoing MTU 1019 and ARM Engine 28,616 bytes.
Resolving an unknown channel from standard/custom-128 UUIDs and exact 1537-byte echo for each passed on hardware.
Enumeration of two candidates with the same UUID, one second with no connection before selection, and echo after selecting route 0 were also confirmed.
The initial additional route 1 stopped terminally with status 86 because L2CAP pool 3 was insufficient. With diagnostic pool 6, all three trials passed, including explicit selection, Open and exact 1537-byte echo on both routes (approximately 12 seconds).
Core errors and trace drops were zero. Postflight confirmed no final ACL or persistent bond.
A real-SDK link of A2DP/SPP in one target also succeeded, but simultaneous hardware audio was not yet verified.
The shared configuration was explicitly unified at HCI 1028; A2DP-only 1695 was unchanged.
The integration record above links the details and preserved original traces.

## 1. Agreed direction and changes from r2

### Latest user direction at this design revision

**The library provides raw SPP. The application first uses two routes, one for control and one for data, and can multiplex several data types within the data route using its own IDs/lengths. Framing, data identification, dropping, priority, audio formats and control semantics belong to the application.**

- The first version has one peer. Retain the approved policy of supporting both connection initiation and acceptance.
- Design for **multiple SPP routes to the same peer**. Prioritize measured load and bidirectional behavior on two routes; four are optional if needed. This does not mean adding other Bluetooth devices.
- The latest requirement goal is **at least two simultaneous SPP routes to the same peer**. The peer must also support two endpoints/connections. Four are not mandatory. Propose the guaranteed API scope, resource conditions and test scope after measurement, without promising control latency or unlimited transfer volume.
- Retain the shared runtime, Sokoban-style state architecture and staged SPP → A2DP regression → coexistence validation.
- BluePico is a usage example. Do not assign fixed meanings or numbers such as “audio route” or “control route” inside the library.

### Removed from the previous proposal

- Library-specific length prefix, magic, version, CRC, type, sequence and Hello exchange.
- Library recognition of SBC/PCM/buttons or message-level handling.
- Library-internal ExpireBeforeSend / ReplacePending, media deadlines or application-priority decisions.
- The assumption that audio and control must share one RFCOMM stream.

An application may add its own minimal envelope such as “length + contents.” The library does not add, detect or duplicate that envelope. Any future application examples remain separate from the SPP core contract.

## 2. SPP, RFCOMM and separate routes

SPP (Serial Port Profile) uses RFCOMM for serial connections; it does not define an audio/button application protocol. **The application may define its own communication content.**

Here, “another RFCOMM” means **another independent SPP byte stream to the same peer**. It connects to a different RFCOMM server channel and has its own connection CID. For example, the application can use one for audio and another for control.

| Term | Meaning |
|---|---|
| Peer address | Identifies the same Bluetooth peer |
| RFCOMM server channel | The peer's accepting endpoint; multiple routes need different endpoints |
| RFCOMM CID | Local identifier assigned by BTstack after connection; different from the server channel number |
| Library route/session handle (name undecided in this proposal) | Application route identifier; proposed to include a generation to protect against CID reuse |
| Central / peripheral | Radio role, separate from initiator/acceptor and route purpose |

Four routes (“control,” “playback audio,” “microphone audio,” “buttons”) were discussed, but the latest policy is to **first test two routes: separate control SPP, and a data SPP carrying multiple types in the application's format**. Each SPP stream is bidirectional, so one data route can carry playback-like host→Pico and microphone-like Pico→host data. Feasibility and performance still need measurement.

IDs identify content; lengths can reconstruct boundaries, but neither solves head-of-line waiting or priority. Buttons on the data route cannot overtake earlier data in that direction. The application decides whether latency-sensitive buttons use the control route or another route. A separate control route is not a latency guarantee across the shared ACL.

While connected, SPP provides ordered, reliable transfer and lower Bluetooth layers retransmit radio errors. It does not guarantee matching write/read boundaries, peer-application processing completion, delivery across disconnections or delivery within a fixed time.

**Two routes are not two independent radio links.** RFCOMM channels to one peer share the multiplexer/L2CAP/ACL, controller and airtime. Separating application-stream head-of-line waiting does not eliminate shared-layer congestion, retransmission or insufficient bandwidth. Do not promise low latency before measurement.

## 3. Existing design and evidence

The following descriptions retain the state of the design review, rather than silently rewriting historical limitations as current behavior.

- [Validation scope](validation.md), sections 2–5: separate State / Event / Rule / Action / Engine. Decisions are pure rules, side effects belong to the execution layer, and observation does not mutate.
- [A2DP transition table](connection_state_transitions.md): callback normalization, serialized reentry, bounded history and separation of measured/unmeasured behavior.
- [Validation scope](validation.md): with one peer/one RFCOMM route, initiation/acceptance, actual central/peripheral roles and exact 36 bytes in each direction were measured. Added dual-route/bounded-load results are in section 11. Saturation, endurance and coexistence performance were not yet measured at that point.
- The [A2DP implementation at this design stage](../examples/A2DPSourcePlayer/README.md) exclusively owned CYW43/BTstack/L2CAP/SDP/power. Do not independently begin another SPP class to obtain coexistence.
- Do not change the audio-pipeline architecture in this task. Give SPP its own bidirectional byte API instead of forcing it into the audio-only ITransport.
- Retain [HFP unsupported status](HFP_UNSUPPORTED_REASON_en.md) and the FastStream pause. Adding SPP does not demonstrate LBS microphone support.

## 4. Responsibility split

### Library responsibilities

1. **Shared Bluetooth runtime:** single ownership of initialization, poll, GAP/security, L2CAP, SDP, common events and power.
2. **Multiple SPP route connections:** service registration, initiation/acceptance, peer/endpoint/CID/generation/MTU, per-route disconnection and errors.
3. **Raw byte I/O:** route-specific read/write, send permission, MTU splitting, bounded buffers, receive credits and explicit full/disconnected/failure results.
4. **Observation:** route states, queue occupancy, transfer counts, errors, radio role and transition history. Do not interpret payload semantics.

### Application responsibilities

- Assign routes to audio, control or other purposes.
- Define in-route framing, type, version, format negotiation, SBC/PCM and button semantics.
- Select and prioritize data, drop before sending, adjust quality/production rate and manage playback queues.
- Implement ACKs, request IDs, retries and idempotence where receipt/execution confirmation is required.

The library enforces connection and byte-transfer bounds. It must not guess that application data is “old audio” and discard it, or hide application decisions inside a buffer policy.

## 5. Proposed Sokoban states and transitions

### State

The detailed canonical record is the [SPP transition table](spp_state_transitions.md). It includes runtime begin/initialization failure, shared SDP requests/candidates/completion/failure/timeout/cancellation, and route selection waiting.

Under common runtime state, use a fixed-capacity route-state array. Centralize each route's connection intent, peer/endpoint, CID, generation, MTU, send permission, TX/RX occupancy, offset and latest result. Do not store audio/control classification or message deadlines.

Keep payload in fixed-capacity storage; record ownership, location and length in State/history. Do not copy large audio payloads into every history entry or retain temporary callback pointers.

### Normalization and side effects

Callbacks only normalize Events and copy into bounded storage. Rules return next State and an Action sequence from State/Event without reading BTstack or an external clock directly. Queue synchronous Action callbacks until the current transition is committed. SDK CAN_SEND_NOW can occur inside its request call, making this reentry rule mandatory.

| Condition / Event | Proposed next state / Action | Validation scope at this stage |
|---|---|---|
| Begin / InitResult / initialization timeout | Runtime Stopped → Starting → Ready or Failed; observable error/status and history | Stage 2 host validation, not yet linked to real SDK |
| Discover / ConnectUuid(peer, UUID) | Shared SDP Querying; connect for one candidate, wait for application selection for several | Standard 0x1101/128-bit host validation; radio discovery not yet measured |
| SDP failure / timeout / CancelDiscovery | Explicit result; do not reuse client until quiescent after cancellation | Stage 2 host validation |
| ConnectRequested(route, peer, channel) | Route Connecting; request RFCOMM connection | Two fixed-channel initiations measured in independent probe |
| ListenRequested(endpoint) | Register service/start acceptance | Two endpoints measured in independent probe |
| Incoming, allowed and capacity available | Allocate route, Accept | Two fixed-peer connections measured in independent probe |
| Incoming, disallowed/full | Reject and record reason | Proposed protection |
| Open(status=0) | Store route CID/MTU, Open | Simultaneous Open on two routes measured in independent probe |
| Open failure | Preserve route result without changing other route | Single-route timeout/security failures measured |
| WriteAccepted, permission not requested | Request send permission for that route | Bounded two-route echo buffer measured; generic API/full behavior not yet measured |
| CanSend, matching route generation/CID | Send at most one MTU of bytes | Two-route small payloads/bounded load measured in independent probe |
| SendResult | Update offset/count/failure; preserve subsequent byte order | Distinguish local success from peer processing |
| RxBytes, capacity available | Store in that route's RX area | Split/concatenated/full cases require validation |
| ReadConsumed | Grant credits matching actually freed capacity | Manual credits require validation |
| DisconnectRequested(route) / DLC Closed | Retire only that route; report incomplete bytes/reason | Keeping the other route alive not yet measured |
| ACL disconnect / runtime fault | Retire all affected routes; update generation | Needs multi-route validation |
| TX free 0 → positive | Update writable_epoch and history writable_mask; snapshot shows current free bytes | Stage 2 host validation |
| Old generation / unrelated CID | Reject/observe without advancing state | Proposed CID-reuse protection |

Record initiation/acceptance separately from radio role. Disconnecting one SPP route must not power HCI off, disconnect every GAP link or stop A2DP. Overflow of important events is an explicit fault, not permission to silently continue normally.

## 6. API candidates (names/signatures finalized after review)

Examples should read in this order: configuration → runtime begin → initiate/listen → main-loop read/write → status queries.

- Runtime: one begin/poll owner for initialization and power.
- Endpoint registration: peer policy, server channel and capacity. Purpose names are application-local labels.
- connect / listen / disconnect: explicit operations; proposed first version has no automatic reconnect.
- **tryWrite(route, bytes):** partial acceptance is decided. Nonblocking API returning accepted byte count and result. Copy the accepted prefix during the call; its transmission ownership transfers to the library, so original memory can immediately be reused. The application retains and resubmits the unaccepted suffix in order.
- **read(route, buffer):** returns bytes obtained; no matching write-boundary guarantee.
- snapshot / history: read each route's connection state, writable capacity, RX bytes, incomplete sends and failures without mutation. `txFree()` is current writable bytes. Observe 0→positive through `writable_epoch` and history `writable_mask`; epoch comparison can resume pacing even if history was overwritten.

tryWrite Accepted means “accepted into the library's bounded TX storage,” not sent over Bluetooth or processed by the peer. Candidate results are InvalidHandle, NotConnected, WouldBlock, Accepted and Failed. The partial-acceptance contract is fixed. Record incomplete bytes/reasons on disconnect separately from locally successful sends.

Library byte queues are FIFO. No frame-level discard/replace API is introduced here. If an application splits a message across writes, it must send the remainder of a partially accepted message in order. Disconnect invalidates incomplete bytes and reports counts/reasons, but does not automatically replay across connections.

**Peer SDP discovery is mandatory.** Resolve RFCOMM server channels from both standard UUID 0x1101 and custom 128-bit UUIDs. Offer both `connect(peer, channel)` and `connect(peer, serviceUuid)` (resolve → connect). Observe discovery-only `discover(peer, serviceUuid)` and token-based `cancelDiscovery(query)` as asynchronous operations. These names are proposed product-facade names; the stage 2 core accepts equivalent Events.

When one UUID matches multiple services, return all candidates and do not choose arbitrarily. The application selects a candidate. The initial fixed limit is eight; exceeding it explicitly fails with CandidateOverflow rather than truncating and mistaking one remaining candidate for a unique match.

Several SPP services may all use the standard UUID, so UUID 1101 alone does not reveal purpose. Identification belongs in endpoint configuration or the application contract. Generic terminal software is not guaranteed to use multiple endpoints automatically.

Pairing/authentication/encryption/key persistence requirements are explicit shared-runtime settings. Do not blindly transfer experimental automatic confirmation into a product. The proposed API runs entirely on one main loop; IRQ/other-core use requires a separate specification.

## 7. Backpressure and latency boundaries

### Raw-SPP library guarantees

- Bounded TX/RX. Use WouldBlock/receive-credit suppression when full; never report success while silently dropping bytes.
- Respect RFCOMM CAN_SEND and MTU. Do not reuse one route's send permission on another.
- Manual credits count RFCOMM frames. Reserve capacity for already-granted frames × maximum MTU plus pending processing storage. Do not overgrant based only on currently free bytes.
- Proposed separate per-route buffers and control-event storage prevent one unconsumed route from exhausting the other's RX storage.
- Bound poll/execution work so one route cannot occupy it indefinitely. This is a low-level work bound, not payload-aware application priority.

### Application dropping and priority

The application may drop unsent audio/video at appropriate boundaries and try control-route writes first. Separate SPP routes eliminate the need to insert control frames into a media stream.

However, safe withdrawal of bytes already accepted by the library or BTstack/controller is not promised. Discarding the remainder of a partially sent application frame corrupts the peer parser. Decide what to discard **before handing it over**, using small units, short send queues and limited look-ahead.

Shared-ACL retransmission/head-of-line waiting remains; two routes do not mean “control always arrives first” or “no lag.” The application needs ACKs if peer receipt/execution must be confirmed. Discarding playback after receipt does not reclaim airtime or latency already incurred.

## 8. SDK support and resources for multiple SPP routes

### Findings from source

- BTstack RFCOMM APIs distinguish server channel/CID and independently handle service registration, send permission, credits and disconnection.
- SDK `rfcomm_channel_create_internal` locates an existing multiplexer by peer address and creates another DLC. Duplicate creation of the same peer/DLC is rejected.
- Memory pools include `MAX_NR_RFCOMM_CHANNELS`, `MAX_NR_RFCOMM_MULTIPLEXERS` and `MAX_NR_RFCOMM_SERVICES`.
- `rfcomm.c` can-send dispatch traverses channels of the shared L2CAP in list order, passing tokens to waiting channels with credits. This alone does not guarantee fairness/control priority. Load tests must specify re-request timing, creation order and queued-ahead volume.
- The original PicoSppProbe configuration sets each to one. Preserve it; the separate PicoDualSppProbe and load probe allocate two channels/services for measurements. Four have not been tested.

### Stage 2 static RAM budget

The historical stage 2 configuration uses two routes, 1,024-byte TX/RX each, MTU limit 256, up to four RX credits per route, queue 16, history 16 and eight SDP candidates. `sizeof` was checked from object symbol sizes with `arm-none-eabi-g++ 13.2.1 -mcpu=cortex-m0plus -mthumb`. No dynamic allocation is used.

| Item | Bytes | Scope |
|---|---:|---|
| TX FIFO | 1,024 | Per route |
| RX FIFO | 1,024 | Per route |
| Route state/counters/eight SDP candidates | 192 | Per route |
| **Route subtotal** | **2,240** | **4,480 for two routes** |
| Common State (runtime, endpoints, shared SDP, ACL identity) | 272 | Shared |
| Event queue (72 metadata + 256 temporary RX copy) ×16 | 5,248 | Shared |
| Payload-free transition history 312×16 | 4,992 | Shared |
| Temporary contiguous SDK send buffer | 256 | Shared |
| Queue indices, counters, executor reference, alignment | 56 | Shared |
| **Entire Engine** | **15,304** | **Two routes plus shared storage** |

This is static core-object RAM, excluding BTstack RFCOMM services/channels/multiplexers, L2CAP/ACL/SDP client, CYW43, adapter, application buffers and stack. Add these using stage 3 configuration, linker maps and stack validation. Do not claim “the whole two-route Bluetooth implementation fits in 15,304 bytes.” Changing query/history/queue capacity changes shared cost. Route state includes a 64-byte candidate table.

Grant only the difference between `min(4, floor((RX capacity − unread bytes)/MTU))` and already-issued credits. Check at compile time that the queue holds each route's four-frame window plus eight control entries. Four frames is an initial capacity policy, not a throughput or application-priority guarantee.

### Further checks before the full implementation

- Configuration/state/SDP-record lifetime for two DLCs to one peer; two published endpoints also require two services.
- Total RAM across RFCOMM pools, L2CAP/ACL resources, RX credits, TX/RX storage and history. Increasing configuration values alone does not ensure performance.
- SDK multiplexer send selection/shared queues and the effect of stopping one route's TX/RX on the other.
- Peer-application support for two RFCOMM endpoints/connections. The library cannot invent a second service on a device exposing only one.

Focus current evaluation on one peer/two routes. Testing four requires four accepting services/channels, per-route buffers and shared-multiplexer resource adjustments. Consider capacities extensible to N routes, but do not require four or claim unmeasured connection counts as supported.

## 9. Coexistence with A2DP and AVRCP

These are the proposals/constraints recorded at this design stage:

- Separate A2dpSourcePlayer Initialize, static owner/claimed and lifetime into shared runtime. Do not merely run independent begin calls and remove ownership protection.
- Centralize profile registration around shared initialization; avoid duplicate l2cap_init/sdp_init/power operations.
- Separate A2DP and each RFCOMM send permission/queue and regression-test existing A2DP.
- AudioPipeline is a singleton; configure replaces its route. Adding SPP media must not overwrite the existing A2DP path. An audio bridge belongs in the application/separate adapter; do not redesign the pipeline without authorization.
- The old BTStackAVRCPPlugin opcode parser is a placeholder. Do not assume coexistence with a completed AVRCP implementation.
- Multiple profiles to one peer, an SPP host plus separate A2DP speaker, and Wi-Fi coexistence are separate tests. The A2DP helper uses CYW43_LWIP=0, so Wi-Fi coexistence needs separate consideration.

## 10. Approved requirements and staged validation

### Agreed boundaries and validation scope

- Raw SPP through multiple route handles, providing byte I/O and observation only.
- Measure load/bidirectional behavior for one peer/two routes and decide from results whether more routes are needed.
- Nonblocking partial-acceptance tryWrite, buffer capacity/ownership, explicit disconnect and incomplete-transfer notification contracts.
- Staging that includes shared-runtime changes. Stage 2 is approved; the real SDK adapter/shared-runtime integration is separate stage 3.

### Stages

1. **Add minimal multiple-SPP measurements (execution approved):** supplement original one-route Phase 0 with evidence of two simultaneous routes to one peer. Use synthetic data and do not claim unseen transitions as proven. Present results to settle requirements.
2. **Transition table and pure core (approved, host implementation complete):** map measured events into State/Event/Rule. Host-test API boundaries, generations, credits, queues, failures and reentry.
3. **Shared runtime/SPP API:** readable one/two-route examples, real-SDK builds and A2DP regression.
4. **API-based hardware comparison:** validate concurrency, isolation and load below, then A2DP/AVRCP coexistence.

| Test | Acceptance criteria |
|---|---|
| Pico initiates two routes to one host | Both Open, distinct CIDs, exact bidirectional bytes per route, no cross-routing |
| Host initiates to two Pico endpoints | Both accepted, same exact bidirectional comparison; initiation/acceptance recorded separately from radio role |
| Unknown channel, UUID-only connection | Peer SDP resolves standard 0x1101/custom 128-bit UUIDs, then connects |
| Multiple SPP-like services with the same UUID | All candidates returned; no connection before explicit selection; explicit candidate-overflow result |
| Runtime initialization failure / SDP failure or timeout | State/cause/history distinct from route Open failure; reuse after cancellation only once client is quiescent |
| Splitting/concatenation/MTU spanning | Preserve byte order/content across arbitrary write/read splits |
| Stop consuming one route's RX | Route credits/bounds work; measure other-route effects and shared-layer waiting |
| Disconnect/reconnect only one route | Other route does not close unintentionally; old CID/generation events do not contaminate new route |
| ACL disconnect / partial send / full queue | All affected route states/incomplete bytes/errors agree, no silent loss |
| Synthetic media plus control load | Application ACKs measure control RTT percentiles/max; also record queue occupancy, throughput, CPU/RAM |
| Multiple routes plus A2DP | Measure combined audio gaps/control latency/TX-RX counts, rather than inferring from isolated tests |

The dual-route load comparison uses the same harness for baseline and synthetic-media data-route load. Additional directional tests send playback-like host→Pico only, microphone-like Pico→host only, and simultaneous traffic if possible, measuring ACK RTT/loss/timeouts on the separate control route. Media echo is bidirectional load, not a one-way test. Distinguish limited outstanding-packet conditions from saturation. Four-route testing is optional based on results, not currently mandatory.

Test identifiers and ACKs belong to the **validation application**, not the library wire format. Measure round trips on one clock without assuming time synchronization. Compare frame lengths, production rates and look-ahead volumes; short success is not an endurance or practical-video guarantee.

Host tests include per-route queues, capacity boundaries, partial acceptance, unchanged bytes, duplicate/delayed/synchronous-reentrant CAN_SEND, API failures, CID reuse, generation mismatch, per-route/full-ACL disconnect. Start with synthetic data without real recording/playback; any necessary recording follows the established advance-notice rule.

## 11. Status and reference implementations

**Direction and requirements 1–4 were approved. Stage 2 pure core was followed by stage 3 facade/shared runtime and bounded hardware tests.** Preserve existing A2DP and paused HFP/FastStream work. Measurements, retained firmware and final state are stored in separate evidence files.

### Initial multiple-route measurements

The [Validation scope](validation.md) records Pico acceptor channel1/CID1 and channel2/CID2, and Pico initiator CIDs3/4, both Open status=0 and coexisting without intervening Closed. Each trial sent distinct 36-byte fixtures on each route in both directions; every comparison matched and trace loss was zero. Pico's radio role was peripheral in both trials; initiating is not proof of central role.

### Bounded load measurement

The [Validation scope](validation.md) and raw comparison preserve baseline → loaded → baseline on one connection, approximately six seconds each with 115 verified control round trips per phase.

| Condition | Median control RTT | 95th percentile | Observed maximum |
|---|---|---|---|
| Baseline | 8.155 ms | 14.116 ms | 26.116 ms |
| Data-route echo load | 9.927 ms | 17.412 ms | 35.754 ms |
| Baseline after load | 7.724 ms | 14.927 ms | 31.906 ms |

Media-like data: 617×128-byte round trips, exactly 78,976 bytes each direction (approximately 13.14 kB/s each direction). Control: 16 bytes approximately every 50 ms. Each route allowed only one outstanding request: **bounded bidirectional echo, not saturation or independent one-way tests**. Timeouts, buffer overflow, send errors and trace loss were zero. Firmware queue maxima: 128/16 bytes. ELF text=357020, data=0, bss=54804 bytes are static section sizes, not runtime peak RAM.

Verified final state: Pico radio OFF; host discoverable/pairable/discovery OFF; no ACL; USB/socket handles released. Retained image and restoration target are recorded.

### Requirements settled from measurements (1–4 approved verbatim)

1. Handle **at least two simultaneous SPP byte streams to the same peer**, supporting initiation/acceptance and requiring two endpoints/connections on the peer.
2. Distinguish each route's TX/RX/state/errors; never insert proprietary headers. Data multiplexing/audio/buttons/dropping/priority belong to the application.
3. Validate bounded buffers/nonblocking I/O, explicit full results, generation/CID isolation, and distinction between one-route and whole-ACL disconnection in the formal API.
4. Do not turn these short measurements into latency bounds, maximum throughput or real-time guarantees. Separately validate fake-stack sequences, MTU spanning, per-route disconnect/reconnect, RX pauses, endurance and A2DP coexistence as implementation acceptance criteria.
5. tryWrite's contract includes partial accepted-byte count, copying ownership transfer and application retention of the unaccepted suffix. TX writability is observable through snapshots/change notifications.
6. SDP is mandatory: standard 0x1101/custom 128-bit UUIDs, resolve-to-connect, return multiple candidates and wait for explicit selection; shared runtime owns the SDP client.
7. Distinguish runtime initialization from route Open failure. Normalize requests/results/failures/timeouts, including SDP, into transitions/observation.
8. Initial static per-route RAM: TX 1,024 + RX 1,024 + state 192 = **2,240 bytes/route**, or 4,480 for two. Shared queue/history/SDP and SDK costs are separate (section 8). This is an initial budget, not immutable capacity assurance.

These are approved product requirements, not a claim that an unfinished product API is already guaranteed. Keep short probe, pure-core host-test and product-API hardware evidence separate.

### Stage 2 deliverables

- [SPP transition table and adapter contract](spp_state_transitions.md)
- [SPP pure core](../spp-core/SppCore.h)
- Host tests and execution
- Stage 2 validation record
- [A2dpConnection.h](../audio-core/connection/A2dpConnection.h): pure rules, bounded Engine, observation.
- [A2dpSourcePlayer.cpp](../audio-core/A2dpSourcePlayer.cpp): actual boundary for shared initialization/ownership separation.
- [AudioPipeline.cpp](../audio-core/AudioPipeline.cpp): shared singleton path.
- [ITransport.h](../audio-core/ITransport.h): audio-transmit interface, separate from bidirectional SPP.
- [BTStackAVRCPPlugin.cpp](../audio-core/plugins/BTStack/BTStackAVRCPPlugin.cpp): unfinished old AVRCP component.
- [picow_a2dp_source.cmake](../cmake/picow_a2dp_source.cmake): polling, SDK dependencies and LWIP settings.
- PicoSppProbe: original single-route proof, distinct from the product API.
