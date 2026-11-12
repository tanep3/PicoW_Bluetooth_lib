# AVRCP Cover Art Client integration guide

[日本語](avrcp_cover_art_jp.md)

The pinned BTstack supplies a **Cover Art Client**, with connect, thumbnail/image/properties retrieval and disconnect APIs. It does not supply a Cover Art image server. This integration guide implements that client scope; it does not claim an image-serving Target or completed radio acceptance.

## Setup and API

Include `AvrcpCoverArt.h` and use `picow::avrcp::AvrcpCoverArt`. CMake: include `cmake/picow_avrcp_cover_art.cmake`, then call `picow_target_avrcp_cover_art(target, config_directory)`. Enable `ENABLE_AVRCP_COVER_ART`, `ENABLE_GOEP_L2CAP` and `ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE`. Reserve two additional outgoing L2CAP channels for two peers. The helper selects reviewed Cover Art/GOEP and L2CAP overlays only for that consumer; installed SDK files remain unchanged.

Start the shared AVRCP signalling owner first. `begin(config, max_object_bytes)` accepts one or two allowed peers and a positive byte limit (default 16 MiB per object). Owners and initialization are process-lifetime and one-shot. `connect(peer)` requires an allowed peer with a live ACL and existing signalling state in the SDK; it checks shared SDP ownership. Local success is not remote connection success. Observe Connected and obtain `bindingForPeer(peer)`.

- `thumbnail(binding, handle)`, `image(binding, handle, descriptor)` and `properties(binding, handle)` map to all three SDK GET operations. Handles must be exactly seven ASCII digits plus NUL. Descriptor is optional: NULL requests the original image without a descriptor header. Non-NULL descriptors are at most 256 bytes plus NUL; XML semantics are left to the peer. Negotiated OBEX capacity may reject even a descriptor within that length bound. Empty strings retain the SDK's empty-header behavior.
- Handle and descriptor are copied into per-peer owned storage before any asynchronous SDK send. Caller storage may be changed/freed after return. A second operation on a busy peer is rejected; the other peer may operate independently.
- `setDataHandler` installs a synchronous sink, required before a GET. Each Data contains the live binding, request number, operation, offset, borrowed bytes and size. Copy or consume the chunk during the callback. Do not retain the SDK pointer. No whole-image buffer is allocated by this facade.
- A sink returning false requests deferred cancellation. The same applies when the next chunk would exceed the configured object limit. Subsequent data is ignored and `poll()` cancels the transport. If OBEX success arrives before that poll, the Complete event still reports the local sink/limit error rather than false success. Accepted byte count excludes the rejected chunk.
- Complete includes request number, operation, accepted byte count and status. Zero means successful OBEX operation, **not validation of the image/XML contents**. Nonzero status may follow partial data; applications decide how to discard or keep partial output. Local errors are SizeLimit (-1001), SinkRejected (-1002) Cancelled (-1003) and Timeout (-1004).
- Binding includes peer, ACL epoch, generation, Cover Art CID and signalling CID. Calls reject stale bindings. `poll()` also cancels when the ACL or associated signalling connection disappears.
- `disconnect(binding)` performs graceful OBEX disconnect only when idle. `cancel(binding)` closes active Cover Art transport; `cancelPeer(peer)` also handles a pending connection. Cancellation does not intentionally disconnect AVRCP signalling, browsing, A2DP or SPP. Failed explicit cancellation preserves the previous local state; deferred cancellation can retry from poll.
- `stop()` is terminal and requests cancellation without global GOEP/Cover Art deinit or radio OFF. ERTM/client/request memory survives asynchronous close and owner destruction. Application callbacks are detached after stop. SDK raw callbacks must not be retained/reinjected after CID reuse.

All API calls use one main loop. Callback-time mutation is rejected; the sink's boolean result is the cancellation signal. The SDK has no public pause/resume/backpressure API for this client. A sink must keep up or return false, for example when its bounded queue fills.

## Opt-in main-loop timeouts

`setTimeouts(connection_ms, object_ms)` configures new connection attempts and GET operations independently. Zero disables that budget (both default to zero). Values above `0x7fffffff` milliseconds are rejected without changing either setting; callback-time changes and changes after stop are also rejected. An admitted operation keeps its budget even if the settings change later.

Use `poll(now_ms)` with caller-supplied monotonic milliseconds modulo 2^32. The first timed poll after a connect/GET arms that operation's timer; call it promptly after admitting commands and regularly thereafter, with less than 2^31 ms between timed polls. Expiry uses unsigned subtraction and handles clock wraparound. This is an **absolute elapsed-time budget**, not an inactivity timer: received data does not reset it. An idle established channel has no timeout. The original `poll()` continues runtime/cancellation work but neither arms nor advances timers. No wall clock or SDK timer callback is installed.

For example, call `setTimeouts(15000, 60000)` before connecting, then `poll(monotonic_ms)` in the main loop. Pending connection expiry emits one Error with Timeout. GET expiry marks the request Timeout; its Complete arrives on SDK operation completion or transport release, with the accepted byte count. A late OBEX success cannot turn that result into success. Timeout cancellation blocks further data/requests and retries from either poll overload if the SDK initially rejects cancellation. `status().last_error` reports Timeout at expiry but may subsequently report a cancellation error; the final request status retains Timeout.

Timeout is a cancellation request, **not a guarantee of release by the deadline**. Clients, ERTM buffers, handles and descriptors remain leased until the SDK confirms release (or failed connection cleanup). Reconnect is rejected while that lease remains, and late connection success is not exposed as Connected. If release never arrives, the slot stays reserved rather than risking use-after-free. `stop()` still detaches callbacks and preserves static storage. These are facade implementation tests, not radio recovery evidence.

## Ownership and SDK corrections

Two static SDK clients and aligned 10,000-byte ERTM buffers remain allocated for the process lifetime. Usable sizes account for the pinned SDK's alignment and RX/TX state layout, with MPS capped at HCI payload minus eight bytes. ERTM is mandatory, MTU 512, four RX/TX slots, FCS and 2000/12000 ms timers. No image data is saved to disk or uploaded automatically.

Hash-pinned build-directory overlays preserve original source licenses and fail on unknown SDK bytes. They fix connection-list removal order; failure events using cleared callbacks; synchronous GOEP connection cleanup; malformed OBEX/parser termination; GET header-capacity checks; peer OBEX packet size; transport send failure handling; and cancellation across SDP, pending transport and active transfer states. GET preflight errors return locally without starting a transfer. Later transport failures emit failure/completion and retain ownership until the terminal release event. [Overlay licensing](avrcp_sdk_overlay.md) and the manifest retain the original notices; this work does not replace the SDK's license or grant new commercial rights.

`picow_avrcp_cover_art_client_cancel` is a repository extension, not an upstream API. During shared SDP work it removes only the Cover Art request, leaving unrelated SDP ownership intact. A pending L2CAP open is allowed to resolve before its storage is freed; successful late opens are immediately closed. Established transfers are closed through GOEP and retain buffers until release.

## Validation scope

This feature has host-model and ARM compile/link coverage. Real-peer interoperability, ERTM radio transfer, simultaneous profile load and hardware-fault recovery remain unverified. A Cover Art image server is not provided.

[Validation scope](validation.md)
