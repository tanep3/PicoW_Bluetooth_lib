#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
bt=${BTSTACK_ROOT:-${PICO_SDK_PATH:?Set PICO_SDK_PATH or BTSTACK_ROOT}/lib/btstack}
: "${TMPDIR:?Set a task-local TMPDIR}"
build=$(mktemp -d "$TMPDIR/persistent-tests.XXXXXX")
# Keep build products for inspection. No downloads, device access, or key logging.
printf '%s\n' "$build"
cat > "$build/btstack_config.h" <<'CONFIG'
#define HAVE_ASSERT
#define ENABLE_CLASSIC
CONFIG
flags=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
inc=(-I"$build" -I"$root/bluetooth-runtime" -I"$bt/src" -I"$bt/platform/embedded")
for source in "$bt/src/btstack_tlv.c" "$bt/src/btstack_util.c" "$bt/platform/embedded/btstack_tlv_flash_bank.c"; do
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror "${flags[@]}" "${inc[@]}" -c "$source" -o "$build/$(basename "$source").o"
done
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic "${flags[@]}" "${inc[@]}" "$root/tests/host/persistent_bonds/test_persistent.cpp" "$root/bluetooth-runtime/PicoPersistentBonds.cpp" "$build/"*.o -o "$build/persistent-tests"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$build/persistent-tests"
