# AVRCP Target SDK feature coverage

[日本語](avrcp_target_features_jp.md) / [Target integration](avrcp_target.md) / [Controller](avrcp_controller.md)

This page describes the current implementation, not a full-AVRCP compliance claim. SDK baseline: Pico SDK commit `9a4113fbbae65ee82d8cd6537963bc3d3b14bcca`, BTstack `501e6d2b86e6c92bfb9c390bcf55709938e25ac1`. Newly added functions have host adapter tests and an ARM build; their interoperability has **not** been tested on hardware. Earlier button tests do not validate these extensions.

## Implemented Target facade

All methods run on the same main loop. Obtain a fresh `Binding` from `core().bindingForPeer(peer)` after connection. It contains CID, peer, ACL epoch, and generation; stale bindings are rejected before SDK calls. An integer zero means local SDK acceptance, not remote receipt. Application callbacks must be bounded and must not reenter receiver/control APIs.

| Capability | API / behavior |
|---|---|
| Incoming and outgoing signaling | `begin`, `connect(peer)`, `disconnect(binding)`, `poll`, terminal `stop`; Controller/Target share one SDK initialization |
| Pass-through buttons | `setSupported` before begin; press/release events and held-state clearing on disconnect/reset |
| Play-status query | `setPlayback(length_ms, position_ms, status)` supplies query response state for both configured peers; no automatic media-player behavior |
| Metadata | `setMetadata(binding, Metadata)` copies title/artist/album/genre, track ID/number/count, and duration into owner-controlled storage |
| Supported notifications | `supportNotification(binding,event_id)` accepts IDs 1..13 with the repository overlay (seven kinds with the unmodified SDK) |
| Playback and volume | `notifyPlayback`, `notifyVolume`; volume is 0..127 and does not change a DAC/software gain |
| Track/list/player/UID state | `notifyTrack`, `notifyContentChanged`, `notifyAddressedPlayer`, `notifyUids` |
| Battery | `notifyBattery`, values 0..4 |
| Capabilities | `setCompanies` (1..8 24-bit IDs), `setUnitInfo` (5-bit type/24-bit company), `setSubunitInfo` (1..32 copied bytes) |
| Remote absolute-volume request | `setRequestHandler`: SDK auto-responds after the synchronous handler; returned volume can quantize the requested volume |
| Remote PlayItem/AddToNowPlaying | Same handler receives scope, UID counter, 8-byte item ID and binding; returns an AVRCP status code for the SDK response |

The original SDK notification IDs are 1 (playback), 2 (track), 6 (battery), 9 (now-playing content), 11 (addressed player), 12 (UIDs), and 13 (volume). Configure each connection explicitly, then report application changes through the corresponding methods. SDK owns notification registration, transaction labels, interim/changed responses, and one-shot re-registration behavior. No automatic application-state observation is supplied.

`notifyPlayback` updates that peer's play-status query playback value; `setMetadata` updates its query duration. `setPlayback` updates query length, position, and playback for both peers; it does not itself issue notifications. Keep query state and notifications consistent in your application.

## Metadata and pointer lifetime

`Metadata` input strings must be readable, NUL-terminated, and at most 255 bytes (bytes, not Unicode characters). Null or longer fields are rejected before any field is changed. The four text fields, company IDs, and subunit bytes are copied; callers may release their input after a successful call. Track IDs are eight bytes, never text strings.

BTstack retains pointers to metadata/capability buffers. The owner has separate fixed storage per configured peer. Updates return `ERROR_CODE_COMMAND_DISALLOWED` while the SDK is sending a response or waiting to continue a fragmented metadata response. Poll normally and retry later; do not spin. The check uses the validated SDK's connection state, metadata response flag, and unfinished `GET_ELEMENT_ATTRIBUTES` byte count, and must be reviewed on SDK upgrades. If a peer abandons a continuation without an abort, the update can remain busy until the SDK transaction/channel ends.

Use the Target for the program lifetime. If it is destroyed after begin was attempted, copied capability/metadata storage is deliberately retained until reset because asynchronous SDK cleanup can still reference it. This is not a restartable or repeated-create/destroy service. `stop()` does not power off another profile's radio and avoids disconnecting a signaling channel still used by the Controller owner.

## Request handler contract

```cpp
using Target = picow::avrcp::AvrcpTarget;
Target::Response decide(const Target::Request& request, void*) {
    if (request.kind == Target::Request::Kind::AbsoluteVolume) {
        // Queue your actual output-volume intent here. No automatic gain change.
        return {0, request.volume};
    }
    // 0 = AVRCP INVALID_COMMAND. Return 4 (SUCCESS) only if your application
    // actually accepts the requested item operation.
    return {0, 0};
}
// target.setRequestHandler(decide, nullptr);
```

With no handler, item operations return INVALID_COMMAND and absolute volume retains the requested value. The volume response is a value, not an AVRCP status; the SDK already accepts it on the wire. Invalid returned volume falls back to the original request and increments response errors. Invalid item status values become INVALID_PARAMETER. The callback's intent must be implemented by the application; receiving a request does not play audio or modify a queue automatically.

## Pass-through wire decision (pinned overlay)

`setSupported(operation, enabled)` before `begin` controls the wire support policy. The existing nine standard button defaults are unchanged. Only defined AV/C panel operations are dispatched; reserved IDs return NOT_IMPLEMENTED even when configured. Standard buttons must have zero operands. Unsupported operations return NOT_IMPLEMENTED; repeated press, unmatched release, stale binding and unsupported standard operands return REJECTED. Rejection never changes the held state or emits an Operation event.

Optionally install `setPassThroughHandler(handler, context)`, where the handler returns `PassThroughDecision::Accepted`, `Rejected`, or `NotImplemented` for a `const PassThroughRequest&`. The request contains a fresh `binding`, `operation`, `pressed`, `operand_count`, and a copied five-byte `operands` array. Invalid decision values become REJECTED. Without a handler, supported valid standard buttons are accepted and delivered once through the existing core callback. With a handler, only Accepted updates state and delivers that callback. Treat the decision handler as validation/intent admission and the core Operation callback as standard-button execution; do not execute the same action twice.

SIG next/previous group navigation requires `setSupported(0x7e, true)` **and** a handler. Exactly five operands encode `00 19 58 00 00` (next group) or `00 19 58 00 01` (previous group). The handler must implement these group intents; they are not mapped to track forward/back buttons. Each group has independent press/release state per peer and binding generation, reset on reconnect. Group requests are delivered only to the decision handler, not the operand-free core Operation callback. No handler means NOT_IMPLEMENTED. Unknown company/group IDs return NOT_IMPLEMENTED; wrong group operand lengths return REJECTED. Arbitrary vendor payloads are not implemented.

The overlay queues exactly one response, echoes operation/state, transaction label and complete bounded operands, and fixes the SDK's two extraneous pass-through padding bytes. It accepts only CONTROL commands for panel subunit 0. Truncated headers are ignored; declared-length mismatches and more than five operands are rejected without invoking the application or copying unbounded input. A busy channel (including an unfinished metadata continuation) drops a new pass-through command without replacing its pending response or transaction label. The peer must retry after the channel becomes idle.

Both callbacks must be bounded, synchronous, and must not call SDK response functions, reenter APIs, or throw. Never send a second ACK. Acceptance admits application intent; it does not prove a physical media effect or response delivery. The installed SDK is not edited: generation checks the pinned source SHA-256 and fails on an unknown revision.

## Explicit remaining gaps and SDK limitations

- The [pinned repository overlay](avrcp_sdk_overlay.md) now implements standard equalizer/repeat/shuffle/scan settings. Call `configurePlayerSetting(id,value)` before `begin`; only enabled attributes are advertised. List/value/current/text queries are bounded; SET is validated atomically and requires `SetPlayerSettings` handler acceptance before updating cached state. Text is fixed UTF-8 English, not vendor-defined settings or a localization system.
- `SetAddressedPlayer` requests now carry a validated binding and player ID. Without a handler they are rejected. Acceptance changes the SDK addressed-player state; the application must actually select the player and notify relevant state changes.
- Overlay notifications add track start/end, playback position, system status, settings, and available players. Use `notifyTrackReachedStart/End`, `notifyPosition`, `notifySystemStatus`, `notifyPlayerSettings`, and `notifyAvailablePlayers`. Read `positionNotificationInterval(binding)` in seconds and schedule position updates in the application. There is no automatic timer, seek detector, or media database. Settings changes do not automatically notify peers.
- **Browsing Target and cover art** are not wrapped here. PlayItem/AddToNowPlaying control PDUs are not a complete browsing database, filesystem, or player implementation.
- **Pass-through wire policy:** the pinned overlay implements the explicit policy below. Without the overlay, the original SDK still auto-accepts before its legacy event; do not use that path when wire rejection is required.
- **No full AVRCP 1.6 guarantee:** SDP's version is SDK-selected. SDP advertises player/recorder plus configured settings and group-navigation bits. The browsing build may additionally advertise browsing via begin(config, true); multiple-player support is not implied. No adapter test proves on-air interoperability or audio effects.

## Verification

- Shared Controller/Target lifecycle regression. This does not establish hardware negotiation or timing.

- Target API checks: real SDK types/accessors with stubbed SDK effects, ASan/UBSan. Covers owned text/capability copies, 255/256-byte bounds, null input, two-peer isolation, pending/continued-response blocking, supported/unsupported notification IDs, range checks, SDK error propagation, stale binding, volume/items, callback reentry rejection, and terminal stop.
- Target adapter checks: existing lifecycle, release, malformed event, play-status, failure and two-peer regressions.
- Target build checks covered compilation and linking against the actual SDK for Pico W; they do not establish radio operation.

Target overlay checks exercise generated SDK parser/serializer paths under ASan/UBSan and checks addressed-player CID/length/rejection, settings replies, item response lookup, notification transaction/busy/one-shot behavior, malformed IDs, and facade atomic settings/binding rules. Pass-through tests additionally cover all three response codes, exact frame bytes, press/release, group payload echo, invalid decisions, busy preservation, length bounds, one response, facade sequence/reentry/peer-generation rules, and deterministic overlay generation. L2CAP framing is mocked as single-packet. These tests do not prove over-air fragmentation, interoperability with a real remote player, or browsing behavior.

[Validation scope](validation.md)

[Shared pending signalling cancellation contract](avrcp_controller.md): Target also exposes cancelConnection(peer), cancellationPending(peer) and cancellationStatus(peer). Cancellation affects the shared attempt and does not permit resource reuse before release.

## Long metadata responses

The pinned Target overlay splits metadata into vendor responses of at most 502 parameter bytes, each with its own AVCTP framing. Attribute bytes and their aggregate offset survive REQUEST_CONTINUE; only the first response includes the attribute count. ABORT ends the retained transaction. Metadata replacement is blocked and queued notifications are deferred until completion or abort. If the peer abandons continuation without aborting, the channel can remain busy.

metadata continuation checks exercises the generated SDK serializer and common framing with mocked L2CAP, including long attributes, continuation/abort, retained buffers and notification ordering. It is not a radio or facade-copy test.

For included sample builds and runnable checks, see the [build guide](build_guide.md#compile-only). Those checks do not rerun every feature-specific validation described here.
