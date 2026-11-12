# Sink status-report framing test

`test_report_framing.cpp` uses synthetic data and `ReportFraming.h`. It does not open a serial port or a Bluetooth device.

From the repository root, use a host C++17 compiler:

```sh
mkdir -p .work/tests
c++ -std=c++17 -Iexamples/A2dpSinkPcmConsumer tests/host/sink_report_framing/test_report_framing.cpp -o .work/tests/sink-report-framing
.work/tests/sink-report-framing
```

The test covers the header's framing boundaries, not USB delivery, session freshness or hardware behavior.
