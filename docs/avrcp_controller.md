# AVRCP Controller: send media commands

[日本語](avrcp_controller_jp.md)

Use Controller to send media buttons, metadata/capability queries, notification registrations and volume/item controls to configured peers. Follow begin → poll → observe connection → submit command → inspect response. Start with the [media-control example](../examples/AvrcpMediaControlSample/README.md). Limited hardware observations exist, but full-feature and arbitrary-peer interoperability remain unqualified. Browsing and Cover Art use separate facades.

## API and ownership

Include `AvrcpController.h` and use `picow::avrcp::AvrcpController`. Include `cmake/picow_avrcp_controller.cmake` and call `picow_target_avrcp_controller(target config_directory)` in CMake.
The helper includes the Target source too, so the same application may construct either or both role owners. Configure at least four AVRCP connection objects for two peers: BTstack uses a Controller and a Target object per signalling connection.

- `Config` accepts one or two distinct nonzero peer addresses. `begin(config)` is one-shot per owner; `stop()` is terminal. Objects should live for the process lifetime.
- `connect(peer)` explicitly initiates an outgoing connection; incoming connections from configured peers are also accepted. Return zero means the SDK accepted the request, not that a remote connection completed. Observe `Connected` and `bindingForPeer()`.
- `Binding` includes peer, ACL epoch, CID, and a monotonically increasing owner-local generation. `press`, `release`, and `disconnect` reject stale bindings. Two peers have independent slots.
- `press(binding, operation)` accepts all standard no-operand operation IDs declared by the pinned SDK: navigation/menu, numeric/channel, power/media, angle/subpicture, and F1–F5. Reserved IDs and arbitrary vendor-unique operand commands are rejected. `group(binding, next)` supplies next/previous-group vendor-unique clicks separately. It uses BTstack's press-and-hold command: **the SDK repeats the press until release**. Call `release(binding)` even after a short press; do not assume a press automatically produces release.
- `release` may be requested before the press response. In that case BTstack may report only completion, not a separate press response. A command pending on a peer prevents another press there.
- `PressResponse` and `ReleaseResponse` expose the remote AV/C response byte (9 accepted, 10 rejected, 8 not implemented). A zero local API result is not a remote ACK. A rejected press triggers a release request to cancel SDK repetition; its eventual response remains separate.
- Callback execution is synchronous. Mutating API calls made from callbacks are rejected; copy observations into an application queue and act from the main loop. Callback references are only valid during the call.
- `poll()` drives the shared Bluetooth runtime. There is no IRQ/multicore/thread-safe entry point.

## Shared Target/Controller lifecycle

`AvrcpShared` initializes the SDK signalling service and both internal role handlers once. This matters because another `avrcp_init()` clears the SDK connection list even when its L2CAP service is already registered. Both application roles use the single runtime AVRCP slot, not independent radio initializations.

Target and Controller owners have separate SDP records and callbacks. Both can observe one shared signalling channel. Stopping one owner unregisters its record and clears its state; it retains a channel still used by the other role. Stopping a Controller also requests release for a held command. If release fails, it disconnects the signalling channel to avoid indefinite repeats; this safety path affects both roles.
An explicit `disconnect(binding)` disconnects the shared signalling channel and therefore both AVRCP roles on that CID. It does not intentionally disconnect A2DP/SPP or power down the radio.

`AvrcpTarget` also supports `connect(peer)` and `disconnect(binding)`. Outgoing connections use the SDK's serialized SDP query mechanism. The helper rejects requests while shared SPP owns SDP or the SDK reports the SDP client busy. Do not reinitialize SDP or bypass its ownership rules.
Pairing/discoverability remain under the shared application's policy; these APIs do not silently approve pairing or enable global visibility.

## Validation and remaining work

- Shared Controller/Target lifecycle regression. This does not establish hardware negotiation or timing.

- Controller adapter checks: real SDK headers, mock transport; both registration orders, all 128 operation IDs classified as supported/reserved, group commands, query dispatch and parameter validation, press/release responses, rejection, early release, local failure rollback, two peers, stale epoch/generation, malformed packets, stop and initialization failures. ASan/UBSan and warnings-as-errors.
- Target adapter checks: existing Target adapter regression.
- runtime/core checks: runtime/core/probe regressions.
- `examples/PicoAvrcpCompileProbe`: combined ARM compile/link with both roles. It does not initialize the radio by default. Newlib `_getentropy` warning remains.

Unverified: simultaneous real Target/Controller operation, arbitrary peers, long-duration operation, and fault recovery beyond host cases. Limited Controller media-button observations are recorded in [Validation scope](validation.md). An opt-in Controller command deadline is available as described below; pending signalling setup can be canceled as described below. SDK callbacks carry no generation, so replaying old raw callbacks after CID reuse cannot be distinguished. Do not retain and reinject callback packets.
These APIs do not imply full AVRCP compliance.

## Capability, metadata, notification and item adapters

- `query(binding, Query)` dispatches UnitInfo, SubunitInfo, SupportedCompanies, SupportedEvents, PlayStatus, NowPlayingInfo, SettingAttributes, SettingAttributeText, SettingValueText, and ShuffleAndRepeat.
- `notification(binding, eventId, enable)` requests registration or stops renewal for event IDs 1–13 through the SDK. Remote support and cached capabilities control local admission. For an established registration, `false` is deferred until the next CHANGED response; it does not send an immediate unsubscribe frame. Controller overlay v5 returns `ERROR_CODE_COMMAND_DISALLOWED` (12) if registration is queued or sent but still awaiting INTERIM. That result means cancellation was not accepted: the application may retry outside callbacks on a later main-loop iteration, with its own bounded wait policy. A missing INTERIM has no autonomous cancellation timeout here. Fully inactive disable remains a successful no-op. See the [overlay integration guide](avrcp_sdk_overlay.md).
- `elementAttributes`, `settingValues`, `absoluteVolume` (0–127), `batteryStatus`, `addressedPlayer`, `maximumFragments`, `playItem`, and `addItem` validate their parameter bounds before calling the SDK. Metadata attribute IDs and item UIDs are synchronously copied by the pinned SDK; caller storage is not retained by these adapters.
- `setSdkCallback` exposes bounded SDK event bytes with the live Binding. The packet envelope/CID/epoch are checked, but nested variable-length fields are not interpreted: use the supplied size when decoding, and copy data before asynchronous use. Callback mutation is rejected. A local query result remains distinct from a remote response.

## Reviewed player-settings SDK overlay

The CMake helper selects a hash-pinned, build-directory [SDK overlay](avrcp_sdk_overlay.md). Installed SDK sources remain unchanged and retain their original license. Unknown source bytes stop configuration. This fixes the original SDK's missing settings data callbacks, malformed setting pair counts/value-text requests, and character-set count/length/index/byte-order defects. The original defects are independently reproduced by the host test before comparing the patched behavior.

- `displayableCharsets` copies 1–5 charset IDs and sends count plus big-endian 16-bit values. `settingAttributeText` and `currentSettings` accept 1–10 IDs; `settingValueText` accepts an explicit attribute and 1–9 values; `setSettings` accepts 1–5 ID/value pairs. These bounds come from the pinned SDK's 11-byte owned command buffer.
- `shuffleMode` and `repeatMode` use the corrected one-pair serializer. The SDK's legacy no-argument value-text query explicitly queries repeat attribute 2, values 1–4; use `settingValueText` to select other values. Local success still does not imply remote acceptance.
- These additional methods return unsupported when built without the overlay, rather than invoking the known defective original serializers.
- `setSettingsCallback` delivers `SettingsChunk` with the live Binding, response, PDU, total byte count, offset and borrowed bytes. Copy data during the synchronous callback, concatenate by offset, and use `final()` before decoding the complete response. Payload fields preserve AVRCP network byte order. Empty accepted/rejected payloads still produce one callback.
- Local subevent `0xf0` has an 11-byte event header: HCI event code, event length, subevent, CID LE16, AV/C response, PDU, total LE16, offset LE16, then at most 246 payload bytes. It is a repository extension, not an upstream BTstack event. The overlay checks complete list/pair/text lengths, transaction label and requested PDU before delivering any chunks. The facade checks chunk bounds/order and live connection; it does not separately parse nested payload fields.
- Controller overlay checks compiles the real SDK parser and serializers with ASan/UBSan, tests valid and truncated/mismatched responses, exact request bytes, chunking, deterministic generation and fail-closed hashes. The facade suite runs with overlay enabled and disabled. These are host tests, not radio interoperability evidence.

Vendor-PDU settings fragmentation supports START/CONTINUE/END with copied storage for two peers, up to 4096 bytes per complete response. Each continuation uses a fresh transaction label and the original PDU ID. No partial data is emitted. The configured `maximumFragments` budget includes START; exceeding it, exceeding storage or an invalid completed aggregate sends AbortContinuingResponse and clears the partial result. Stop, ACL reset and signalling release clear ownership. Applications using the C extension without the facade must call `picow_avrcp_controller_forget_settings(cid)` on release. Missing fragments can be bounded by the opt-in command deadline below; otherwise explicit disconnect is required. This is distinct from AVCTP transport fragmentation. PID/company/PDU/transaction mismatches are rejected without consuming the settings transaction.

Custom commands use bounded owned buffers, transport fragmentation, vendor continuation and registered-notification interleaving as described below. Browsing Controller/Target and Cover Art Client use separate facades and contracts. Cover Art radio acceptance remains open.

The Target role's separately implemented metadata/notification APIs are documented in [Target feature coverage](avrcp_target_features.md). Role claims remain reserved after destruction, so recreating an owner does not reset generation protection within a running process.

The [Browsing guide](avrcp_browsing.md) documents its separate transport, bounds and validation limits.

[Cover Art Client coverage](avrcp_cover_art.md) includes the pinned SDK client APIs; the SDK does not provide an image server.

[Validation scope](validation.md)

## Command deadline and pending connection cancellation

`setCommandTimeout(milliseconds)` accepts zero (default disabled) through `0x7fffffff`. Call `poll(now_ms)` with monotonic uint32 milliseconds, with less than 2^31 ms between timed polls. The first timed poll observing a busy SDK command or pending press/release starts an absolute budget; data/fragments do not extend it. Settings changes apply when a new budget is armed. `poll()` services the runtime and custom send retries without advancing time. Held buttons with no pending response and idle registered notifications do not start a budget; custom INTERIM awaiting CHANGED remains busy and can expire. Callback-time mutation is rejected.

Expiry emits `EventKind::Timeout` once, quarantines the binding and requests shared signalling disconnect, affecting both roles and dependent transports. A failed local disconnect is retried; an accepted disconnect waits for actual release. It never reuses a live transaction to avoid a late-response collision. Deadlines request teardown, not guaranteed release by a fixed wall-clock time.

Both Controller and Target expose `cancelConnection(peer)`, `cancellationPending(peer)` and `cancellationStatus(peer)` for an allowed peer's pending signalling setup. Cancellation applies to the shared attempt, not just one role. Zero return means accepted and may complete synchronously; while pending, do not reconnect or free associated resources. Queued/active SDP retains its shared callback/ownership until completion. Pending L2CAP waits for OPENED failure or late success followed by disconnect. Canceled setup never emits a successful Connected; RELEASED is emitted only after pair cleanup. Deferred lower disconnect failure is exposed by status, remains pending, and can be retried with cancelConnection. A missing connection yields pending=false/status=0; query after your accepted cancellation, not as proof that a new unrelated attempt succeeded. An already established ordinary connection is rejected by cancelConnection; use disconnect(binding). Attached browsing also prevents the low-level cancellation extension without side effects. Finish browsing teardown first.

The facade has no autonomous connection-attempt timer. An application can enforce its own setup deadline by calling cancelConnection from the same main loop. Keep polling and checking cancellation status until cleanup; an accepted cancel is not immediate resource release. Callback-time cancellation is rejected. Without the generated overlay, cancelConnection/status return unsupported. The raw extensions are `picow_avrcp_cancel(cid)`, `picow_avrcp_cancel_pending(cid)` and `picow_avrcp_cancel_status(cid)`.

## Owned custom vendor commands

`customCommand(binding, command_type, subunit_type, subunit_id, pdu, company, bytes, length)` copies requests into static two-peer storage. It supports command types 0–4, subunit types 0–31, subunit IDs 0–7 and 24-bit company IDs. Requests are at most **502 parameter bytes**, fitting the 512-byte AV/C frame with its 10-byte vendor header; zero-length input is valid. Transport MTU must be >=48. The owned sender uses AVCTP SINGLE or START/CONTINUE/END at min(MTU,512), including the first fragment's packet count. Failed sends do not advance the offset, and poll retries the same frame. Inputs may be freed after return. Without the overlay this API returns unsupported. Arbitrary uint16-sized vendor commands are not promised by this AV/C frame contract.

`setCustomCallback` receives `CustomChunk` with binding, response, original PDU, total, offset and borrowed bytes. Local subevent `0xf1` is a repository extension. The overlay checks PID, label, response bit, subunit, opcode, company, PDU and exact parameter length before completion. It reconstructs AVCTP fragments into 4096 AV/C bytes (4086 parameter bytes per response fragment). Vendor START/CONTINUE/END responses are additionally concatenated into a separate 4096-byte parameter buffer. Continuation requests preserve company/subunit/original PDU, use fresh labels that avoid tracked notification labels, and honor the SDK maximum-fragment budget including START. No partial vendor aggregate is delivered. Capacity/budget failures send AbortContinuingResponse; rejection/not-implemented of a continuation is reported against the original PDU. INTERIM retains the transaction; final/CHANGED completes it.

`EventKind::CustomError` reports a local failure separately from a remote result: event.operation is the original PDU; event.response is 2 (capacity), 3 (fragment budget), or 4 (send failure). No synthetic remote success is generated. Send failure is retryable; an abort waits for its remote response or command deadline/disconnect. Malformed/mismatched responses do not complete a command. Raw C clients must service `picow_avrcp_controller_custom_poll(cid)` for failed-send retries and call `picow_avrcp_controller_forget_custom(cid)` only after confirmed release. Retained request bytes are never erased during cancellation/owner destruction.

Already-sent standard notification registrations are tracked by event ID and label. Valid interim/changed/rejected notifications can be dispatched through the SDK while custom fragments or continuation are pending, without changing the custom transaction. Renewals are queued until custom completion. Bounds and identities are checked; ambiguous rejection labels do not retire another subscription. This does not semantically decode every SDK notification payload. Custom commands using a numeric standard PDU still retain their own company/PDU path instead of accidentally invoking settings/metadata parsers.

Custom-command host regressions check MTUs 48–512, retry, continuation, notification interleaving and two-peer bounds. They do not qualify radio interoperability or timing.

For included sample builds and runnable checks, see the [build guide](build_guide.md#compile-only). Those checks do not rerun every feature-specific validation described here.
