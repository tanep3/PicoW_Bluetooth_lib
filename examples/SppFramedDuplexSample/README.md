# SPP application-framed duplex example

[日本語](README_jp.md)

The library remains **RAW byte streams**. This example owns a tiny application
format on top: a control stream and a separate data stream carrying two typed
messages. Both peers can send either role independently. Nothing auto-echoes or
acknowledges data. It is not RFCOMM port-control signaling or a generic protocol
added to the library. No microphone, audio, heap-growing application queues,
automatic reconnect, automatic pairing approval, or implicit replay.

## Build only

From the repository root, with installed SDK 2.1.1 and installed picotool:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S examples/SppFramedDuplexSample -B build/spp-duplex-off \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_UPDATES_DISCONNECTED=ON -DPICOTOOL_FORCE_FETCH_FROM_GIT=OFF
cmake --build build/spp-duplex-off -j4
```

Default OFF returns before stdio, Client construction, poll or radio use. Default
peer is zero. To **compile/link only** the enabled path, use another build directory
and add `-DSPP_SAMPLE_ENABLE_RADIO=ON -DSPP_SAMPLE_PEER=02:00:00:00:00:01`.
That address is synthetic and is not a usable/authorized device identity. For use on hardware, configure the actual peer first. Enabled startup
initializes USB stdio but does not call begin until command `b`. The build commands produce firmware files without flashing or running them.

Basic RFCOMM only. `SPP_SAMPLE_CONTROL_CHANNEL` defaults to 1 and
`SPP_SAMPLE_DATA_CHANNEL` to 2; both must be distinct integers 1..30. The remote
application must implement this format on both matching channels. Authentication
is required and automatic confirmation is disabled. No pairing or credential
management is implemented by this example.

## Manual interaction (after flashing your Pico W)

All facade calls and application work belong to one main loop. Each single ASCII
character is a command; newlines are ignored. Status output occurs on command only. USB stdout timeout, stdio lock wait and connect-wait timeout
are explicitly zero: disconnected/full console output may be dropped. Formatting
and available USB writes still consume bounded work and can delay polling; this
is not a real-time scheduling or lossless logging example.

- `b`: queue begin once. `s` must show Ready (runtime 2) before connecting/listening.
- **Outgoing mode:** `c` connects control (slot 0); `d` connects data (slot 1).
  These are explicit operations, not auto-retry. Wait for Open (phase 4) using `s`.
- **Incoming mode instead:** `l` registers control endpoint 0; `k` registers data
  endpoint 1. `s` must show Listening (endpoint phase 2) before peer connection.
- `t`: enqueue one synthetic control frame on the currently open control route.
- `v`: enqueue temperature then vector in one data-route buffer.
- `s`: print runtime, route/channel/generation, errors, parser state, pending
  application bytes, received type counts/last values, and endpoint states.
- `x`: discard application pending bytes and request disconnect on each active
  route. It does **not** withdraw listeners. A connecting request is also closed.
- `u`: withdraw currently Listening endpoints, preserving existing routes.
  If registration is still pending, wait and issue `u` again. To stop acceptance
  and close routes, observe endpoints Disabled with `s`, then issue `x`.

A true queued result is not completion. Use snapshots via `s`; false requires a
later deliberate command. There is no end/reset/recreate contract for Client;
static owners survive and the loop continues polling even on failure.

Mode locks on the first connect/listen command and cannot switch within this
process, including after disconnect. This deliberate restriction disambiguates
channel numbers: outgoing uses remote channels, incoming uses local channels.
Incoming route slots are assigned in arrival order, **not endpoint order**. The
workflow maps the observed channel and current generation to its role, not slot 0
as control. No mixed-origin mode, SDP selection, third route, or second peer.

The connected peer can send the fixed frames below in either direction without
waiting for a local send. There is no application ACK, request/reply correlation,
or delivery guarantee: enqueue means owned application bytes; tryWrite acceptance
means a copy into the library FIFO, not peer receipt. Counters show local reception.

## Application wire format v1

Four-byte envelope: version (1 byte, value 1), type (1 byte), payload length
(unsigned little-endian 16-bit). Payload integers are unsigned little-endian 16-bit.
No native struct serialization, CRC, magic resynchronization, or compression.

- Control route, type 1, length 2: marker command ID, synthetic value 7.
  Exact bytes: `01 01 02 00 07 00`. Receipt records the command, no external action.
- Data route, type 2, length 2: temperature in centi-degrees C, synthetic 2500
  (25.00 C). Exact bytes: `01 02 02 00 c4 09`.
- Data route, type 3, length 6: three unsigned synthetic vector components
  (10,20,30), no physical units. Exact bytes: `01 03 06 00 0a 00 14 00 1e 00`.

The receiver validates version, role/type and exact length before decoding. It
accepts the full unsigned value range for those types; it does not require the
remote endpoint to repeat our synthetic constants. One fragmented frame or
multiple coalesced frames work across arbitrary read boundaries. Invalid headers
stop application I/O on that route until manual close/reconnect; no guessed
resynchronization or error-response loop. `s` exposes the failure.

## Boundedness and lifecycle

Each of two routes owns fixed 64-byte TX and RX arrays; current frames use at most
16 TX and 10 RX bytes. Enqueue refuses a busy/unconnected/invalid route rather than
growing a queue. Every loop polls first, observes generations, then performs at
most one read (32 bytes) and one write per valid route. First route and first direction rotate across four polls (R0, R1, W0, W1).
This avoids a permanent first-reader advantage when a read queues a credit callback
and makes later calls Busy. Control is a separate stream, not a guaranteed latency/QoS priority. Both routes receive bounded service.
Busy/WouldBlock waits for the next outer poll. Partial acceptance advances exactly
the accepted prefix; the original suffix stays owned until accepted or discarded.

Manual abort latches the current generation stopped even if disconnect is refused,
delayed or fails; only a new generation can resume application I/O.

Any non-Open observation or generation change clears application TX, parser
prefix, decoded values and counts. A close/reopen between observations is caught
by generation change. Application discard counters cover only retained bytes;
library FIFO discard counters are separate. No automatic replay occurs. Explicit
manual sends after reconnect generate new samples. Protocol errors and hard I/O
errors require operator attention; there is no watchdog or automatic recovery.

## Focused verification

```sh
bash tests/host/run_spp_framed_duplex_tests.sh
```

Tests compile this actual header and production SppCore with ASan/UBSan. Mocked
transport callbacks replace SDK/radio; host setup reuses FakeClient from the
control example. Fixed cases cover outgoing and reversed incoming allocation,
exact bytes on both roles/directions, fragmented/coalesced RX, typed values,
partial acceptance, Busy/WouldBlock, close/generation truncation, no replay, and
a fixed MTU1019 one-credit workload with both routes/directions progressing.
The ARM enabled link checks the real Client API separately; it is not a radio test.
All broader acceptance, radio/interoperability, fairness under pressure and
A2DP/AVRCP coexistence claims remain outside this sample.
