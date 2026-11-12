# Your first Pico W Bluetooth application

[日本語](getting_started_jp.md) · [README](../README.md) · [Tool installation](build_guide.md)

Create a standalone C++ project, add this library, and build a UF2 to run on your Pico W. This guide uses the Pico SDK, not Arduino or MicroPython.

## Choose what your Pico will do

| You want to… | Choose | Start below |
|---|---|---|
| Send audio from your program to a Bluetooth speaker | A2DP **Source** | **A: a small app you write** |
| Receive audio from a phone/computer and send Play/Pause back | A2DP **Sink** + AVRCP **Controller** | **B: copy the complete console app** |
| Receive audio without controls | A2DP **Sink** | [Receiver example](../examples/A2DPSinkReceiver/README.md) |
| Receive remote buttons or exchange arbitrary bytes | AVRCP **Target** or **SPP** | [Developer samples](developer_samples.md) |

Source/Sink name the direction of **audio**. Controller/Target name the direction of **commands**. Receiving audio does not make the Pico an AVRCP Target.

Choose **one** route, A or B. A shows the complete source for a small app. B copies the working structure of the supplied console, including connection, button-release, queue and shutdown handling.

**These apps do not provide speaker playback.** A sends ten seconds of silent PCM; replace its audio source with your own data later. B receives and discards decoded PCM. To hear received audio, supply a nonblocking `IAudioSink` implementation and audio output hardware. The [PCM consumer example](../examples/A2dpSinkPcmConsumer/README.md) shows how to process received samples without recording them.

## Before typing the commands

These commands use a Bash terminal on Linux. Have:

- CMake, Make or Ninja, Python 3, Git and an Arm cross-compiler (`arm-none-eabi-gcc` / `arm-none-eabi-g++`) with the Arm C++ standard library.
- An installed **Pico SDK with BTstack, CYW43 and TinyUSB submodules**, plus its build tools, including picotool. Use the SDK revision in the [build guide](build_guide.md); the integration helpers check for a compatible SDK layout.
- A **complete library source checkout**, including `cmake`, `tools`, `audio-core`, `avrcp-sdk`, `avrcp-core`, `bluetooth-runtime` and `examples`. A header downloaded on its own is not enough.
- To run the firmware: a **Pico W**, USB data cable, serial terminal and an authorized Bluetooth Classic peer. An ordinary Pico has no radio. A Raspberry Pi computer is a possible build host, not the target board.

Set the SDK path in this terminal (change the value to your installation):

```sh
export PICO_SDK_PATH="$HOME/pico/pico-sdk"
cmake --version
arm-none-eabi-g++ --version
python3 --version
test -f "$PICO_SDK_PATH/external/pico_sdk_import.cmake"
test -f "$PICO_SDK_PATH/lib/btstack/src/btstack.h"
```

The `test` commands print nothing on success; check their exit status if unsure. Initial CMake configuration may fetch auxiliary tools such as picotool, so it can require Internet access. Finish [tool setup](build_guide.md) first. If a compatible picotool is already installed outside CMake’s search path, add `-Dpicotool_DIR=/path/to/picotool/cmake-directory`, using the directory containing `picotoolConfig.cmake`. Do not disable picotool if you need UF2 output. Fully offline builds require the SDK and auxiliary tools to be prepared in advance.

## A. Write a small Source application

The app owns an executable target, `my_player`. The CMake helper attaches this library’s sources and SDK dependencies to it. Do **not** add the library root with `add_subdirectory()` or link the legacy `picow_bluetooth_lib` target for these public APIs.

### A1. Put the library inside your project

Start with a new, empty `my-player` directory and clone the complete library into `my-player/vendor/PicoW_Bluetooth_lib`:

```sh
mkdir -p my-player/vendor
git clone https://github.com/tanep3/PicoW_Bluetooth_lib.git my-player/vendor/PicoW_Bluetooth_lib
cd my-player
cp vendor/PicoW_Bluetooth_lib/examples/A2DPSourcePlayer/btstack_config.h .
```

If using a source ZIP, replace the clone step by placing the complete extracted directory at `my-player/vendor/PicoW_Bluetooth_lib`, then continue at `cd my-player`. Avoid an extra nested folder: `vendor/PicoW_Bluetooth_lib/cmake/picow_a2dp_source.cmake` must exist.

Create `CMakeLists.txt` and `main.cpp` below. The resulting layout is:

```text
my-player/
  CMakeLists.txt
  main.cpp
  btstack_config.h
  vendor/
    PicoW_Bluetooth_lib/
      cmake/picow_a2dp_source.cmake
      audio-core/
      bluetooth-runtime/
      ...
```

`btstack_config.h` configures the Bluetooth stack's features and buffers. This example copies the **A2DP-only** configuration. Keep it in your app's directory, not in the SDK.

### A2. Add the library to your CMake target

Save this as `my-player/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico_w)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(MyPlayer C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
pico_sdk_init()

set(PICOW_BLUETOOTH_ROOT "${CMAKE_CURRENT_LIST_DIR}/vendor/PicoW_Bluetooth_lib")
add_executable(my_player main.cpp)
include("${PICOW_BLUETOOTH_ROOT}/cmake/picow_a2dp_source.cmake")
picow_target_a2dp_source(my_player "${CMAKE_CURRENT_LIST_DIR}")
pico_enable_stdio_usb(my_player 1)
pico_enable_stdio_uart(my_player 0)
target_compile_definitions(my_player PRIVATE PICO_STDIO_USB_STDOUT_TIMEOUT_US=0)
pico_add_extra_outputs(my_player)
```

For an existing Pico SDK application, keep its SDK import/init and existing `add_executable(...)`. Add the library path, `include(...)`, and `picow_target_a2dp_source(your_target your_config_directory)` **after `pico_sdk_init()`**. Enable `PICO_CXX_ENABLE_EXCEPTIONS` before SDK initialization. Do not create a second target with the same name.

The helper adds the implementation files and include directories, and links `pico_stdlib`, `pico_cyw43_arch_poll`, `pico_btstack_classic`, `pico_btstack_cyw43`, and `pico_btstack_sbc_encoder`. You do not need to repeat that list or build a separate `.a` archive.

### A3. Include the headers and write your app

Save this complete example as `my-player/main.cpp`. **Before building for a board, replace the empty `settings.target_address` with your speaker's [Classic Bluetooth address](build_guide.md#peer-address).**

```cpp
#include "pico/stdlib.h"
#include "A2dpSourcePlayer.h"
#include "IAudioSource.h"
#include <algorithm>
#include <cstdio>

class Silence final : public IAudioSource {
public:
    bool hasSamples() const override { return true; }
    size_t read(int16_t* output, size_t samples) override {
        std::fill_n(output, samples, int16_t{0});
        return samples; // int16_t values, with left/right interleaved
    }
};

int main() {
    stdio_init_all();
    static Silence source;
    A2dpSourcePlayer::Settings settings;
    settings.target_address = ""; // Set your authorized peer's Bluetooth address.
    settings.sample_rate = 48000;
    settings.auto_connect = false;
    settings.play_duration_us = 10000000; // Ten seconds, then disconnect.
    static A2dpSourcePlayer player(source, settings);

    puts("b=start radio; c=connect; d=disconnect; ?=status");
    while (getchar_timeout_us(0) != 'b') tight_loop_contents();
    if (player.begin() != A2dpSourcePlayer::BeginResult::Ok) {
        puts("begin failed: check the peer/settings, then reset.");
        while (true) tight_loop_contents();
    }
    while (true) {
        player.poll();
        const auto state = player.snapshot();
        const int command = getchar_timeout_us(0);
        if (command == 'c' && state.power == a2dp::Power::Working &&
            state.error == a2dp::Error::None &&
            state.inquiry == a2dp::Inquiry::Idle &&
            state.signaling == a2dp::Signaling::Idle &&
            state.stream == a2dp::Stream::Idle) player.connect();
        if (command == 'd') player.disconnect();
        if (command == '?')
            printf("power=%u stream=%u error=%u packets=%lu\n",
                unsigned(state.power), unsigned(state.stream),
                unsigned(state.error), (unsigned long)player.diagnostics().sent_packets);
    }
}
```

The template leaves the address empty so an unchanged copy cannot connect to an unintended device. Set it before the build below. For a compile-only check, you can leave it empty; `begin()` will then fail without starting the radio.

With a valid address, `b` initializes the radio and allows pairing confirmation for that peer. `?` shows the state; when `power=2` and idle, `c` requests one connection. There is no automatic retry. Reset after a terminal error.

This source supplies silent 48 kHz, signed 16-bit stereo PCM. `samples` counts `int16_t` values, not bytes or stereo frames. Check `stream=4` (Playing) and increasing packet count; silence is not a failed audio output. `d` disconnects audio, but does not promise radio power-off. See the [API reference](api_reference.md) before replacing the source or adding other behavior.

### A4. Build your application's UF2

From `my-player`, with an installed Pico SDK and Arm compiler:

```sh
export PICO_SDK_PATH="$HOME/pico/pico-sdk" # Your actual SDK location.
cmake -S . -B build
cmake --build build --target my_player --parallel 2
ls -lh build/my_player.{elf,uf2}
```

Success ends with `Built target my_player`. The output is **`my-player/build/my_player.uf2`**. Continue to [flashing and first use](#flash-and-run). These commands build the app without flashing a board.

For other profiles, use the matching helper and configuration from the [integration guide](build_guide.md#integration). The Source-only `btstack_config.h` above is not suitable for every combination.

## B. Make the Sink + Controller console your own app

Start from the complete supplied console and adapt it to your application. Run these commands from the directory in which you want to create **a new** `my-receiver` project. If using a source ZIP, replace the clone step by placing the complete extracted library at `my-receiver/vendor/PicoW_Bluetooth_lib`:

```sh
mkdir -p my-receiver/vendor
git clone https://github.com/tanep3/PicoW_Bluetooth_lib.git \
  my-receiver/vendor/PicoW_Bluetooth_lib
cd my-receiver
cp vendor/PicoW_Bluetooth_lib/examples/AvrcpMediaControlSample/{main.cpp,Application.h,CommandParser.h,sample_config.h.in,CMakeLists.txt} .
cp vendor/PicoW_Bluetooth_lib/examples/A2DPSinkReceiver/btstack_config.h .
python3 - <<'PYTHON'
from pathlib import Path
p = Path("CMakeLists.txt")
s = p.read_text()
old = "${CMAKE_CURRENT_LIST_DIR}/../../cmake/"
assert s.count(old) == 2, "Example changed; review its CMake includes first"
p.write_text(s.replace(old,
    "${CMAKE_CURRENT_LIST_DIR}/vendor/PicoW_Bluetooth_lib/cmake/"))
PYTHON
```

The full source checkout stays under `vendor`; do not copy only the example directory. We copy the actual Sink `btstack_config.h`, not the console’s relative-path wrapper, so the configuration remains valid in your new directory. The two CMake include paths now refer to the vendored helpers.

```text
my-receiver/
  CMakeLists.txt
  main.cpp
  Application.h
  CommandParser.h
  sample_config.h.in
  btstack_config.h
  vendor/PicoW_Bluetooth_lib/   (complete library)
  build/                      (created by CMake)
    sample_config.h           (generated; do not hand-edit)
```

In the copied CMake file, these calls attach both roles to the **same** executable:

```cmake
picow_target_a2dp_sink(avrcp_media_control_sample ${CMAKE_CURRENT_LIST_DIR})
picow_target_avrcp_controller(avrcp_media_control_sample ${CMAKE_CURRENT_LIST_DIR})
```

Keep the copied `configure_file(...)`, build-directory include path, watchdog link, USB settings and application headers. The console needs them to build and run. The generated `sample_config.h` comes from the CMake options.

### B1. Set the source address and build

Use the [Classic Bluetooth address](build_guide.md#peer-address) of the **phone or computer sending audio**, not the Pico or its Wi-Fi adapter. From `my-receiver`, enter that address and build:

```sh
read -r -p "Audio source Bluetooth address: " SOURCE_BT_ADDRESS
: "${PICO_SDK_PATH:?Set the path to your Pico SDK}"
: "${SOURCE_BT_ADDRESS:?Enter the actual audio source Classic Bluetooth address}"
cmake -S . -B build \
  -DAVRCP_MEDIA_ENABLE_RADIO=ON -DAVRCP_MEDIA_ALLOW_PAIRING=ON \
  -DAVRCP_MEDIA_PEER="${SOURCE_BT_ADDRESS:?Set the actual Classic Bluetooth address}"
cmake --build build --target avrcp_media_control_sample --parallel 2
ls -lh build/avrcp_media_control_sample.{elf,uf2}
```

The output is **`my-receiver/build/avrcp_media_control_sample.uf2`**. A valid address has six hexadecimal pairs separated by colons; an all-zero address is rejected for radio use.

This build starts the USB console at boot, but waits for `START` before initializing the radio. `AVRCP_MEDIA_ALLOW_PAIRING=ON` permits a later `PAIR` command to open a five-minute pairing window for the configured peer. It does not pair automatically at startup.

<a id="flash-and-run"></a>
## Flash and run the app

Follow the [BOOTSEL/USB instructions](build_guide.md#flash) to copy the chosen UF2 to your Pico W and open its serial port. Flashing replaces the board's current program, so keep any firmware you may need to restore.

- **Route A:** flash `build/my_player.uf2`. Send `b`, then `?`. Once `power=2` and idle, send `c` once. `stream=4` and increasing `packets` indicate transmission. This app sends silence and disconnects after about ten seconds of playback. `d` requests disconnection; it does not power off the radio.
- **Route B:** flash `build/avrcp_media_control_sample.uf2`. Follow the [media console workflow](../examples/AvrcpMediaControlSample/README.md#operator-workflow): `START`, wait for READY, `PAIR`, then pair from the source and select Pico as its audio output. The README covers connecting AVRCP, checking PLAY/PAUSE on the source player, and ending with `SHUTDOWN`. Commands are uppercase whole lines, not the single letters used by route A.

The media console provides PLAY/PAUSE, not NEXT or volume commands. Neither app outputs sound through the Pico. Check the reported state and the source player's screen rather than treating a local command return value as proof of success.

<a id="compile-only"></a>
## Optional: compile without a board or peer

You do not need this step before building the hardware configuration. In route A, leave `settings.target_address` empty for a build-only check.

In route B, configure the **same app** with radio and pairing OFF in a separate output directory:

```sh
cmake -S . -B build-compile-only \
  -DAVRCP_MEDIA_ENABLE_RADIO=OFF -DAVRCP_MEDIA_ALLOW_PAIRING=OFF \
  -DAVRCP_MEDIA_PEER=00:00:00:00:00:00
cmake --build build-compile-only --target avrcp_media_control_sample --parallel 2
```

That firmware returns before USB or radio initialization. It has no serial console and does not appear in Bluetooth discovery. Use `build/avrcp_media_control_sample.uf2` from B1 for hardware use, not this compile-check output.

Both standalone-app recipes have been ARM-built to UF2 in independent directories. That verifies compilation and linking; these exact apps have not had separate hardware trials.

## Common first-run problems

| Symptom | What to check |
|---|---|
| Cannot find `pico_sdk_import.cmake` | Export the real SDK path in the current terminal before configuring. |
| BTstack/CYW43/TinyUSB missing | Finish SDK submodule setup; do not download unrelated headers into your app. |
| picotool missing in offline mode / no UF2 | Install/locate the SDK-compatible tool using the build guide, then configure again. An ELF alone is not the requested UF2 result. |
| C++ exception support error in A | Keep `set(PICO_CXX_ENABLE_EXCEPTIONS 1)` before `pico_sdk_init()`. |
| Sink overlay or linker layout rejected | Use the supported SDK revision. Do not bypass the guard or hand-edit the installed SDK. |
| Cannot find `Application.h` or `sample_config.h` | Copy all B files, retain `configure_file` and the binary-directory include. |
| No USB console in B | Check that you flashed B1’s `build/avrcp_media_control_sample.uf2`. The optional radio-OFF compile-check output has no console. |
| A prints `begin failed` | Set the speaker address in `settings.target_address`, then rebuild and flash. An empty address cannot start the radio. |
| Pairing fails after reset | Keys default to RAM; the peer may retain a stale key. Remove only this trial Pico’s pairing and re-pair deliberately. |
| Connected but silent | A sends silence; B discards PCM. Neither proves audible playback. |
| PAUSE appears accepted but player keeps going | Check matching responses and the selected player/OS media focus; a local return code alone is not success. |

## When you start changing the app

Keep each role owner alive for the application lifetime. Poll all active roles from the same main loop. Do not mix these owners with legacy plugins, initialize BTstack twice, or call them from another core/interrupt. Keep callbacks short; copy borrowed callback data before deferring work.

Next: [API reference](api_reference.md) for actual signatures and return values, [integration guide](build_guide.md#integration) for configuration choices, and [PCM handoff](a2dp_sink_pcm.md) when implementing your own output.
