# Supported scope and API contracts

[日本語](specification_jp.md)

The target is Raspberry Pi Pico W, Pico SDK, C++17 and Bluetooth Classic. Apply the required CMake helpers to your application target. This is not an Arduino/MicroPython package or an HFP/BLE audio library.

- Audio: SBC Source transmission and Sink reception. DAC/I2S output and gain processing belong to the application.
- Media control: AVRCP Controller/Target. Additional APIs do not imply hardware validation with every peer.
- Data: SPP byte streams. Two routes do not mean two independent radios or two simultaneous peers.
- Execution: poll profiles from one main loop and respect asynchronous completion, borrowed buffers, binding generations and capacity limits.

Use these current references:

1. [Installation and first application](getting_started.md)
2. [Features and limitations](supported_features.md)
3. [API reference](api_reference.md)
4. [Architecture](architecture.md)
5. [Validation scope](validation.md)
6. [Third-party terms](../THIRD_PARTY_NOTICES.md)

For feature-specific ownership, timing, capacity and experimental settings, follow the API reference and its linked contracts.
