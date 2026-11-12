# Developer sample entry points

[日本語](developer_samples_jp.md)

Start with [DeveloperSamples instructions](../examples/DeveloperSamples/README.md) when first using the public APIs. Learn settings → begin → poll → state inspection from small runnable examples before reading load-test internals.

1. [A2DP Source](../examples/DeveloperSamples/a2dp_source.cpp): send ten seconds of silent PCM from `IAudioSource`.
2. [AVRCP Target](../examples/DeveloperSamples/avrcp_target.cpp): receive Controller operations, observe press/release, and handle them in the application.
3. [SPP, one route](../examples/DeveloperSamples/spp_client.cpp): channel/UUID connection, candidate selection, partial writes, reads, and listening.
4. [SPP, two routes](../examples/DeveloperSamples/spp_two_routes.cpp): two routes to the same peer; shared logic is in [SppSample.h](../examples/DeveloperSamples/SppSample.h).
5. [Bounded SPP reconnect](../examples/DeveloperSamples/spp_reconnect.cpp): at most three attempts with cooldown/deadline, an application policy without infinite retries.
6. [A2DP + AVRCP](../examples/DeveloperSamples/a2dp_avrcp.cpp): shared runtime and passing callback intent to the main loop.
7. [A2DP + SPP + AVRCP](../examples/DeveloperSamples/a2dp_spp_avrcp.cpp): two SPP listeners, explicit A2DP connection on the same live ACL, and polling every profile.

**Scope:** Pico W, Classic A2DP Source / AVRCP Target / SPP. HFP and FastStream are outside scope. A2DP Sink is not a finished speaker facade. Separate public examples cover the [receiver](../examples/A2DPSinkReceiver/README.md), [bounded PCM consumer](../examples/A2dpSinkPcmConsumer/README.md), and [measured-delay command flow](../examples/A2dpSinkDelayReportSample/README.md). The [START decision example](../examples/A2dpSinkStartDecisionSample/README.md) shows preselected allow/refuse policy, request tokens and disconnect handling, with radio OFF by default. The [AVRCP main-loop example](../examples/AvrcpMainLoopSample/README.md) adds an explicit Cover Art connect/GET sequence. These later examples have their own verification limits; the original seven hardware observations do not qualify them.

**Safe defaults:** With no peer, begin is disabled. Even after configuring a peer, radio startup waits for console `b`. USB CDC is default; UART is optional. Read the detailed guide before running for fixed-peer restrictions, explicit SSP opt-in, RAM-only keys, A2DP-specific pairing policy, generation-tagged handles, and lifetime requirements.

## Roles and validation scope

A2DP Sink receives audio; AVRCP Controller sends commands to the source player. Target receives commands in the opposite direction. The public media-control console implements `PLAY` / `PAUSE`. Its hardware validation remains open; results from a different test application do not qualify it. See [Validation scope](validation.md) for trial details.

## AVRCP Controller media control

[The media-control console](../examples/AvrcpMediaControlSample/README.md) shows A2DP Sink + AVRCP Controller, explicit Play/Pause, copied callbacks, safe press/release completion, remote PlayStatus and operator-paced UI confirmation. Both radio and pairing default OFF, with START still required in an enabled image. It contains no personal peer address or recording. It incorporates lessons from the separate Windows hardware trial, but the refactored public example itself remains hardware-unverified. The Target example above receives operations instead; the two roles are not interchangeable.

## Sink PCM and optional USB recording

[The PCM consumer example](../examples/A2dpSinkPcmConsumer/README.md) now includes an opt-in five-second USB recorder and explicit-port `capture_usb.py` console. Radio and `SINK_PCM_USB_CAPTURE` default OFF; the Source peer remains explicitly configured. No private first-peer binding, automatic flash or recorded music is included. See each sample README for its purpose, configuration and validation scope. Individual trial records are kept in [Validation scope](validation.md).

## Two-route SPP application framing and duplex I/O

[The framed duplex example](../examples/SppFramedDuplexSample/README.md) adds a control route and a two-type data route, partial writes, fragmented/coalesced reads, typed values and generation-bound discard. Framing lives only in the sample; the raw SPP API is unchanged. Outgoing/incoming are exclusive modes and radio defaults OFF. See each sample README for its purpose, configuration and validation scope. Individual trial records are kept in [Validation scope](validation.md).

## Check the result

Follow each sample README to configure and build. Check the peer and pairing policy before starting. Observe connection state, received events and PCM/byte progress rather than treating `begin()` as success. Keep polling after stop/disconnect until asynchronous cleanup completes.

The original seven examples have bounded hardware observations, not seven unconditional passes. Verdicts, untested paths and restoration failures are retained in [Validation scope](validation.md). They do not guarantee audio quality, arbitrary peers, endurance or later-added examples.
