#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
bt=${BTSTACK_ROOT:-${PICO_SDK_PATH:?Set PICO_SDK_PATH or BTSTACK_ROOT}/lib/btstack}
: "${TMPDIR:?Set a task-local TMPDIR}"
build=$(mktemp -d "$TMPDIR/committed-tests.XXXXXX")
printf '%s\n' "$build"
printf '#define HAVE_ASSERT\n#define ENABLE_CLASSIC\n' > "$build/btstack_config.h"
flags=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
inc=(-I"$build" -I"$root/bluetooth-runtime" -I"$bt/src" -I"$bt/platform/embedded")
"${CC:-cc}" -std=c11 "${flags[@]}" "${inc[@]}" -c "$bt/src/btstack_tlv.c" -o "$build/tlv.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic "${flags[@]}" "${inc[@]}" -DPICOW_COMMITTED_TLV_PROVIDER=1 "$root/tests/host/committed_tlv/test_committed.cpp" "$root/bluetooth-runtime/PicoCommittedTlv.cpp" "$root/bluetooth-runtime/PicoPersistentBonds.cpp" "$build/tlv.o" -o "$build/committed-tests"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$build/committed-tests"
