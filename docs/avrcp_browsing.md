# AVRCP Browsing Controller and Target integration guide

[日本語](avrcp_browsing_jp.md)

This is a bounded common-transport and Controller implementation, not full AVRCP or radio acceptance. Browsing Target request/response APIs now share the same owner; Cover Art Client is covered in a [separate integration guide](avrcp_cover_art.md); image serving is not supplied by the pinned SDK. Existing signalling Target/Controller and unrelated profiles are not reinitialized or stopped.

## Setup and API

Include `AvrcpBrowsing.h`; use `picow::avrcp::AvrcpBrowsing`. The original `AvrcpBrowsingController` name remains source-compatible and names the same shared owner. The `picow_target_avrcp_browsing(target, config_directory)` CMake helper includes signalling Controller/Target and the reviewed AVRCP/L2CAP overlays. Set `ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE` and allocate four `MAX_NR_AVRCP_BROWSING_CONNECTIONS` for two peers. Reserve one additional L2CAP service and two channels. See `examples/AvrcpBrowsingCompileProbe` (radio off by default).

Start signalling first with `AvrcpController::begin(config, true)` to advertise Controller browsing, then call the browser's `begin(config)` for Controller-only or `begin(config, true)` to enable Target requests too. For Target service advertisement, also use `AvrcpTarget::begin(config, true)` on the signalling Target. Both calls and owners are process-lifetime, one-shot. Incoming connections require an allowed peer with a live ACL; duplicates and unknown peers are declined. `connect(peer)` starts outgoing browsing over an existing signalling connection, checks shared SDP ownership and returns local request status. It does not imply remote success.

- `bindingForPeer` returns peer, ACL epoch, browsing CID and generation. Commands reject stale, closing or busy bindings.
- `folderItems(binding, scope, first, last, attributes)` covers media players, virtual filesystem, search results and now-playing scopes. Attribute masks use bits 1–8; zero means ALL, `AVRCP_MEDIA_ATTR_NONE` means none for this command.
- `setBrowsedPlayer`, `totalItems`, `changePath` (up/down with 8-byte UID), `itemAttributes` (UID/counter/attribute mask/scope) and `search` expose the other Controller commands. Item attributes reject NONE rather than silently treating it as ALL. Search is 1–19 bytes, matching the SDK-owned storage; longer requests are rejected instead of truncated.
- Callback events are Connected, Disconnected, Response and Error. Response bytes start with the **remote browsing status** (success is 4), followed by the original network-byte-order payload. `Event::status == 0` is local processing success, not remote acceptance. Raw payload references are borrowed only during the callback. Copy before asynchronous use. Mutating calls from callbacks are rejected.
- `disconnect` closes the shared browsing channel for both roles on that CID, not signalling or other profiles. `stop` is terminal and requests asynchronous browsing disconnects, without global deinit or radio OFF. `poll` drives the shared runtime and checks ACL liveness.

## Ownership and parsing

Two static 10,000-byte ERTM buffers survive owner destruction until the transport closes. Their usable sizes account for SDK alignment/state layout and cap MPS at `HCI_ACL_PAYLOAD_SIZE - 8`. Configuration is mandatory ERTM, MTU 512, four RX/TX slots, FCS, 2000/12000 ms retransmission/monitor timers. The L2CAP negotiation overlay is required. A failed/pending connect with a retained SDK CID keeps its buffer reserved; it is never prematurely recycled.

The pinned SDK Browsing Controller parser can report a callback length larger than its truncated item storage. This facade does not expose that parser: it retains the SDK request serializers and validates/reassembles responses in owned 4096-byte storage per peer. AVCTP SINGLE/START/CONTINUE/END packets are checked for transaction label, PID, packet count, final length and expected PDU. Complete response envelopes, folder-record lengths, text/folder lengths and attribute lengths are checked before callbacks. Nested media-item semantics remain raw application data, not unchecked iterator objects. Large records are preserved within the aggregate bound. Bad/oversized responses report Error, do not report success, and leave the command pending until a valid response or explicit disconnect. Response timeouts are disabled by default. Enable them with `setTimeouts` and timed `poll(now_ms)` as described below.

SDK request fixes are generated from pinned source hashes without editing installed originals: empty common-transport packets are rejected before reading the header; service registration failure is exposed to `begin`; the no-attributes folder request sends the correct count and parameter length. The original BlueKitchen/Raspberry Pi licenses still apply to generated SDK sources. See [overlay licensing](avrcp_sdk_overlay.md). The response adapter is repository-authored code.

## Target requests and responses

One shared browsing owner installs both direction handlers. Controller outgoing transactions and Target incoming transactions have independent state on each peer; no second service or transport instance is created. Target-only use is possible after signalling initialization. `setRequestCallback` receives a borrowed request payload (PDU parameters in network byte order) and a `RequestToken` containing the live Binding, sequence, transaction label and PDU. Copy the token/data and respond later from the main loop: callback-time mutation is rejected.

Use `respondSetBrowsedPlayer`, `respondFolderItems`, `respondTotalItems`, `respondChangePath`, `respondItemAttributes`, `respondSearch`, or `reject`. The generic `respond(token, status, payload, length)` accepts payload excluding the status byte. Successful replies must match the PDU's structure; error replies have no additional payload. All input bytes are copied synchronously into owned storage. Stale/modified/duplicate tokens, cross-peer tokens, a second response and callbacks that try to mutate state are rejected.

- Set-browsed-player folder records are BE16 name length plus name bytes, repeated by depth. The method supplies counter, item count and charset.
- Folder-item records are item type (1–3), BE16 body length, then the encoded body. Record boundaries are checked; application-specific nested item semantics remain the application's responsibility.
- Item-attribute records are BE32 attribute ID, BE16 charset, BE16 text length and text bytes, repeated by count.
- Incoming requests are validated for all six PDU layouts, scope/direction, attribute IDs, counts, ranges and search-string length before callbacks. Fragmented requests use independent 4096-byte storage. There is one outstanding inbound transaction per peer; additional requests while awaiting or sending a reply do not overwrite it. Controller work may proceed concurrently.
- Response storage holds at most 4096 bytes including the PDU/length/status header (4092 bytes of application payload before any typed-method prefix). Replies use AVCTP SINGLE or START/CONTINUE/END with the original transaction label, bounded by remote MTU and a local 512-byte frame ceiling. Caller buffers may be changed or freed immediately after a successful API return.
- A can-send callback advances the offset only after `l2cap_send` accepts that frame. Local send failure retains the identical frame and retries from `poll`; explicit disconnect remains available. Controller send requests are rescheduled after the final Target fragment. `TargetResponseSent` means all frames were accepted locally, **not** an application acknowledgment from the peer.
- Stop/release invalidates tokens and prevents late application replies. Static transport buffers survive asynchronous close. SDK close events carry no epoch; applications must not retain and reinject old raw callbacks. Optional deadlines are described below; applications may also reject a request or disconnect an unresponsive peer. If Target handling is disabled or no callback is installed, complete unsupported requests receive General Reject.

Successful Controller responses also synchronize the SDK UID counter, so a subsequent ChangePath uses the counter returned by SetBrowsedPlayer, GetFolderItems, GetTotalItems or Search rather than an obsolete value.

## Optional deadlines

`setTimeouts(connection_ms, response_ms, target_ms, assembly_ms = 0)` sets budgets for new outgoing/incoming connections, outgoing Controller commands, and completed incoming Target requests (including automatically generated General Reject replies). All default to zero (disabled); values above `0x7fffffff` and callback-time changes are rejected. Existing admissions retain their captured budgets. Target's budget covers application response waiting and all reply fragments/retries together; the optional fourth budget separately bounds incoming request assembly. It is captured only when a valid first fragment is accepted, including when Target is disabled. Accepted continuation progress and rejected/interleaved traffic never renew or cancel this budget. Valid completion clears it and admits the separate Target budget; teardown clears both. Existing three-argument calls remain valid and disable assembly timeouts. An assembly timeout reports the PDU from the accepted first fragment.

Call `poll(now_ms)` with monotonic milliseconds modulo 2^32, at intervals below 2^31 ms. The first timed poll after each admission arms its budget, so call promptly after starting work. Expiration occurs at elapsed time >= budget; progress does not extend it. The source-compatible `poll()` services work and close retries without advancing time.

A deadline emits one `Error` with `Timeout` (`-1004`) and the binding (PDU zero for connection timeout). It quarantines and closes the entire shared browsing channel for that peer, invalidating both CT work and Target tokens; signalling and the other peer are unaffected. This avoids reusing a transaction label while a late reply could still arrive. Failed disconnect attempts retry on subsequent polls without duplicate timeout notifications. Bindings, late replies and can-send events are rejected while quarantined. SDK-retained ERTM storage is kept until connection failure/release confirms teardown; no synthetic release or forced buffer reuse occurs. A late successful connection is closed rather than exposed to the application.

## Validation scope

This feature has host-model and ARM compile/link coverage. Real-peer interoperability, ERTM radio transfer, simultaneous profile load and hardware-fault recovery remain unverified. A Cover Art image server is not provided.

[Validation scope](validation.md)
