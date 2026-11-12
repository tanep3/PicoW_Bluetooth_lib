#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
build=${1:-/tmp/picow-developer-samples}
arm=${ARM_BUILD_DIR:-$build/arm}
mkdir -p "$build"
for test in policy pending pairing; do
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I"$here/host_mocks" "$here/test_${test}.cpp" -o "$build/test_${test}"
  "$build/test_${test}"
done
: "${PICO_SDK_PATH:?Set PICO_SDK_PATH to an installed Pico SDK}"
cmake -S "$here" -B "$arm" -DSAMPLE_PEER="${SAMPLE_PEER:-}" \
  -DSAMPLE_CHANNEL_0=1 -DSAMPLE_CHANNEL_1=2 \
  -DSAMPLE_USB_CONSOLE="${SAMPLE_USB_CONSOLE:-ON}" \
  -DSAMPLE_ALLOW_PAIRING="${SAMPLE_ALLOW_PAIRING:-OFF}"
cmake --build "$arm" -j"${JOBS:-2}"
# Compile only: never flash, pair, or contact a peer here.
