# Features and limitations

[日本語](supported_features_jp.md) / [README](../README.md)

Use this page to check the library's scope before integrating it into a Pico W application. Follow the examples to get started and the [API reference](api_reference.md) for function details.

## Audio: A2DP

- **Source** encodes PCM supplied by your application as SBC and transmits it.
- **Sink** decodes incoming SBC and delivers PCM to your application.
- Your application provides DAC or I2S output, gain control, and other audio processing. Enabling Sink alone does not produce sound from Pico.

The supported codec is SBC. Other codecs and HFP call audio are outside this scope.

Examples: [Audio sender](../examples/A2DPSourcePlayer/README.md) / [PCM receiver](../examples/A2dpSinkPcmConsumer/README.md)

## Media controls: AVRCP

- **Controller** sends playback, pause, track-change, volume-button, and other commands.
- **Target** receives commands for your application to handle. Your application also owns playback and volume state.
- APIs for metadata, notifications, Browsing, and Cover Art are available, but not all have been validated on hardware.

A remote device accepting a command does not guarantee that the intended player acts on it. Check both the response and the resulting state. Sending volume buttons is separate from setting absolute volume or adjusting Pico's audio gain.

The [Sink + Controller example](../examples/AvrcpMediaControlSample/README.md) provides pause and play commands. Add track changes or volume controls to your application using the [Controller API](avrcp_controller.md).

## Data: SPP

Send and receive byte streams, much like a serial connection. Up to two routes can connect to the same peer; this does not mean two simultaneous peer devices.

Your application defines message boundaries, acknowledgments, and retries. See the [SPP guide](spp_examples.md).

## Combining features

A2DP, AVRCP, and SPP share Bluetooth initialization. Keep calling each active feature's processing methods from the same main loop.

An audio Sink can also act as a Controller to send commands back to the source. This is different from simultaneous Source + Sink audio. The library does not guarantee every feature combination or peer device.

## Validation scope

Normal SBC transmission and SBC reception from Windows have been tested on hardware. A separate Windows trial confirmed track-change and volume-button controls while receiving audio. Controller pause and play were confirmed in another trial.

The public PCM consumer and Sink + Controller examples have build and software-test coverage, but those exact applications have not been run on hardware. Tests of the shared library do not guarantee a particular example or peer combination.

Check long-running use, wider device compatibility, Wi-Fi coexistence, and other requirements in your intended configuration. Detailed test conditions and history are in the [Validation scope](validation.md).
