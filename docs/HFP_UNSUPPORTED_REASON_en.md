# Why HFP call audio is unsupported

[日本語](HFP_UNSUPPORTED_REASON.md) · [Supported features](supported_features.md)

This library supports Bluetooth Classic A2DP audio, AVRCP controls and SPP data. It does not provide a supported HFP/HSP call-audio or microphone API.

HFP control signalling and SCO/eSCO audio transport are separate. A control connection or received request does not prove voice audio can be transported and captured through the Pico W configuration. Investigation did not establish a working end-to-end SCO voice path.

The exact internal failure location/root cause was not established. This is not evidence of a defective chip, proof that every hardware arrangement is impossible, or a finding that one particular BTstack function is responsible. Successful SPI/WLAN reads do not establish a working Bluetooth SCO path.

For headset microphone or call audio, choose a platform and SDK with a supported, validated voice path. A2DP SBC reception is not a substitute for HFP microphone support. FastStream, BLE and LE Audio are not supported alternatives here.

See [validation scope](validation.md) for the limits of supported profiles.
