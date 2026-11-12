# Standalone discoverable-listen sample tests

Run `bash tests/host/run_sink_discoverable_listen_tests.sh` from the repository root.
The runner compiles the **actual sample `main.cpp`**, actual FIFO/report/window headers
and public receiver declarations with ASan+UBSan. It supplies a modeled receiver,
clock, character input and SDK discovery/identity functions. No device node,
socket, Bluetooth controller, serial port, microphone, audio output or service is
accessed. Each fixed case runs in a separate process; no exhaustive sweep or fuzzing
is involved. Retained build paths and input hashes are printed/saved.

18 fixed scenarios cover inert radio-OFF even with the discovery option ON,
ordinary radio-ON/discovery-OFF, idle startup, explicit identity retrieval, rejected
listen, repeated listen without extending the original deadline, exact timeout,
manual cancel, outgoing connect, explicit reopen after timeout, uint32 clock wrap,
and visibility removal for signaling, failure, nonworking, closing, admission-off,
streaming, connecting and a changed generation with final idle flags. The latter
models a short session whose establishment/release were drained in one poll; it
does **not** run that SDK event path. The lifecycle cases check removal before the
timeout. Effects are asserted in order and discovery show/hide calls have exact modeled timestamps, including no redundant automatic reopen.

The source address passed to the receiver is synthetic and checked unchanged.
This does not retest real peer authentication/admission, actual inquiry visibility,
controller command latency, real timekeeping, firmware restoration or audio. A
polled timeout is not an independent watchdog. USB log delivery remains untested.
