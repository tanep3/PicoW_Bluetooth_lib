#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$root/.work/public-sink-usb"
build=$(mktemp -d "$root/.work/public-sink-usb/host.XXXXXX")
trap 'rm -rf "$build"' EXIT
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 PYTHONDONTWRITEBYTECODE=1
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -g -O1 \
 -fsanitize=address,undefined -fno-omit-frame-pointer \
 -I"$root/tests/host/sink_usb_capture/mocks" -I"$root/examples/A2dpSinkPcmConsumer" \
 "$root/tests/host/sink_usb_capture/test_capture.cpp" -o "$build/capture"
"$build/capture"
python3 "$root/tests/host/sink_usb_capture/test_parser.py"

cat > "$build/sample_config.h" <<'CONFIG'
#define SINK_PCM_ENABLE_RADIO 1
#define SINK_PCM_USB_CAPTURE 1
#define SINK_PCM_ALLOW_PAIRING 0
#define SINK_PCM_DISCOVERABLE_LISTEN 0
#define SINK_PCM_PEER_BYTES 0x02,0,0,0,0,0x01
CONFIG
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
inc=(-I"$root/tests/host/sink_usb_capture/mocks" -I"$root/tests/host/sink_discoverable_listen/mocks" -I"$build" -I"$root/audio-core" -I"$root/examples/A2dpSinkPcmConsumer")
"${CXX:-c++}" "${flags[@]}" "${inc[@]}" -Dmain=sample_main -c "$root/examples/A2dpSinkPcmConsumer/main.cpp" -o "$build/main.o"
"${CXX:-c++}" "${flags[@]}" "${inc[@]}" "$root/tests/host/sink_usb_capture/test_main.cpp" "$build/main.o" -o "$build/test-main"
"$build/test-main"
