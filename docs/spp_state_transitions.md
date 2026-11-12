# SPP: state transitions and pure core (stages 2–4)

**Implementation and acceptance record for SPP stages 2–4, not a first-use walkthrough.**

[日本語](spp_state_transitions_jp.md)

This table corresponds to `spp-core/SppCore.h`. The real SDK adapter, shared runtime, product API, and limited hardware tests added in stage 3 are described below and in the SDK integration record.
Stage 4 send deferral, failure-status retention, and hardware results through the API are also reflected here.
Passing core host tests must not be interpreted as a guarantee of wireless operation.

## Source of truth, correlation, and side effects

- `State` owns runtime, two routes, two endpoints, and the shared SDP query. Payload lives in separate fixed storage.
- Runtime states are `Stopped / Starting / Ready / Failed`. A `begin` failure is distinct from a route Open failure.
- Route states are `Closed / Resolving / Selecting / Connecting / Open / Closing`.
- SDP states are `Idle / Querying / Complete / Failed / TimedOut / Cancelled`. `busy` means ownership of the SDP client and remains true after timeout/cancellation until adapter `SdpQuiesced`.
- A route handle is `(slot, generation)`. Callbacks also check cid and operation token. ACL correlation uses the shared runtime's monotonically increasing link token and HCI connection handle, so an old ACL disconnection is not applied after reconnection to the same peer. The adapter retains request-time tokens when normalizing events; it does not attach the current generation to an old cid.
- Rule is only `(State, Event) -> Transition`. Time arrives as monotonic `Tick` data.
- Engine commits the next State and finishes copying payload before executing Actions. Synchronous callbacks enter a fixed queue and are processed after the current Action sequence finishes.
- `poll(budget)` returns after a finite amount of work. `tryWrite/read` during side effects, or I/O overtaking pending events, returns `Busy`. The main loop owns the API; do not call it directly from IRQs or another core.

## Transition table

| Previous state / condition | Event | Next state / Action | Validation category |
|---|---|---|---|
| Stopped | Begin | Starting, Initialize(runtime token) | Host-tested protection |
| Starting, matching token | InitResult(0) | Ready | Host-tested protection |
| Starting, matching token | InitResult(failure) / initialization deadline | Failed, cause/status, QuiesceRuntime | Host-tested protection |
| Ready, free route | ConnectChannel(peer, channel) | Connecting, increment generation, Connect | Two connections measured by independent probe; core host-tested |
| Ready, free route, SDP free | ConnectUuid(peer, uuid) | Resolving, shared query=Querying, StartSdp | Host / stage 3 hardware |
| Ready, SDP free | Discover(peer, uuid) | query=Querying, StartSdp | Host; not measured over radio |
| Querying, matching query token | SdpCandidate(record, channel) | Store up to eight candidates; do not duplicate the same record/channel | Host |
| Querying, matching query token | SdpComplete(0) | Complete, release ownership; zero candidates=NotFound, one=Connect, multiple=Selecting | Host / stage 3 hardware for one and multiple candidates |
| Selecting, matching handle/query token/index | SelectCandidate | Connecting with application-selected candidate, Connect | Host / stage 3 hardware |
| Querying | SdpComplete(failure) | Failed, route resolution failure; preserve other routes | Host |
| Querying, deadline / candidate overflow / cancellation | Tick / SdpCandidate / CancelDiscovery(token) or Disconnect | TimedOut / Failed / Cancelled, CancelSdp, retain busy | Host |
| After SDP cancellation, matching token | SdpQuiesced | busy=false; retain result; permit next query | Host |
| Endpoint unregistered, no duplicate channel | Listen | Registering, RegisterService | Host / stage 4 hardware |
| Registering, matching token | ListenResult | Listening or Failed | Host / stage 4 hardware for Listening |
| Listening, specified peer, free route, no duplicate connection | Incoming | Connecting, increment generation, Accept | Host / stage 4 hardware through public API |
| Disallowed peer / full, new valid cid | Incoming | Reject, record reason | Host |
| Already-owned cid / cid=0 / token=0 / allowed peer's stale ACL token | Incoming | Increment rejection count only; do not Reject existing connection | Host |
| Connecting, matching handle/peer/channel | Opened(0, cid, mtu) | Open, reserve receive credits, GrantCredits | Host / stages 3–4 hardware |
| Connecting | Opened(failure) | Closed, retain status; other route unchanged | Independent probe / host |
| Open, TX space available | WriteRequested(n) | Accept min(n, free); RequestCanSend if not already pending | Host |
| Open, matching handle/cid/token, permission pending | CanSend | Send min(TX, MTU); readiness notification, not buffer reservation; reject duplicates | Host / stages 3–4 hardware |
| Awaiting Send, matching handle/cid/token | SendResult(0) | Advance FIFO by sent bytes; RequestCanSend again if needed | Host / stages 3–4 hardware |
| Awaiting Send, matching handle/cid/token | SendDeferred | Preserve FIFO/ownership/tx_sent, clear sending; RequestCanSend with new token; result WouldBlock | Host / stage 4 hardware |
| Open, receive reservation available | RxBytes | Consume one credit, store bytes in RX FIFO | Host / stage 4 hardware RX pause |
| Open | ReadRequested(n) | Return min(n, RX); regrant only frames with room for a full MTU | Host / stage 4 hardware RX resume |
| Awaiting credit grant, matching token | CreditResult | Keep reservation on success; terminate route on failure | Host |
| Open / Connecting | Disconnect | Closing; discard unfinished TX/unread RX with counts; Disconnect / CancelConnect | Host / stage 4 hardware single-route disconnection from Open |
| Closing, late connection success | Opened | Disconnect late cid; do not resume writes | Host |
| Matching handle/cid | Closed | Closed, retain unfinished counts/cause; status 0 does not erase earlier nonzero status; preserve other route | Host / stage 4 hardware route isolation |
| Specified peer, new link token | AclOpened | Store ACL handle/link token; reject duplicate while active | Host / stage 4 hardware reconnection |
| Specified peer, matching ACL handle/link token | AclClosed | ACL inactive; close remaining affected routes, cancel related SDP; preserve already-Closed routes | Host / stage 4 hardware disconnection without SDP |
| Nontransient SDK send failure / credit / permission API failure | Corresponding Result | Closing for affected route, unfinished counts, Disconnect; transient send unavailability uses SendDeferred | Host |
| Runtime fault / queue overflow / clock regression | RuntimeFault / QueueOverflow / Tick | Runtime Failed, invalidate all routes, QuiesceRuntime | Host |
| Stale generation/token/cid or wrong state | Any notification | Record rejection count/reason; do not advance connection or byte counts | Host |

The SDK adapter always returns outcomes of `Initialize`, `StartSdp`, `RegisterService`, `RequestCanSend`, `Send`, and `GrantCredits` as events.
For `RequestCanSend`, success is reported by `CanSend`; only failure produces `CanSendFailed`. Transient unavailability at Send execution is `SendDeferred`.
`SendDeferred` is appended as Kind ID 29 without changing IDs 0–28.
SDP completion means no more callbacks will arrive. Cancellation requires `SdpQuiesced` after SDK completion is confirmed; timeout alone does not permit client reuse.
The shared runtime normalizes HCI Connection Complete into `AclOpened`, assigning a nonzero link token greater than the previous token.
`Incoming`, successful `Opened`, and `AclClosed` retain that ACL token. Reusing an HCI handle never reuses its old token.
On counter exhaustion, quiesce and reconstruct the runtime rather than wrap. ACL handle 0 is valid. Use tokens associated when events were generated, not the current token retroactively attached to old callbacks.
The shared runtime also assigns an ACL-specific nonzero token to Incoming from disallowed peers.
The core does not begin tracking that peer; it returns `Reject(token, cid)` for a valid unowned cid.
Before Reject, the adapter verifies that token/cid still identify the same live ACL/DLC and does not decline an expired connection.
Disallowed peers can be rejected even when the allowed peer has no ACL. Duplicate notifications for an already-owned cid never produce Reject, regardless of peer.
Terminal `Closed` for `CancelConnect` is returned only after the SDK connection request is quiescent. Until then, late `Opened` is disconnected while remaining Closing; do not reuse a closing slot.

## Byte ownership and observation contract

`tryWrite(handle, bytes, n)` is nonblocking and returns `IoResult{bytes, error}`.
The returned bytes count is the accepted prefix, which may be partial. That prefix is copied during the call and transmission responsibility/ownership transfers to the library.
The caller's pointer is not retained, so accepted source memory can immediately be reused.
The application retains the unaccepted suffix and submits it after preceding bytes. Accepted is not an ACK of peer delivery or execution.
`WouldBlock` (insufficient TX space) and `Busy` (pending events/dispatch etc.) mean a temporary wait while retaining the suffix.
Engine returns `{0,Busy}` during pending events or dispatch. Even in one main loop, a route 0 write can generate an event and make the immediately following route 1 write Busy. Snapshot TX space alone does not guarantee acceptance.
Advance the send position only by accepted `bytes` and retry the remainder after ordinary `poll()`. Do not discard the remainder as fatal, resend accepted bytes, or spin without polling.
Associate unaccepted data with the handle's generation; discard it on disconnect, generation change, or fatal error rather than carry it into a new route.
Busy includes pending overflow handling, so check runtime/route faults after poll; it is not grounds for indefinite retry.
`read` is also a copying API. It does not restore write boundaries or transform FIFO bytes.

`snapshot().routes[i].txFree()` is writable byte capacity when Open and zero otherwise.
A transition from zero to positive (opening or recovery from full) increments `writable_epoch` and sets the route bit in a history entry's `writable_mask`.
Comparing epochs reveals regained writability even after history overwrite. Snapshot remains authoritative for current free space.
Distinguish `tx_accepted / tx_sent / tx_discarded` and `rx_received / rx_read / rx_discarded`.
`tx_accepted` counts copies into the library; `tx_sent` counts SDK local send success. Neither proves peer receipt/processing. Check peer receive totals, echo, or application ACK separately.
`rx_received` is bytes received on this Pico, not on the peer.

Shared buffer/credit state may change between CanSend and Send. The adapter checks again during execution and returns `SendDeferred` if temporarily unable to send.
The core retains accepted bytes without advancing FIFO and requests new readiness with a new token. The application does not resubmit those bytes.
BTstack 1.6.2 `rfcomm_send` hides the internal send return value, so `spp-sdk/SdkSend.h` uses reserve/copy/`rfcomm_send_prepared` results.
On nonzero return it releases the buffer on the assumption that caller ownership remains.
This covers validated CYW 43 polling/basic RFCOMM only, not arbitrary transports.
ERTM via `ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE_FOR_RFCOMM` is rejected at compile time.

## SDP UUIDs and selection

Use `Uuid::from16(0x1101)` for standard UUID 0x1101 and a 128-bit array in canonical network byte order for a custom UUID.
The adapter queries the peer's actual SDP and normalizes record handle/RFCOMM channel into Candidate. The core does not interpret UUID semantics.
More than eight candidates produces `CandidateOverflow` and failure; never truncate and treat the result as unique.
Candidates live in `query.candidates` and, for UUID connect, also in `route.candidates`, preserving pending application selection when another query starts.
Different records pointing at the same channel remain separate candidates. Simultaneous duplicate connections to the same peer/channel are rejected.

## Required stage 3 checks

1. Resolve unknown channels through SDP and connect for both standard and custom UUIDs.
2. Return every candidate from a peer publishing multiple services with the same UUID, without connecting before selection.
3. Correctly associate SDK SDP callbacks/cancellation/late RFCOMM events with request tokens.
4. Measure small MTU, paused RX consumption, API failures, single-route reconnection, endurance, and A2DP regression/coexistence on hardware.

## Fixed capacities and RAM

Stage 2 configuration used two routes, 1, 024-byte TX and RX per route, maximum RFCOMM payload 256 bytes, and at most four simultaneously granted receive frames per route.
The frame window limits credits as well as byte capacity. Even with small MTU, the queue can hold two routes × four granted frames.
Engine requires at least 16 queue entries: eight data slots plus eight control slots; capacity can be increased.
Unbounded control-event bursts are not supported; overflow is an explicit runtime fault. Values also fit the SDK's 8-bit credit argument.
Stage 2 assumed the adapter could be configured to at most 256 bytes, but the real SDK outgoing API did not permit that.
Stage 3 expanded MaxFrame to 1024. With current HCI payload 1028, 1019 is a calculated upper bound, while actual MTU is peer-negotiated. Stage 4 incoming measurements yielded 1016.

Cortex-M0+ ABI static size is Route 192 + payload 2, 048 = **2, 240 bytes per route**.
The stage 2 default Engine (two routes, queue 16, history 16) was **15, 304 bytes**. See [design §8](spp_generic_protocol_design.md#8-sdk-support-and-resources-for-multiple-spp-routes) for breakdown and exclusions.

## Stage 3 addendum

The [SDK integration record](spp_sdk_integration.md) describes current API and callback normalization.
The 15, 304-byte figure is historical stage 2 data. Current ARM Engine is 28, 616 bytes; host Engine is 28, 632 bytes.
Route+Payload remains 2, 240 bytes. QueuedEvent is 1104, State 656, and HistoryEntry 320 bytes.
Cancelled SDP stays busy until natural QUERY_COMPLETE. If SDK disconnect is rejected before connection completes, Closing persists and a late Opened is disconnected again.
Hardware confirmed unknown-channel resolution for standard/custom 128 UUIDs and 1537-byte exact echo, plus explicit selection waiting for multiple candidates and route 0 echo.
The additional route 1 initially terminated runtimeFailed with status 86 because L2CAP pool 3 was insufficient. After expansion to pool 6, all three trials passed.
Both routes were explicitly selected and opened at cid3/channel 19 and cid4/channel 23, each with 1537-byte exact echo. Core error=0 and dropped_trace=0; the entire session lasted approximately 12 seconds.
If a non-busy StartSdp failure leaves the SDK busy, runtime failed/powerOff is terminal with no retry.
A2DP/SPP linked together in the real SDK; simultaneous hardware audio had not been verified at that stage.
Shared HCI payload is explicitly unified at 1028 with a compile-time guard requiring MaxFrame+9 or less. A2DP-only config1695 is unchanged.
Postflight reconfirmed no final ACL and disappearance of the device. Saved session/console/postflight evidence is listed in the [SDK integration record](spp_sdk_integration.md#retained-evidence).

## Stage 4 addendum

The [hardware validation through the API](spp_hardware_validation.md) and stage 4 evidence record the accepted scope.
`attempt3` passed every selected check in approximately 23.06 seconds: two public-listen endpoints on channels 30/29, single-route disconnect/reconnect, old-handle rejection, RX pause/resume in both roles, 209384-byte exact bulk alongside 100 control echoes, and ACL disconnect/reconnect.
Each paused RX stalled at MTU 1016, unread 1016 bytes, credits 0 while the other route continued control echo.

During ACL disconnection, two RFCOMM `Closed` notifications arrived before `AclClosed`.
Both routes were already Closed and retained error/status 0; only ACL became inactive.
Do not treat that order as a fault. Judge all-Closed state, old-socket termination, new ACL token/route generation, and reconnection echo.
The rule preserving an earlier nonzero failure status across `Closed(status=0)` is a separate protection. A nonzero Closed status takes precedence when present.

At completion Pico HCI was OFF, no target ACL remained, and host Paired/Bonded/Trusted/Connected were all no. The BlueZ device object remained.
Do not infer endurance, forced small MTU, SDP cancellation, simultaneous hardware A2DP, arbitrary transports, or ERTM success from this measurement.

A separate attempt4 also passed every check (181216 exact mixed bytes, 100 controls, 24 SendDeferred events).
Post-fix UUID regression reconfirmed standard→channel 19, custom→23, explicit selection among 19/23, and 1537-byte exact echo each.
Final host 31 passed; audio/connection regressions and combined relink succeeded. Numbers and originals are in [stage 4 validation](spp_hardware_validation.md).
