#!/usr/bin/env bash
set -euo pipefail
export PATH=/usr/bin:/bin:/usr/local/bin PYTHONPATH= PYTHONNOUSERSITE=1 PYTHONDONTWRITEBYTECODE=1
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$root/.work"
build=$(mktemp -d "$root/.work/avrcp-media-control-host.XXXXXX")
export TMPDIR="$build/tmp"
mkdir -p "$TMPDIR"
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1
printf 'Build directory: %s\n' "$build"
exec > >(tee "$build/results.log") 2>&1
trap 'rc=$?; printf "Runner exit status: %s\n" "$rc"; printf "%s\n" "$rc" > "$build/exit-status"' EXIT
{
 date -u +%FT%TZ
 /usr/bin/c++ --version | head -n 1
 uname -m
 sha256sum "$root/examples/AvrcpMediaControlSample/"{Application.h,CommandParser.h,main.cpp} \
   "$root/tests/host/avrcp_media_control_sample/test_application.cpp" \
   "$root/tests/host/avrcp_media_control_sample/stubs/Model.h" "$0"
} > "$build/manifest.txt"
for variant in inert enabled pairing; do
 flags=()
 if [[ "$variant" != inert ]]; then flags+=(-DAVRCP_MEDIA_ENABLE_RADIO=1); fi
 if [[ "$variant" == pairing ]]; then flags+=(-DAVRCP_MEDIA_ALLOW_PAIRING=1); fi
 /usr/bin/c++ -std=c++17 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Wall -Wextra -Werror -pedantic -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Wl,--wrap=printf,--wrap=puts \
  "${flags[@]}" -I"$root/tests/host/avrcp_media_control_sample/stubs" -I"$root" \
  "$root/tests/host/avrcp_media_control_sample/test_application.cpp" -o "$build/$variant"
 if [[ "$variant" == inert ]]; then
  for scenario in default parser preinit start_once pair listen no_binding lifetime hci_off \
   startup_timeout begin_failure queue_overflow poll_overflow query_copy query_reject query_stale \
   stale_input timeout sdk_timeout stale_event click press_reject release_reject release_local_fail \
   foreign_hci unapproved_pairing allowed_pairing malformed_status disconnect_no_retry \
   local_reject operation_mismatch release_without_press \
   disconnect_a2dp_only disconnect_stale disconnect_live disconnect_reject \
   poll_overflow_press poll_overflow_query; do
   "$build/$variant" "$scenario"
  done
 else
  for scenario in console console_stale console_pair; do "$build/$variant" "$scenario"; done
 fi
done
