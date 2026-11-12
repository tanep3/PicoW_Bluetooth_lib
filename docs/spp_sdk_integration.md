# SPP: integrate RFCOMM byte streams

[日本語](spp_sdk_integration_jp.md)

`spp::Client` provides up to two full-duplex byte streams to one selected peer. Use C++17 and a single main loop; integrate with `picow_target_spp(target config_directory)` from `cmake/picow_spp.cmake`. Start with the [examples](spp_examples.md), then follow the ownership, partial-I/O and SDP contracts below.

## Ownership and API

The shared BluetoothRuntime owns CYW43, L2CAP, SDP server/client, HCI/ACL observation, power, and pairing policy. A2dpSourcePlayer joins the same runtime without duplicate initialization. The A2DP audio pipeline is not replaced for SPP. SDP client ownership is exclusive; avoid conflicts including A2DP's internal SDP. SDK integration itself does not establish successful simultaneous A2DP hardware communication.

Use `spp::Client` as a fixed process-lifetime instance. Callback registrations and SDP-record storage must survive until reset. All APIs/callbacks belong to one main loop; never call directly from IRQ or another core. `begin()`/`poll()` advance asynchronous initialization/processing. Boolean results indicate queue acceptance, not connection success. Observe initialization failures, transitions, results, and lost events with `snapshot()`, `historySize()`, `history()`, and `droppedEvents()`.

- `connect(slot, peer, channel)`: explicit-channel connection.
- `connect(slot, peer, Uuid)`: connection after SDP resolution; supports `Uuid::from16(0x1101)` and 128-bit UUIDs.
- `discover(peer, uuid)`: retrieve candidates only. `cancelDiscovery(token)` requests cancellation.
- Multiple candidates stop in Selecting; the application chooses with `select(handle,index)`. No candidate is chosen silently.
- `listen(slot,channel,uuid)`: register a listener and SDP record.
- `disconnect(handle)`: disconnect that route.
- `tryWrite(handle,p,n)`: copy the accepted range into library storage and return the byte count. Partial acceptance is allowed; retain the unaccepted tail in the application. The caller's pointer is not retained.
- `read(handle,p,n)`: copy received bytes. It guarantees neither matching write/read boundaries nor completion of application processing.

### Retrying tryWrite versus completion

The result is `IoResult{bytes,error}`. Advance the application's offset only by accepted `bytes`; never resubmit them. `WouldBlock` means insufficient TX space. `Busy` means I/O cannot proceed ahead of pending events or Engine dispatch, among other conditions. Neither is inherently fatal. `Engine::tryWrite` returns `{0,Busy}` during pending-event processing/dispatch. Even in one loop, a write to one route can generate an event, making the immediately following write to another route Busy.

Retain the unaccepted tail with the same-generation handle, advance normal processing through `poll()`, then retry. Do not spin on Busy in the same loop or reenter from a callback. Busy can persist after a finite-budget poll; also inspect route state, generation, and runtime failures. On disconnect, generation change, or fatal error, discard the application's unaccepted tail instead of transferring it to a new route. Already accepted bytes belong to the library; counters expose their discard on disconnection.

`tx_accepted` means copied into the library. `tx_sent` means local SDK send success. Neither proves peer receipt/processing; compare peer received counts, echo, or application ACKs separately. `rx_received` is bytes received by this Pico, not bytes received by the peer.

Preserve handle slot/generation, operation tokens, and ACL epoch during normalization. Never relabel a delayed old-CID callback with the current generation. If the SDK rejects disconnect before connection completion, retain Closing. A late successful Opened triggers another disconnect; cancellation alone does not free the slot for reuse.

## Actual SDP events and cancellation

This SDK's `sdp_client.c` emits `SDP_EVENT_QUERY_ATTRIBUTE_VALUE` in the **11-byte legacy BYTE format**: record index at offset 2, attribute ID at 4, attribute size at 6, byte offset at 8, and data at 10. Do not use the generated VALUE getter's contiguous-value interpretation. DE-header notifications have size=0; payload notifications establish the size. The parser handles this transition and validates offset order, final length, bounds, and incomplete termination. The 256-byte attribute bound is an SDP-parser limit, not an SPP payload limit.

Combine attributes by query-local record index: 0x0000 provides the real record handle and 0x0004 the RFCOMM channel. At most eight record/channel candidates are retained. The convenient RFCOMM service callback is not used because it loses record identity. Malformed, incomplete, or over-capacity input must not become a successful candidate.

The SDK has no public SDP cancel API. Cancellation/timeout suppresses results but drains SDK processing until natural QUERY_COMPLETE, retaining shared-client ownership throughout. SdpQuiesced is reported afterward; cancellation does not immediately set busy=false. If StartSdp returns a non-busy error while SDK internal busy remains set, runtime enters terminal `failed()` and powers off. A2DP/SPP both observe that failure; do not retry before reset.

Another SDK difference: RFCOMM CHANNEL_OPENED declares 18 bytes (`packet[1]=16`) but the actual callback length is 17. `SdkEventLength.h` makes an exception only for exact17/decl16 and verifies every used field fits within 17 bytes. This does not permit arbitrary short packets or length mismatches.

## MTU, memory and security

`MaxFrame=1024`; each of the two routes has 1024-byte TX and RX FIFOs. Shared configuration requires `HCI_ACL_PAYLOAD_SIZE <= 1033`. The sample value 1028 bounds outgoing RFCOMM MTU at 1019; negotiation may lower it. At most four frame credits are available, but actual grants depend on unread space and MTU.

Do not estimate total RAM from FIFOs alone. Check the selected build's map/sizeof including event queues, history, runtime, BTstack, SDP records and application buffers. ERTM additionally requires caller-owned storage.

`Security` defaults to `authentication=true`, `allow_confirmation=false`. A test probe's pairing policy is not a product default.

<a id="retained-evidence"></a>
## Validation scope

Short UUID-resolution and two-route echo tests do not guarantee maximum performance or concurrent audio. See the existing evidence linked from [Validation scope](validation.md) for test conditions and prior failures.

## Send deferral and buffer ownership

CAN_SEND_NOW is not an exclusive reservation. Between callback queuing and Action execution, another route may use the shared HCI buffer. The adapter rechecks readiness at execution. Not-ready, full ACL buffers, missing RFCOMM credits, and stopped aggregate flow return `SendDeferred` to the core, preserving TX FIFO, ownership, and accepted/sent counters while requesting another can-send notification with a new token. Permanent failure uses `SendResult`; a later `Closed(status=0)` does not erase its original failure status.

This BTstack's `rfcomm_send()` discards the lower `rfcomm_send_prepared()` result, so `SdkSend.h` uses reserve/copy/prepared-send to retain actual status. Its buffer contract is: success consumes/releases in the SDK; failure leaves ownership with the caller. This was checked for Pico SDK 2.1.1 CYW43 polling/basic RFCOMM, not arbitrary transports. Other transports may release before returning failure, and a synchronous callback may let another profile reserve again, so a simple reserved flag cannot identify ownership. Revalidate this contract when porting. Optional ERTM requires both ERTM feature macros, the generated SPP overlay and caller-owned buffers; follow [ERTM integration](spp_lifecycle.md).

See stage 4 results for bounded hardware reproduction and remaining issues. This validation does not include concurrent A2DP + two-route SPP audio, long-duration stability, or latency/throughput guarantees.


## Later serial-port control extension

See [SPP service lifecycle and controls](spp_control.md) for listener withdrawal, modem/line status and port configuration APIs, their acceptance-versus-completion contract and separate test status. Historical measurements are retained in the development history.

For the later peer-selection and ERTM integration, see [SPP lifecycle](spp_lifecycle.md). Its host/build evidence does not replace radio acceptance.
