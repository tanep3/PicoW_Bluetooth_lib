#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
: "${PICO_SDK_PATH:?Set PICO_SDK_PATH to the unmodified pinned SDK}"
bt=${BTSTACK_ROOT:-$PICO_SDK_PATH/lib/btstack}
build=${OUTPUT_DIR:-$(mktemp -d)}
mkdir -p "$build"
if [[ -n ${CONTROLLER_OVERLAY:-} ]]; then cp "$CONTROLLER_OVERLAY" "$build/controller_overlay.c"; else
 python3 "$root/tools/patch_avrcp_controller.py" "$bt/src/classic/avrcp_controller.c" "$build/controller_overlay.c"
fi
printf '#define HAVE_ASSERT\n#define HCI_ACL_PAYLOAD_SIZE 1028\n' > "$build/btstack_config.h"
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections \
 -I"$build" -I"$bt/src" -I"$bt/3rd-party/bluedroid/encoder/include" -I"$bt/3rd-party/bluedroid/decoder/include" -I"$bt/3rd-party/yxml" \
 "$root/tests/host/avrcp_pass_through_correlation/test_wire.c" "$bt/src/btstack_util.c" -Wl,--gc-sections -o "$build/wire"
"$build/wire" "$@"
echo "Evidence retained: $build"
