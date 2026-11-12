#!/usr/bin/env bash
set -euo pipefail
export PATH=/usr/bin:/bin:/usr/local/bin PYTHONPATH= PYTHONNOUSERSITE=1 PYTHONDONTWRITEBYTECODE=1
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$root/.work"
build=$(mktemp -d "$root/.work/avrcp-main-loop.XXXXXX")
export TMPDIR="$build/tmp"
mkdir -p "$TMPDIR"
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1
printf 'Build directory: %s\n' "$build"
/usr/bin/c++ -std=c++17 -Wall -Wextra -Werror -pedantic -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Wl,--wrap=printf,--wrap=puts \
  -I"$root/tests/host/avrcp_sample_main_loop/stubs" -I"$root" \
  "$root/tests/host/avrcp_sample_main_loop/test_sample_main.cpp" -o "$build/main"
for scenario in image thumbnail properties no_input early_input query_reset browse_reject \
  connect_reject get_reject complete_error complete_operation complete_request complete_count \
  cancel queue_loss signal_stale browse_stale disconnected \
  copied_peer copied_epoch copied_generation copied_cid copied_avrcp_cid \
  live_peer live_epoch live_generation live_cid live_avrcp_cid; do
  "$build/main" "$scenario"
done
