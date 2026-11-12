# Pico W Bluetooth Library Build Guide

[日本語](build_guide_jp.md) / [README](../README.md) / [Detailed sample controls](../examples/DeveloperSamples/README.md)

Build one sample for your chosen Bluetooth device, flash its UF2 to a Pico W, and try it over USB serial. Once it works, use the same library in your own application.

This guide assumes basic C++ knowledge but no prior Pico SDK or Bluetooth setup. Commands use Bash on Ubuntu/Debian Linux. Windows users can build in WSL, but flashing and serial access need separate USB arrangements. These steps have not been verified on WSL, macOS, or native Windows.

**Choose a route before running commands.** You do not need to build every sample.

| Your goal | Build/configure | After flashing |
|---|---|---|
| Send audio, SPP data, or receive buttons | Step 3 below (`DeveloperSamples`) | Step 5, single-letter commands |
| Receive audio and control the source player | [Sink/Controller build](#sink-avrcp) | [Media console workflow](../examples/AvrcpMediaControlSample/README.md#operator-workflow), whole-line commands |
| Receive PCM into your own processing | [Sink build](#sink-avrcp) | [PCM first use](../examples/A2dpSinkPcmConsumer/README.md#first-use), no recording needed |
| Make a separate application | [Your first app](getting_started.md) | The controls implemented by that app |

Tool setup and BOOTSEL flashing are shared. Runtime commands are **not** interchangeable.

## 1. Build overview

```text
Application C++ + this library + Pico SDK / BTstack
                         ↓ Configure with CMake
                         ↓ Compile and link with the ARM toolchain
                     .elf and .uf2
                         ↓ Copy one selected UF2 to Pico W
                  Operate from a serial terminal
```

- **Pico SDK:** Raspberry Pi's development kit for GPIO, USB, radio, and other peripherals. It includes BTstack, the Bluetooth foundation.
- **ARM cross-compiler:** Produces code for Pico W, not for the development computer. Commands are `arm-none-eabi-gcc` and `arm-none-eabi-g++`.
- **CMake:** Selects sources and build settings. `-S` selects the source directory; `-B` selects the output directory.
- **Target:** The executable to build, such as `sample_a2dp_source`.
- **ELF / UF2:** ELF contains executable code and debug information; UF2 is used to flash the board.

The build host can be a Raspberry Pi 5 or another Linux computer. **The target board is Raspberry Pi Pico W.** The ordinary Pico has no radio. Sample CMake files select `PICO_BOARD=pico_w`; other boards are not verified.

<a id="prerequisites"></a>
## 2. Prerequisites

### Hardware

No board is needed just to build. For hardware use, prepare:

- Raspberry Pi Pico W and a USB data cable, not a charging-only cable.
- A suitable Bluetooth Classic peer:
  - For Pico as A2DP Source: an audio receiver, such as a speaker or test receiver.
  - For Pico as A2DP Sink: a phone/computer that sends Bluetooth Classic SBC audio.
  - SPP: a device/application providing an RFCOMM service, or a client connecting to Pico's service.
  - For Pico as AVRCP Target: a remote Controller that can connect and send controls. Not every headset connects independently.
  - For Pico as AVRCP Controller: a source player exposing an AVRCP Target; OS/player behavior must be observed, not inferred from connection alone.
- A USB serial terminal application. For UART, a 3.3 V USB-UART adapter.

### Linux tools

Example packages for a new setup; do not reinstall tools that are already available:

```sh
sudo apt update
sudo apt install build-essential cmake git python3 \
  gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
  pkg-config libusb-1.0-0-dev
```

The ARM C++ standard library is large, so check available disk space. `libusb` is needed if the SDK builds the auxiliary picotool utility.

Verify installation:

```sh
cmake --version
arm-none-eabi-gcc --version
arm-none-eabi-g++ --version
c++ --version
python3 --version
```

The library uses C++17 and declares CMake 3.13 as its minimum. The tested version is CMake 3.28.3; see the [verified environment](#verified). Optional historical HFP experiments require CMake 3.19 or newer, but are not needed for these samples.

### Pico SDK

If an SDK is already installed, locate it rather than silently switching versions. The following example creates a **new SDK directory**.

Validation used SDK 2.1.1-series commit `9a4113fbbae65ee82d8cd6537963bc3d3b14bcca`, shortly after the `2.1.1` tag, with fixes including the fetched picotool version. Pin the same commit for reproducibility:

```sh
mkdir -p "$HOME/pico"
git clone https://github.com/raspberrypi/pico-sdk.git "$HOME/pico/pico-sdk"
git -C "$HOME/pico/pico-sdk" checkout 9a4113fbbae65ee82d8cd6537963bc3d3b14bcca
git -C "$HOME/pico/pico-sdk" submodule update --init --recursive
export PICO_SDK_PATH="$HOME/pico/pico-sdk"
```

A detached HEAD message is expected when pinning a commit. If that directory already exists, use a different new clone destination.

**Do not skip submodules.** They contain separately maintained dependencies such as BTstack, CYW43, and TinyUSB. Missing dependencies prevent the build.

```sh
test -f "$PICO_SDK_PATH/external/pico_sdk_import.cmake"
test -f "$PICO_SDK_PATH/lib/btstack/src/btstack.h"
test -d "$PICO_SDK_PATH/lib/cyw43-driver/src"
test -d "$PICO_SDK_PATH/lib/tinyusb/src"
git -C "$PICO_SDK_PATH" submodule status
```

Successful `test` commands print nothing; `echo $?` immediately afterward returns `0`. A leading `-` in submodule status means it is not initialized.

`export` applies only to the current terminal. Set it again in new terminals, using your actual SDK path.

Initial configuration may fetch and build auxiliary tools such as picotool, requiring Git and Internet access. Cloning the SDK alone does not produce a fully offline environment.

References: [official Pico SDK repository](https://github.com/raspberrypi/pico-sdk), [official picotool repository](https://github.com/raspberrypi/picotool).

### Get the library

```sh
git clone https://github.com/tanep3/PicoW_Bluetooth_lib.git
```

Alternatively, extract the source ZIP and use that directory wherever `/path/to/PicoW_Bluetooth_lib` appears below. Keep the complete source tree, including `cmake`, `tools`, and `examples`.

<a id="source-build"></a>
## 3. Build a sample for your Bluetooth device

This walkthrough builds `sample_a2dp_source`, which sends ten seconds of silent audio to a Bluetooth speaker. For receiving audio or controlling the source player, use the [Sink examples below](#sink-avrcp) instead.

Find the speaker's [Classic Bluetooth address](#peer-address), then enter your library checkout. Replace `/path/to/` with its actual location:

```sh
cd /path/to/PicoW_Bluetooth_lib
export PICO_SDK_PATH="$HOME/pico/pico-sdk"
read -r -p "Speaker Bluetooth address: " PEER_BT_ADDRESS
: "${PEER_BT_ADDRESS:?Enter the actual speaker Classic Bluetooth address}"
cmake -S examples/DeveloperSamples -B .work/builds/samples-usb \
  -DSAMPLE_PEER="${PEER_BT_ADDRESS:?Set the actual Classic Bluetooth address}" \
  -DSAMPLE_USB_CONSOLE=ON -DSAMPLE_ALLOW_PAIRING=OFF \
  -DSAMPLE_CHANNEL_0=1 -DSAMPLE_CHANNEL_1=2
cmake --build .work/builds/samples-usb --target sample_a2dp_source --parallel 2
ls -lh .work/builds/samples-usb/sample_a2dp_source.{elf,uf2}
```

A successful build ends with `Built target sample_a2dp_source`. Flash **`.work/builds/samples-usb/sample_a2dp_source.uf2`** using [step 4](#flash).

- `SAMPLE_PEER` selects the device you intend to connect to.
- `SAMPLE_USB_CONSOLE=ON` selects USB serial for controls and status.
- `SAMPLE_ALLOW_PAIRING=OFF` applies to standalone SPP/AVRCP. **A2DP still allows pairing confirmation for its configured peer when started.**
- `SAMPLE_CHANNEL_0` and `SAMPLE_CHANNEL_1` are unused by this audio-only target. The shared configuration requires two different values in 1–30.
- `--parallel 2` limits compilation to two jobs. Use `1` if memory is limited.

The build does not flash the board or connect to Bluetooth. The firmware waits for `b` before starting the radio, then for `c` before connecting.

Use the sample-specific CMake entry point shown above. The root `CMakeLists.txt` and `make.sh` belong to the legacy static-library build.

For SPP, AVRCP Target, or combinations of profiles, choose a target and its settings from [DeveloperSamples](../examples/DeveloperSamples/README.md). Omit `--target` to build all seven examples. SPP channel numbers must match the services you use: outgoing `c` uses the peer's channels; listener `l` uses local Pico channels. Standalone SPP/AVRCP initial pairing requires `SAMPLE_ALLOW_PAIRING=ON` for the configured peer. Set these options when configuring your chosen sample.

CMake saves settings in its build directory. After changing settings, configure and build again. Set peer/pairing values explicitly; use a new `-B` directory when changing the SDK, compiler or board.

<a id="sink-avrcp"></a>
## Receive audio or control its source

Choose one of these complete projects. Each README provides its build command, output filename and serial controls, so there is no need to build `DeveloperSamples` first.

| Goal | Build and use | Startup behavior |
|---|---|---|
| Receive audio and send PLAY/PAUSE to the source player | [Media-control sample](../examples/AvrcpMediaControlSample/README.md) | USB console starts at boot; `START` initializes the radio |
| Pass received PCM to your own processing | [PCM consumer sample](../examples/A2dpSinkPcmConsumer/README.md) | Radio initialization starts at boot; wait for Ready before `l` |

For hardware use, set `AVRCP_MEDIA_ENABLE_RADIO=ON` or `SINK_PCM_ENABLE_RADIO=ON` as appropriate, and specify the actual, nonzero Bluetooth address of the **phone or computer sending audio**. Pairing is limited to that configured peer. The media console's pairing option enables a later `PAIR` command; it does not pair automatically at startup.

The media console uses uppercase whole-line commands; the PCM sample uses single characters. They do not use the `SAMPLE_*` settings. Follow the selected README rather than mixing commands from different samples.

Neither project drives a speaker. The media sample discards PCM; the PCM consumer counts frames and measures peaks by default. Recording is not needed for first use. For audible output, add your own audio hardware and `IAudioSink` implementation. The public media console supports PLAY/PAUSE, with no NEXT or volume command.

Keep the complete repository layout for these builds. To move an example into a separate app, follow [Your first app](getting_started.md).

<a id="peer-address"></a>
## Find the correct Bluetooth address

A **peer** is the device on the other side of Pico's connection. Use its **Classic Bluetooth address**, not its Wi-Fi MAC, USB serial number, friendly name, or an arbitrary BLE scan address.

| Pico's task | Address to put in the sample |
|---|---|
| Send audio to a speaker | The speaker's Bluetooth address |
| Receive/control audio from a Windows or Linux PC | The PC's actual Bluetooth adapter address, not the Pico address |
| Receive audio from a phone | The phone's Classic Bluetooth address |

For a **Linux source PC with BlueZ already available**, `bluetoothctl list` displays its local adapters. Read the address beside the intended adapter. For an already known remote speaker, `bluetoothctl devices` lists device addresses and names; verify the specific device rather than choosing by name alone. These listing commands do not start scanning or pair. See the [BlueZ command reference](https://github.com/bluez/bluez/blob/master/doc/bluetoothctl.rst). Do not use a build host's adapter address when the audio source is a different PC.

For a **Windows source PC**, start with Device Manager → Bluetooth → the manufacturer's Bluetooth radio → Properties → Advanced. Choose the actual adapter, not Microsoft Bluetooth Enumerator or RFCOMM Protocol TDI; [Microsoft describes this adapter-identification route](https://support.microsoft.com/en-us/windows/hardware/bluetooth/what-bluetooth-version-is-on-a-windows-device). If your driver exposes an Address/Bluetooth Radio Address field, copy that value. Fields vary by driver; the referenced Microsoft page identifies the radio but does not promise an address field. The Windows UI steps have not been verified for every driver.

If the address is absent, use that device/adapter manufacturer's instructions or another source whose Classic address you can verify. A phone may hide it. These fixed-peer samples do not accept an unknown first device by name; **do not guess an address or remove the peer restriction**. If shown as twelve hexadecimal digits or hyphen-separated pairs, format the same six pairs as `XX:XX:XX:XX:XX:XX` without changing their order. Use the address you verified on your device.

### Pairing and reconnecting

Pairing creates authentication information for the selected device. Carry out the peer-side pairing steps in your sample's instructions. A pairing option does not authorize other devices.

The samples use RAM-only keys. After a Pico restart, the peer may still hold an old key. If that prevents reconnection, remove only the Pico's entry on the peer and pair again. Legacy PIN pairing is not implemented in these APIs.

<a id="flash"></a>
## 4. Flash one selected UF2

**Flashing replaces the board's existing program.** If the board already serves another purpose, obtain its restoration UF2 and procedure first. This guide does not use automatic flashing commands.

1. Identify the target Pico W and unplug USB.
2. Hold **BOOTSEL**, connect USB, then release the button.
3. Confirm that the `RPI-RP2` drive appears.
4. Copy **one** intended `.uf2` onto that drive.
5. The drive disconnects and the board restarts with the new firmware. A USB-console build then appears as a serial device.

The enabled PCM consumer starts radio initialization at boot. DeveloperSamples waits for `b`; the enabled media console waits for `START`.

For WSL builds, you can open the file from Windows and copy it to `RPI-RP2`. Use a Windows serial terminal or separately arrange USB access in WSL. USB access is not needed merely to compile.

## 5. First use over USB serial

On Linux, compare the device list before/after connection and verify board identifiers. Do not open another device's serial port:

```sh
ls -l /dev/serial/by-id/
```

One terminal application is `picocom`, installable with `sudo apt install picocom`. Replace the illustrative port below with the verified actual port:

```sh
picocom -b 115200 /dev/serial/by-id/REPLACE_WITH_YOUR_PICO
```

USB CDC is virtual serial; 115200 here is a terminal setting, unlike the physical UART baud rate. Normally exit picocom with `Ctrl-A`, then `Ctrl-X`.

**Check which firmware you flashed before typing commands:**

| Firmware | Startup and controls |
|---|---|
| `sample_*` from DeveloperSamples | Send one character at a time: `b`, then `?`; follow the table below |
| `avrcp_media_control_sample` | Enabled build: `STATUS`/`START` as uppercase whole lines with Enter. Follow the [operator workflow](../examples/AvrcpMediaControlSample/README.md#operator-workflow) |
| `a2dp_sink_pcm_consumer` | Enabled build begins initialization automatically. Wait for Ready, then `l`/`?`; follow [first use](../examples/A2dpSinkPcmConsumer/README.md#first-use) |

### DeveloperSamples only

Even with a valid peer, **DeveloperSamples do not start the radio until you send `b`**. Send one character at a time. Startup messages may have appeared before the terminal opened. With a valid peer, send `b` once, then `?` to check status. An empty/invalid peer still cannot start with `b`; rebuild first.

| First example | Command sequence | What to check |
|---|---|---|
| `sample_a2dp_source` | `b` → `?` until power=2 → `c` → `?` | stream=4 (Playing) and increasing packets; natural completion after about 10 seconds; `d` disconnects manually |
| `sample_spp_client` | `b` → `?` until runtime=2 → `c` → `?` until route phase=4 → `w` | Greeting received by the peer; `d` disconnects |
| `sample_avrcp_target` | Build with pairing ON → `b` → Ready → `p` → peer connects/sends controls → `?` | Operation IDs and press/release events; `x` terminates AVRCP, requiring reset to restart |

**The A2DP learning samples are silent.** They send about ten seconds of 48 kHz, signed 16-bit interleaved stereo PCM. Silence alone is not evidence of failure; inspect state and packet counts first. Conversely, packet growth alone does not establish audio quality or physical speaker output.

To replace the source, implement your own data supply for `IAudioSource::read()` in place of `SilenceSource.h`. `samples` counts `int16_t` values across both channels, not stereo frames. Match the source rate to settings; there is no automatic resampling.

See [detailed sample instructions](../examples/DeveloperSamples/README.md) for three-profile connection order and SPP listeners. Do not assume `c` has the same role in every example.

`d` disconnects but does not necessarily power off the radio. SPP listeners also remain. Reset or unplug the board when appropriate after use. These examples have no independent watchdog guaranteeing radio shutdown if the host stops.

## 6. UART instead of USB

For DeveloperSamples, use a separate build directory for the UART variant. Keep the actual peer address from step 3:

```sh
: "${PEER_BT_ADDRESS:?Set the actual peer Classic Bluetooth address}"
cmake -S examples/DeveloperSamples -B .work/builds/samples-uart \
  -DSAMPLE_PEER="${PEER_BT_ADDRESS:?Set the actual Classic Bluetooth address}" -DSAMPLE_USB_CONSOLE=OFF \
  -DSAMPLE_ALLOW_PAIRING=OFF -DSAMPLE_CHANNEL_0=1 -DSAMPLE_CHANNEL_1=2
cmake --build .work/builds/samples-uart --target sample_a2dp_source --parallel 2
```

Pico W UART0 uses **GP0=TX, GP1=RX, 115200 baud, 8N1**. Connect adapter RX to GP0, adapter TX to GP1, and share GND. Disconnect power before wiring.

**Use a 3.3 V TTL adapter. Do not connect RS-232 or 5 V signals directly.** This guide does not assume that the adapter powers the board.

UART output can block, so avoid repeatedly sending `?`. USB output can drop logs under congestion; neither guarantees a complete log.

<a id="integration"></a>
## 7. Integrate with your own CMake project

Use **[Your first app](getting_started.md)** as the single complete copy/paste route. It provides the library layout, configuration, CMake and `main.cpp`, and an external Sink + Controller route. Do not combine pieces of different projects without their generated configuration and helper dependencies.

There is no `make install` or `find_package(PicoWBluetooth)` package. For an **existing Pico SDK application**, retain its SDK import/init and executable target. Add the selected helper after `pico_sdk_init()` and apply it to that target. The final argument is the directory containing your app's `btstack_config.h`, not the SDK directory. The Source-only configuration is not suitable for every profile combination.

Source-specific integration reminders:

- Keep `IAudioSource` and `A2dpSourcePlayer` alive for the program lifetime.
- Check `begin()` and continue calling `poll()` in the main loop.
- Set `settings.auto_connect=false` for manual connection; automatic connection is the default.
- `play_duration_us=0` means continuous playback by default. Ten seconds is a sample setting.
- Do not add independent `cyw43_arch_init()`, BTstack initialization, or legacy plugin targets.

For multiple profiles, add the relevant helpers to the same target:

| Profile | Include | Apply to target |
|---|---|---|
| A2DP Source | `cmake/picow_a2dp_source.cmake` | `picow_target_a2dp_source(target config_directory)` |
| A2DP Sink | `cmake/picow_a2dp_sink.cmake` | `picow_target_a2dp_sink(target config_directory)` |
| AVRCP Controller | `cmake/picow_avrcp_controller.cmake` | `picow_target_avrcp_controller(target config_directory)` |
| SPP | `cmake/picow_spp.cmake` | `picow_target_spp(target config_directory)` |
| AVRCP Target | `cmake/picow_avrcp_target.cmake` | `picow_target_avrcp_target(target config_directory)` |

For Sink + Controller, start from the complete [media-control project](../examples/AvrcpMediaControlSample/CMakeLists.txt): both helpers apply to the same executable, with one Sink and one Controller owner polled from the same loop. Do not initialize BTstack roles separately or infer that A2DP dual-role audio is enabled.

For the original Source/SPP/Target combination, start `btstack_config.h` settings from [DeveloperSamples](../examples/DeveloperSamples/btstack_config.h). Do not reuse audio-only settings unchanged; check [SPP resources/HCI settings](spp_sdk_integration.md). Poll each profile every iteration. Follow the [three-profile connection order](../examples/DeveloperSamples/a2dp_spp_avrcp.cpp) to avoid conflicting shared service discovery.

<a id="compile-only"></a>
## 8. Optional: build checks without hardware

A build-only check is optional, not a prerequisite for the hardware walkthrough. DeveloperSamples accepts an empty peer for this purpose. The Sink/media examples have a radio-OFF configuration of the **same sample**: it returns before USB and radio initialization, so it has no console. See the [PCM compile check](../examples/A2dpSinkPcmConsumer/README.md#compile-only) or [media compile check](../examples/AvrcpMediaControlSample/README.md#compile-only) if needed.

This script runs three sample host-test programs and builds all seven ARM targets. Initial execution can take time. It does not flash, start the radio, or pair:

```sh
SAMPLE_PEER="" SAMPLE_ALLOW_PAIRING=OFF SAMPLE_USB_CONSOLE=ON \
  examples/DeveloperSamples/check.sh .work/builds/samples-check
```

It uses `PICO_SDK_PATH` exported in step 2. Host tests use the computer's `c++`; ARM firmware compilation is a separate stage. UF2 outputs are under `.work/builds/samples-check/arm/`.

These do not establish actual peer connections, audio quality, or reliability in a radio environment.

The media and PCM examples also have host tests: `bash tests/host/run_avrcp_media_control_sample_tests.sh` and `bash tests/host/run_sink_pcm_sample_tests.sh`. Run them from a complete checkout.

## Advanced: Sink configuration validation

`picow_target_a2dp_sink` automatically selects the [bounded raw SBC configuration guard](sink_raw_configuration_contract.md). It needs a host Python3 interpreter and the pinned SDK acceptor source; an unfamiliar source hash fails configuration rather than silently omitting protection. The installed SDK is not edited. Source-only sibling targets retain their original acceptor. Use the helper rather than adding receiver C++ files manually. The same-peer experimental overlay cannot share this CMake configuration, even on another target; use separate configurations until composition is explicitly supported. The guard covers raw SBC configuration validation, not general signaling fragmentation.

For optional remote START decisions, call `picow_enable_sink_start_decisions(app)` after `picow_target_a2dp_sink(app ...)`, then set `Settings::require_start_decision=true`. Do not enable the SDK's explicit-start macro alone. The [dedicated example](../examples/A2dpSinkStartDecisionSample/README.md) documents separate feature and radio build options; default compilation does not start radio. See the [API/timing contract](sink_start_decisions.md).

<a id="troubleshooting"></a>
## 9. Troubleshooting

| Symptom | Check |
|---|---|
| Missing `pico_sdk_import.cmake` | Check `echo "$PICO_SDK_PATH"` and step 2's `test -f`; point to the SDK root |
| Missing `arm-none-eabi-gcc` / `g++` | Install the ARM toolchain and check `--version` in a new terminal; host `gcc` alone is insufficient |
| Missing `<algorithm>` / `libstdc++` | Install the ARM C++ standard library, such as `libstdc++-arm-none-eabi-newlib` on Ubuntu/Debian |
| Missing BTstack/CYW43/TinyUSB | Initialize SDK submodules recursively; do not copy arbitrary headers into this library |
| picotool fetch/build failure | Check network, Git, host C/C++ compiler, libusb development packages, and CMake discovery of any installed compatible version |
| picotool version mismatch | Check the requested version in the error; this SDK requires 2.1.1. If needed, use a fresh build directory with `-DPICOTOOL_FORCE_FETCH_FROM_GIT=ON` |
| ELF exists but UF2 does not | Check that the build finished; `PICO_NO_PICOTOOL` / `PICO_NO_UF2` prevent UF2 generation |
| Old SDK or wrong path used | Absolute paths may remain in CMake cache; configure a new `-B` directory without deleting existing outputs |
| Peer/channel validation error | Check the [peer-address guide](#peer-address). DeveloperSamples accepts an empty build-only peer; enabled Sink/media requires a nonzero address. SPP channels must be distinct and in 1–30 |
| Compiler is `Killed` | Possible memory exhaustion; retry with `--parallel 1`, checking OS logs and free space |
| No serial port or startup message | Radio-OFF Sink/media builds intentionally have no console. Otherwise check UF2, cable, port and console variant. DeveloperSamples: `b` then `?`; enabled media: `STATUS`/`START` plus Enter; enabled PCM: `?`. USB startup logs may be missed |
| Pico does not appear in the source's Bluetooth list | Enabled media needs `START` → READY → `PAIR` with pairing enabled. PCM needs `SINK_PCM_DISCOVERABLE_LISTEN=ON` and an accepted `l`; its window is 60 seconds. Ordinary `listen()` alone is not discovery |
| Serial `Permission denied` | Check port ownership/group and your access; follow OS procedures rather than loosening permissions on all devices |
| No A2DP sound | Original Source examples send silence; check stream=4 and packet growth. Sink/media-control examples count or discard PCM and have no speaker driver; inspect streaming/decoded-frame counters and provide an output consumer separately |
| Media console refuses a command | Check START/READY, the current binding and pending operation; do not send DeveloperSamples single-letter commands. Wait for matching responses and operator observation |
| Successful begin/connect but no connection | Acceptance is not completion. Inspect state/error/status, peer profiles/services, and pairing requirements |
| Cannot reconnect after reboot | Check RAM-only keys and stale peer bonds; remove only the relevant pairing and pair again if necessary |
| `c` does not restart | Startup may be incomplete, a connection may be pending, or a terminal error may require reset; inspect state rather than retry forever |

The validated toolchain reports newlib's `_getentropy is not implemented` warning when linking A2DP targets. UF2 generation succeeded with this known warning; this is not permission to ignore other warnings or errors.

<a id="verified"></a>
## Verified build environment

- Linux: Raspberry Pi 5 / Ubuntu 24.04 series
- Target: Pico W / RP2040
- Pico SDK: `9a4113fbbae65ee82d8cd6537963bc3d3b14bcca` (2.1.1 series)
- BTstack submodule: `501e6d2b86e6c92bfb9c390bcf55709938e25ac1`
- CMake 3.28.3, Arm GCC 13.2.1, picotool 2.1.1

The own-app walkthrough was ARM-built for Source and Sink + Controller in separate layouts without source dependencies on the original checkout, producing UF2 files. This verifies compilation/linking; Windows/WSL setup and hardware operation were not newly rerun. For test details and known limits, see [Validation scope](validation.md).
