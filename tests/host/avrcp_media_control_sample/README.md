# AVRCP Media Control sample host tests

Run `bash tests/host/run_avrcp_media_control_sample_tests.sh` from any directory.
The runner creates a unique `.work/avrcp-media-control-host.*` directory per
attempt and retains binaries, results.log, manifest.txt, and exit-status.
It does not overwrite prior results. Compiler warnings are errors, and all
three variants run with AddressSanitizer and UndefinedBehaviorSanitizer.

## What is actually compiled

`test_application.cpp` includes the **real**
`examples/AvrcpMediaControlSample/main.cpp`, which includes the real
Application.h and CommandParser.h. The entry point is renamed only in the test.
The default entry is invoked unchanged and must return before stdio, owners,
watchdog, or radio initialization. Enabled-console tests run the actual loop,
feed modeled byte input, and terminate it by a test-only exception from the
modeled sleep after three iterations. Each scenario is a fresh process.

All hardware and SDK dependencies are doubles from stubs/. They model only
public facade calls, live bindings, controller phase, callback delivery,
runtime ownership, HCI event fields, clock, stdio, and watchdog. The modeled
sink asserts Settings::allow_pairing_confirmation=false at construction.
This setting is pairing policy, not autoplay configuration. Mutation and output wrappers assert that neither
occurs inside controller or HCI callbacks. Fortification is disabled in the
host compile so that libc's fortified printf alternative cannot bypass the
linker printf wrapper. This is a test-observation setting, not a firmware
setting. SDK and HCI constants/layouts in the doubles are illustrative model
inputs, not an independent ABI check.

## Coverage (44 scenario executions)

- Inert entry, pre-init STATUS without hci_get_state, START once, failed begin,
  terminal shutdown and restart refusal.
- All exact uppercase commands, CRLF, malformed controls/bytes, repeated CR,
  bounded overflow across 10,000 characters, parser recovery.
- Actual console byte-loop limit, initial STATUS, explicit START/PAUSE,
  a partially entered command becoming stale, and PAIR with its compile flag
  enabled or disabled.
- Explicit PAIR and LISTEN, pairing expiry, configured peer only, foreign HCI
  and unapproved confirmation rejection. No implicit pairing, query, play, or
  connect on start; no outgoing reconnection attempt after disconnect.
  This does not assert that configured-peer inbound connections are disabled.
- Controls denied without a binding; CONNECT admission is not a binding;
  input peer/epoch/generation/CID mismatch and pending-command denial.
  A2DP-only DISCONNECT works with no CT CID; stale or absent input binding
  cannot disconnect a live CT binding. Valid CT disconnect and local rejection
  are tested separately with explicit controller/sink call counts.
- Press callback queues without release; main-loop release after callback;
  a rejected press already in facade ReleasePending does not release twice.
  Completion is accepted only when both press and release responses accept;
  rejected press, rejected release, release without press, mismatched operation,
  local press/query rejection, and release local failure are tested.
- Borrowed PlayStatus bytes are poisoned after callback, proving the log uses
  a copy. A mismatched binding is not current; only matching stable response
  ctype is interpreted. Wrong-sized status fails closed.
- Command deadline at 1500 ms, startup deadline at 15 seconds, and lifetime at
  900 seconds stop before the next controller poll. A modeled facade Timeout
  at 700 ms stops in that iteration; this is **not** verification of the SDK's
  actual timer implementation. Pairing closes at 300 seconds.
- Queue overflow before a command and inside poll fails closed. Overflow behind
  a queued matching PressResponse must not trigger release; overflow behind
  copied query status must not produce interpreted success. Unexpected HCI
  OFF stops, and no control resumes after terminal stop.

## Limits

These are application-policy/model tests, not real SDK linkage, wire parsing,
BTstack timer tests, pairing/security interoperability, radio-off confirmation,
real media-player acceptance, audio decoding/output, or hardware watchdog
validation. No device, Bluetooth adapter, radio, flashing, recording, or SSH is
used. They do not replace ARM builds, the existing library/overlay tests, or
hardware validation of the intended configuration. The enabled main loop intentionally
stays alive after shutdown; modeled powerOff is only a requested local action.
