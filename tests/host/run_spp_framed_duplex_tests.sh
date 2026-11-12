#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
build=${SPP_DUPLEX_TEST_BUILD:-$(mktemp -d)}
mkdir -p "$build"
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -g -O1 \
 -fsanitize=address,undefined -fno-omit-frame-pointer -I"$root" \
 "$root/tests/host/spp_framed_duplex/test.cpp" -o "$build/test"
"$build/test"
