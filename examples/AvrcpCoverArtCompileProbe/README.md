# AvrcpCoverArtCompileProbe

[日本語](README_jp.md)

This is a **compile/link probe**, not a runtime usage example or a hardware test. It makes Cover Art Client plus shared signalling/Browsing symbols reachable so the linker checks the current public API against the pinned Pico SDK and generated BTstack overlays. `initialize_for_link_check` is false by default: the program returns without calling `begin` or starting the radio. Do not turn that flag on and treat the sequential API calls as a working Bluetooth workflow. They deliberately exercise APIs without waiting for remote connection, response, credits or teardown.

## Build only

From the repository root, with the supported SDK installed:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S examples/AvrcpCoverArtCompileProbe -B build/build-avrcp-cover-art-compile-probe -DCMAKE_BUILD_TYPE=Debug
cmake --build build/build-avrcp-cover-art-compile-probe -j4
```

The generated ELF/UF2 are under that build directory. The helpers select hash-checked build-directory overlays only for this consumer; the installed SDK is not edited. A new/modified SDK fails closed instead of silently receiving the wrong patch.

## What this proves

- Public headers/types and reachable APIs compile for Pico W.
- The selected profile objects and SDK dependencies link together.
- ELF/UF2 generation succeeds for this configuration.

It does **not** prove real peer interoperability, response order, all AVRCP semantic behavior, audio output, resources under simultaneous traffic or long-duration stability. Do not flash or record as part of a build-only check.

For supported bounds, ownership and the real execution plan, read [Validation scope](../../docs/validation.md), [Controller](../../docs/avrcp_controller.md), [Browsing](../../docs/avrcp_browsing.md) and [Cover Art](../../docs/avrcp_cover_art.md). Shared roles are single-main-loop/process-lifetime owners. Borrowed callback bytes must be copied before later use; local API success is not a remote result.

## Troubleshooting configuration and linking

For missing SDK/toolchain dependencies, use the [build guide](../../docs/build_guide.md). An overlay hash mismatch indicates an SDK mismatch: use the supported SDK rather than bypassing the check. The default firmware returning without doing anything is expected.
