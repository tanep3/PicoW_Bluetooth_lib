# Library architecture

[日本語](architecture_jp.md) · [API reference](api_reference.md) · [Integration guide](getting_started.md)

The library separates application-facing profile objects, shared Bluetooth lifecycle, protocol adapters and bounded data processing. Applications normally use the profile CMake helpers and public facades; they do not initialize a second BTstack instance for each profile.

## Profile entry points

- [A2dpSourcePlayer](../audio-core/A2dpSourcePlayer.h) pulls application PCM through an encoder and transport pipeline, then sends SBC.
- [A2dpSinkReceiver](../audio-core/A2dpSinkReceiver.h) receives and decodes SBC, delivering borrowed PCM to an [IAudioSink](../audio-core/IAudioSink.h). The application supplies playback/output and respects reset/ownership boundaries.
- [AvrcpController](../avrcp-sdk/AvrcpController.h) sends commands to a remote player; [AvrcpTarget](../avrcp-sdk/AvrcpTarget.h) receives controls and exposes application-owned state. Browsing and Cover Art have separate adapters and bounded APIs.
- [SppClient](../spp-sdk/SppClient.h) exposes two serial-like byte-stream routes, with explicit lifecycle, partial I/O and diagnostics. Message framing and retries belong to the application.

## Shared runtime and execution

[BluetoothRuntime](../bluetooth-runtime/BluetoothRuntime.h) coordinates the shared radio lifecycle and registered profile owners. Profile-specific state machines and SDK adapters retain separate bindings, generations and state. A disconnected/replaced binding must not silently revive queued application work.

Call active profiles' processing methods from one main loop and follow their object-lifetime rules. Do not invoke receiver mutation from its PCM callbacks or introduce another-core/IRQ access unless the API explicitly allows it. Acceptance of a request is asynchronous; observe the relevant state/event instead of treating a successful return as remote completion.

See [connection transitions](connection_state_transitions.md), [SPP transitions](spp_state_transitions.md) and the [API reference](api_reference.md) for exact boundaries.

## Audio data ownership

Source processing uses [IAudioSource](../audio-core/IAudioSource.h), [IAudioEncoder](../audio-core/IAudioEncoder.h), [ITransport](../audio-core/ITransport.h) and [AudioPipeline](../audio-core/AudioPipeline.h). Pending encoded data is retained across ordinary send deferral rather than reading and encoding the same PCM again. Lifecycle/format changes invalidate old queued data.

Sink PCM callbacks borrow data for the documented callback duration. Applications that defer work must copy it into their own bounded storage and discard queued data at the required reset/generation boundary. A receiver without an output consumer does not drive a speaker.

## Build helpers and SDK boundaries

The `cmake/` helpers add the selected library files, configuration headers, SDK dependencies and required target-local overlays. Some helpers generate patched copies of hash-pinned BTstack source in the build tree; they do not edit the installed SDK. Unknown source hashes fail configuration.

Do not combine helpers merely because they each build independently. In particular, the [same-peer A2DP experiment](a2dp_same_peer_experiment.md) cannot share a CMake configuration with the public Sink helper. Ordinary distinct-peer dual-role use has separate [constraints](a2dp_dual_role.md).

Persistent bonds and committed TLV are opt-in storage components with [layout and recovery requirements](bluetooth_committed_tlv.md). They are not automatically enabled by using a Bluetooth profile.

See [supported features](supported_features.md), [validation limits](validation.md) and [third-party terms](../THIRD_PARTY_NOTICES.md) before relying on a configuration.
