#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
build=${BUILD_DIR:?Set a fresh isolated BUILD_DIR}
mkdir -p "$build"
export ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
bt=${BTSTACK_ROOT:-${PICO_SDK_PATH:?Set PICO_SDK_PATH}/lib/btstack}
printf '#define HAVE_ASSERT\n' > "$build/btstack_config.h"
flags=(-MMD -g -O1 -fsanitize=address -fno-omit-frame-pointer)
inc=(-I"$build" -I"$root/audio-core/sink" -I"$root/audio-core" -I"$root/bluetooth-runtime" -I"$bt/src" -I"$bt/src/classic" -I"$bt/3rd-party/bluedroid/encoder/include" -I"$bt/3rd-party/bluedroid/decoder/include" -I"$bt/3rd-party/yxml")
# Upstream fixed-point arithmetic uses signed shifts; do not apply UBSan to it.
for src in "$bt/src/btstack_util.c" "$bt/src/classic/btstack_sbc_bluedroid.c" "$bt/src/classic/btstack_sbc_plc.c" "$bt"/3rd-party/bluedroid/{encoder,decoder}/srce/*.c; do
  cc "${flags[@]}" "${inc[@]}" -c "$src" -o "$build/$(basename "$src").o"
done
c++ -std=c++17 -Wall -Wextra -Werror "${flags[@]}" -fsanitize=undefined -fno-sanitize-recover=undefined "${inc[@]}" -DENABLE_CLASSIC -DHCI_ACL_PAYLOAD_SIZE=1028 "$root/tests/host/sink_fragment_boundary/test_boundary.cpp" "$build"/*.o -lm -o "$build/test_boundary"
"$build/test_boundary"
c++ -std=c++17 -Wall -Wextra -Werror "${flags[@]}" -fsanitize=undefined -fno-sanitize-recover=undefined "${inc[@]}" -DENABLE_CLASSIC -DHCI_ACL_PAYLOAD_SIZE=1028 "$root/tests/host/sink_fragment_boundary/test_facade.cpp" "$root/audio-core/A2dpSinkReceiver.cpp" "$build"/*.o -lm -o "$build/test_facade"
"$build/test_facade"
