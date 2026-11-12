# AVRCP Target: receive media controls

[日本語](avrcp_target_jp.md) | [Target feature reference](avrcp_target_features.md)

Use `picow::avrcp::AvrcpTarget` to receive media buttons from a configured Controller, such as a headset. The application maps accepted operations to its own playback state. This facade does not play audio, send Controller commands or implement a browsing database.

## Prerequisites and build

Use C++17, the polling Pico SDK architecture and a compatible `btstack_config.h`. Include `cmake/picow_avrcp_target.cmake` and call `picow_target_avrcp_target(target config_directory)`. BTstack uses two AVRCP connection objects per peer; reserve `MAX_NR_AVRCP_CONNECTIONS >= 4` for two peers. Use the [main-loop sample](../examples/AvrcpMainLoopSample/README.md) for a complete build and ownership pattern.

Target and Controller share signalling initialization and the Bluetooth runtime. Do not initialize BTstack again or combine these owners with the legacy AVRCP plugin. Owners and callback contexts must live for the process lifetime. Use one main loop, never IRQs or a second core.

## Connect and handle commands

1. Configure one or two distinct nonzero peers in `AvrcpTarget::Config`. Register the operation callback with `setCallback` and, if required, the synchronous decision handler described in the [feature reference](avrcp_target_features.md).
2. Call `begin(config)` once and check its result. Keep calling `poll()`; initialization success is not a connected channel. Pairing, visibility and connectability require the shared application's explicit policy.
3. Accept an incoming connection from an allowed peer or call `connect(peer)` to request an outgoing one. Outgoing SDP can be busy when another profile owns the shared client. A zero result is local acceptance, not remote completion. Observe `Connected` and obtain `core().bindingForPeer(peer)` before using binding-dependent methods.
4. Copy callback observations into bounded application storage and act later from the main loop. A decision handler must return its acceptance/rejection synchronously; it cannot defer that decision. Do not execute the same accepted operation in both the decision handler and operation callback.
5. Keep playback state current with `setPlayback(length_ms, position_ms, status)`. Initially playback is stopped and length/position are unknown. The facade responds to play-status requests using that state.

The generated SDK overlay implements pass-through wire decisions and owns response framing. Never send a second ACK. Metadata, notifications, volume/item requests and supported player settings have separate bounds and response obligations in the [Target feature reference](avrcp_target_features.md). SDP's version does not guarantee complete AVRCP conformance.

## Disconnect and failures

A `Binding` carries peer, ACL epoch, CID and generation. Preserve it intact; a stale binding must not be relabeled for a new connection. `Disconnected` / `Reset` means the application must clear held-button state. SDK callbacks do not carry generations, so stored raw packets must not be reinjected after CID reuse.

`disconnect(binding)` closes the shared signalling channel and affects both AVRCP roles on that CID. Pending setup uses `cancelConnection(peer)` and its pending/status queries, following the [shared cancellation contract](avrcp_controller.md). Keep polling and retain owners until asynchronous cleanup completes. A locally accepted cancellation is not resource release.

`stop()` is terminal, not a reconnect API. A shared channel can remain for the other role. Stale or unknown release callbacks can increase `ignored` without delivering another application disconnect. Never recover by recreating owners or repeatedly calling `begin()`.

## Check the result

Use the [API reference](api_reference.md) for event/state fields and the feature reference for individual return codes. For offline regression in the full checkout:

For included checks, see the [build guide](build_guide.md#compile-only).

Limited Target hardware observations exist, but they do not qualify every button, held-button disconnect, arbitrary headsets or long-term reliability. Trial measurements and retained failures are in [Validation scope](validation.md). Host tests and compile-only probes are not radio validation.
