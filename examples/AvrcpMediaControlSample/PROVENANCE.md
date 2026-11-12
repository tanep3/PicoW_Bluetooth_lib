# Source and dependency scope

The console, application policy, command parser, configuration, documentation,
and focused host tests are original project code under the repository's MIT
license. They adapt the project's own Windows media-control experiment into a
generic example; no device-specific address, private capture, firmware binary,
or operator transcript is included.

This directory adds no third-party source copy or new dependency. It uses the
existing public A2DP Sink and AVRCP Controller helpers, the shared Bluetooth
runtime, the v6 AVRCP SDK overlay, Pico SDK USB stdio and hardware watchdog.
Its btstack_config.h includes the existing public A2DPSinkReceiver configuration.
The original external SDK, Bluetooth, codec, USB and firmware components keep
their own licenses; compiling or linking them does not make them project MIT
code. Retain the repository LICENSE, THIRD_PARTY_NOTICES.md and licenses/ tree
and review those terms before distribution. In particular, SDK/BTstack platform
and commercial-use conditions must not be inferred from this example's license.

The host doubles are original test models, not copied SDK implementations and
not proof of external API behavior. The real ARM build uses the existing SDK
and generated overlay, with their recorded source provenance unchanged.
