# Offline USB capture checks

Run `bash tests/host/run_sink_usb_capture_tests.sh` from the repository root.
No serial/USB/HCI/SSH/flash operation occurs. All PCM bytes are synthetic.
Temporary build/WAV fixtures are created beneath `.work/public-sink-usb` and
removed after the test. Python parser tests need only the standard library;
`pyserial` is imported only by the live collector's `main()`.

- Actual `UsbPcmCapture.h` compiled with strict warnings, ASan and UBSan, against
  small TinyUSB/stdio mocks: full 220,500-frame capture with seven-byte USB writes,
  packet CRCs, invalid starts, busy rejection, queue overflow, receiver/decoder
  loss deltas, epoch changes, stream stop, recording/transport deadlines and
  unsigned clock wrap.
- Ten Python parser/storage tests: fragmented packets and text prefix, complete
  and incomplete results, CRC failures, missing END, malformed headers/order,
  duration/frame accounting, five-second bounds, bounded prefix buffering,
  complete/incomplete WAV headers, exclusive output and the exact `r` command.
- Actual example `main.cpp` with USB capture ON, strict warnings and sanitizers:
  reject recording before stream, explicitly start once, fully drain an
  8192-frame burst before another receiver poll, clear CDC RX directly while
  stdio is disabled, clear input at completion and never replay a queued `r`.

These mocks do not establish actual USB throughput, Bluetooth interoperability,
source waveform equality or auditory quality. See the example README for the
hardware results and the public example's limits.
