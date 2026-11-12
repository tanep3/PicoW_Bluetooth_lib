# USB support provenance

The optional main-loop-owned TinyUSB mode needs the SDK CDC descriptors and
reset interface even when `tinyusb_device` is explicitly linked. These two local
copies retain their complete upstream MIT copyright and permission notices:

- `usb_descriptors.c`: Pico SDK `src/rp2_common/pico_stdio_usb/stdio_usb_descriptors.c`.
  Original SHA-256: `c491fdfd469e868989be05766ab7bea024a7bba8210c6ad2495c597152eac97a`.
  Only change: replace the host/device-library exclusion guard with an always-on
  guard. CMake compiles this file only when `SINK_PCM_USB_CAPTURE=ON`.
- `tusb_config.h`: Pico SDK `src/rp2_common/pico_stdio_usb/include/tusb_config.h`.
  Original SHA-256: `db5052c282353f683878d0209948a6873164d0659f6dc7f2faa9635794b3479c`.
  Changes: include generated `sample_config.h`; enable the settings when capture
  is ON, retaining the original exclusion condition otherwise. Buffer overrides
  are supplied by the example CMake target, not by modifying the SDK.

Upstream checkout: https://github.com/raspberrypi/pico-sdk/tree/9a4113fbbae65ee82d8cd6537963bc3d3b14bcca

The reset vendor interface, Microsoft OS descriptor, unique board USB serial and
SDK baud-rate reset callback remain available. No fixed private device identity
is copied. Other linked SDK/TinyUSB/BTstack dependencies retain their own
licenses; see the repository's third-party notices. This note covers source
provenance, not permission to redistribute a recording.
