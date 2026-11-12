#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -g -O1 \
 -fsanitize=address,undefined -fno-omit-frame-pointer -I"$root" -I"$root/audio-core" \
 "$root/tests/host/sink_pcm_sample/test_pcm_sink.cpp" -o "$build/pcm"
"$build/pcm"
