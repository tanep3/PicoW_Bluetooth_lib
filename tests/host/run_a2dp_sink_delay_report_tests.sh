#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
export ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
bt=${BTSTACK_ROOT:-${PICO_SDK_PATH:?Set PICO_SDK_PATH}/lib/btstack}
printf '#define HAVE_ASSERT\n' > "$build/btstack_config.h"
flags=(-g -O1 -fsanitize=address -fno-omit-frame-pointer)
inc=(-I"$build" -I"$root/audio-core/sink" -I"$root/audio-core" -I"$root/bluetooth-runtime" -I"$bt/src" -I"$bt/src/classic" -I"$bt/3rd-party/bluedroid/encoder/include" -I"$bt/3rd-party/bluedroid/decoder/include" -I"$bt/3rd-party/yxml")
# Upstream fixed-point arithmetic uses signed shifts; do not apply UBSan to it.
for src in "$bt/src/btstack_util.c" "$bt/src/classic/btstack_sbc_bluedroid.c" "$bt/src/classic/btstack_sbc_plc.c" "$bt"/3rd-party/bluedroid/{encoder,decoder}/srce/*.c; do
  cc "${flags[@]}" "${inc[@]}" -c "$src" -o "$build/$(basename "$src").o"
done
c++ -std=c++17 -Wall -Wextra -Werror "${flags[@]}" -fsanitize=undefined -fno-sanitize-recover=undefined "${inc[@]}" -DENABLE_CLASSIC -DHCI_ACL_PAYLOAD_SIZE=1028 "$root/tests/host/a2dp_sink_delay_report/test_delay.cpp" "$build"/*.o -lm -o "$build/test_delay"
"$build/test_delay"
echo "PASS C++ facade/guards: ASan+UBSan; upstream C codec objects: ASan only"
python3 "$root/tests/host/a2dp_sink_delay_report/extract_sdk_fixture.py" "$bt" "$build/sdk_fixture.cpp"
c++ -std=c++17 -Wall -Wextra -Werror "${flags[@]}" -fsanitize=undefined -fno-sanitize-recover=undefined "${inc[@]}" -DENABLE_CLASSIC -DHCI_ACL_PAYLOAD_SIZE=1028 "$build/sdk_fixture.cpp" "$build/btstack_util.c.o" -o "$build/sdk_fixture"
"$build/sdk_fixture"
echo "PASS exact extracted delay/serializer C++ fixture: ASan+UBSan; original license headers retained"
