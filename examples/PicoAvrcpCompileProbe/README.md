# AVRCP shared compile/link probe

[日本語](README_jp.md) | English

Compile-only check of A2DP source + raw SPP + AVRCP target with the polling runtime.
See `../../docs/avrcp_target_jp.md` for API, SDK response contract and limitations.
This is not a hardware test. Normal entry does not initialize Bluetooth.

    PICO_SDK_PATH=/path/to/pico-sdk cmake -S . -B ../../.work/builds/build-avrcp-target
    cmake --build ../../.work/builds/build-avrcp-target -j4

No flash, radio or audio operation is needed for this check.
