# Validation scope and limitations

[日本語](validation_jp.md) · [Features](supported_features.md) · [Documentation index](documentation_index.md)

Use this page to distinguish implemented APIs, software/build checks and bounded hardware observations. Test success applies to the tested configuration; it is not a Bluetooth certification or a guarantee for every peer.

## A2DP audio

- **Source:** ordinary PCM-to-SBC transmission has been exercised on hardware. Small-MTU SBC fragmentation has software and ARM-build coverage, but has not been confirmed on hardware.
- **Sink:** SBC reception from Windows and PCM decoding have bounded hardware evidence. The receiving application must provide its own DAC/I2S output, gain and buffering. Counting or discarding PCM does not demonstrate audible output or audio quality.
- **Codec scope:** SBC is the supported codec. General encoded-media/codec and SEP-management APIs are not complete.
- **Two audio roles:** normal Source and Sink helpers permit different-peer owners. Same-peer Source+Sink is opt-in experimental work, not a supported two-stream radio configuration. Arbitrary cancellation, reconnect and sustained dual-stream throughput are not established.

See the [Source lifecycle](a2dp_source_lifecycle.md), [PCM contract](a2dp_sink_pcm.md) and [dual-role guide](a2dp_dual_role.md).

## AVRCP media control

Controller and Target are distinct roles: Pico can control a remote player, or receive commands that its own application implements. APIs exist for metadata, notifications, Browsing and Cover Art, but their full interoperability is not hardware-validated.

A bounded Windows test exercised **SBC Sink reception and Controller NEXT/volume-button commands together**. Accepted command responses and visible player changes were observed. A separate test observed PLAY/PAUSE; it also recorded an unexplained disconnect before shutdown. These results do not establish long-running stability, every player, absolute-volume semantics, speaker gain or every Target function.

The public **AvrcpMediaControlSample** and **A2dpSinkPcmConsumer** applications have software/build coverage, but those exact public applications remain **hardware NOT_RUN**. Shared-library hardware evidence is not an execution of every example configuration.

See the [Controller guide](avrcp_controller.md), [Target API](avrcp_target.md), [Browsing](avrcp_browsing.md) and [Cover Art](avrcp_cover_art.md).

## SPP data communication

Bounded tests exercised two routes to one peer, reconnection, pause/resume and data integrity under selected traffic. Two routes do not mean two independent radios or two simultaneous peer devices. The shared Bluetooth link has finite buffers and scheduling capacity; there is no general throughput, latency or audio-quality guarantee under mixed traffic.

The application supplies message framing, acknowledgments and retry policy. Review the [SPP lifecycle](spp_lifecycle.md), [port controls](spp_control.md), [state transitions](spp_state_transitions.md) and [hardware-validation scope](spp_hardware_validation.md).

## Bond storage and optional features

Persistent bonds and committed TLV storage are opt-in. Software models and build checks cover bounded corruption, power-loss and recovery cases; they do not certify flash endurance, arbitrary power interruption or every storage layout. Review the [bond backend](bluetooth_bond_storage.md) and [committed-TLV contract](bluetooth_committed_tlv.md) before provisioning or using a device.

HFP/SCO call audio, BLE and LE Audio are outside the library's supported scope. See [why HFP is unsupported](HFP_UNSUPPORTED_REASON_en.md).

## Using the test and example material

- A host test checks the modeled inputs and mocked boundaries described by that test. It does not establish radio interoperability.
- A configure/compile/link check proves only the selected build path. Linker garbage collection can discard unused code.
- Hardware observations are limited to the peer, configuration and operations actually exercised. Raw device logs and recordings are not distributed here.
- Compile probes are not complete runnable applications. Follow each example's instructions and start from its default configuration.
- Recheck long-running use, wider device compatibility, Wi-Fi coexistence, resource limits and feature combinations in your intended application. Do not infer a newly passed test from a documentation change.

For installation and ordinary checks, start with the [build guide](build_guide.md). Third-party software keeps its own [license terms](../THIRD_PARTY_NOTICES.md).
