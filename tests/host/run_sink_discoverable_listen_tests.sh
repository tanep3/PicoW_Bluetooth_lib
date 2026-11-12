#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$root/.work/sink-discoverable-listen"
build=$(mktemp -d "$root/.work/sink-discoverable-listen/host-XXXXXXXX")
echo "Retained test build: $build"
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
src="$root/tests/host/sink_discoverable_listen"
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
for mode in inert default discoverable; do
    mkdir "$build/$mode"
    radio=1; discoverable=0
    if [[ "$mode" == inert ]]; then radio=0; discoverable=1; fi
    if [[ "$mode" == discoverable ]]; then discoverable=1; fi
    cat > "$build/$mode/sample_config.h" <<CONFIG
#pragma once
#define SINK_PCM_ENABLE_RADIO $radio
#define SINK_PCM_ALLOW_PAIRING 0
#define SINK_PCM_DISCOVERABLE_LISTEN $discoverable
#define SINK_PCM_PEER_BYTES 0x02,0,0,0,0,0x01
CONFIG
    inc=(-I"$src/mocks" -I"$build/$mode" -I"$root/audio-core")
    "${CXX:-c++}" "${flags[@]}" "${inc[@]}" -MMD -MF "$build/$mode/main.d" -Dmain=sample_main \
        -c "$root/examples/A2dpSinkPcmConsumer/main.cpp" -o "$build/$mode/main.o"
    "${CXX:-c++}" "${flags[@]}" "${inc[@]}" -MMD -MF "$build/$mode/harness.d" \
        -c "$src/test_main.cpp" -o "$build/$mode/harness.o"
    "${CXX:-c++}" "${flags[@]}" "$build/$mode/main.o" "$build/$mode/harness.o" -o "$build/$mode/test"
    if [[ "$mode" == discoverable ]]; then
        for scenario in startup_idle identity repeat_timeout rejected cancel outgoing reopen wrap signaling failed not_working closing stop_admission streaming connecting short_session; do
            "$build/$mode/test" "$scenario"
        done
    else
        "$build/$mode/test" "$mode"
    fi
done
sha256sum "$root/examples/A2dpSinkPcmConsumer/"{main.cpp,DiscoverableListenWindow.h,BoundedPcmSink.h,ReportFraming.h,CMakeLists.txt,sample_config.h.in} \
    "$root/audio-core/"{A2dpSinkReceiver.h,IAudioSink.h} "$src/test_main.cpp" \
    "$src/mocks/btstack.h" "$src/mocks/pico/stdlib.h" "$0" "$build"/*/sample_config.h > "$build/input-sha256.txt"
echo 'PASS: 18 fixed main-loop scenarios; modeled receiver/SDK only, no hardware'
