#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
# Caller may provide a task-local directory; no device or SDK dependency.
build=$(mktemp -d "${TMPDIR:-/tmp}/sink-delay-flow.XXXXXX")
trap 'rm -rf "$build"' EXIT
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
for test in test_delay_flow test_sample_main; do
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -g -O1 \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    -I"$root" -I"$root/audio-core" -I"$root/tests/host/sink_delay_use_flow/stubs" \
    "$root/tests/host/sink_delay_use_flow/$test.cpp" -o "$build/$test"
"$build/$test"
done
