# PicoW Bluetooth Library

[日本語](README_jp.md)

**A C++ library for adding Bluetooth audio, media controls, and data communication to Pico W applications.**

Send audio to a Bluetooth speaker, receive audio from a PC, or control its player with buttons on your device. The library handles connections and audio packets so you can focus on your audio source and application.

For **Raspberry Pi Pico W / Pico SDK / C++17**.

## Features

- **Send and receive audio (A2DP).** Supply PCM audio for transmission or consume received audio as PCM. The supported codec is SBC.
- **Send and receive media controls (AVRCP).** Handle playback, pause, track changes, volume, and other controls.
- **Exchange data (SPP).** Send settings, sensor readings, and other data through serial-like byte streams.

For example, receive audio from a PC while sending playback commands back to it in the same application.

## Try an example

[Set up your development tools](docs/build_guide.md#prerequisites), then choose one example below. Each guide covers peer configuration, building, flashing Pico W, and operating the example.

| Goal | Example |
|---|---|
| Send audio to a Bluetooth speaker | [A2DP Source](examples/A2DPSourcePlayer/README.md) |
| Receive PC audio and pause or resume its player | [A2DP Sink + AVRCP Controller](examples/AvrcpMediaControlSample/README.md) |
| Process received PCM in your application | [PCM consumer](examples/A2dpSinkPcmConsumer/README.md#first-use) |
| Exchange data or receive remote button presses | [DeveloperSamples](examples/DeveloperSamples/README.md) |

The A2DP Source example sends a short test tone to the speaker. The receiving examples deliver PCM; audible output from Pico requires your own DAC or other audio-output implementation.

<a id="building-the-project"></a>
## Use it in your application

The [integration walkthrough](docs/getting_started.md) provides a folder layout, CMake configuration, and C++ examples. You can also add the library to an existing Pico SDK project.

- Look up functions and arguments: [API reference](docs/api_reference.md)
- Resolve build or connection problems: [Troubleshooting](docs/build_guide.md#troubleshooting)
- Find more examples and specifications: [Documentation index](docs/documentation_index.md)

<a id="sink-controller"></a>
## Before you use it

- This library uses **Bluetooth Classic**. BLE and HFP calls are not supported. It is not an Arduino or MicroPython library.
- Your application implements playback, speaker output, and the behavior of received controls.
- Available features depend on the peer device. See [supported features and validation scope](docs/supported_features.md).

## License

Project-original code uses the [MIT License](LICENSE). Copyright (c) 2026 Tane Channel Technology.
Dependencies and third-party code have separate terms. Review [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before use or redistribution.
