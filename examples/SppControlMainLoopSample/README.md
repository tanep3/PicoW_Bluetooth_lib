# SPP control main-loop sample

[日本語](README_jp.md)

An application-shaped example of the current `spp::Client` public API. It covers serial-port controls, incoming status observations, service withdrawal, sequential peer selection and optional RFCOMM ERTM. It is not a hardware probe or a reliable-command protocol.

## Safe default

Both `SPP_SAMPLE_ENABLE_RADIO` and `SPP_SAMPLE_ENABLE_ERTM` default to `OFF`. The default `main()` returns before stdio initialization, client construction, `configureErtm`, `begin`, `poll` or any Bluetooth operation. Building creates files only. Set the actual peer addresses and review the command sequence before flashing a radio-enabled build.

`main.cpp` uses an ordinary `if`, so the live path remains C++ type-checked in default builds. A separate enabled-path build is also needed to check reachable linking rather than relying on an optimizer-discarded path.

## Files and ordering

- `SppControlWorkflow.h`: the complete application sequencing, templated over a Client-like facade. It uses production SPP state/types, but includes no Pico/BTstack headers and performs no effects except through the injected facade.
- `main.cpp`: process-lifetime client and ERTM buffers, one polling loop, and copied diagnostic output.
- `btstack_config.h`: bounded standalone resources, with both ERTM defines enabled together when requested.
- `CMakeLists.txt`: uses `picow_target_spp`, including its validated transport overlay. SDK source originals are not modified.

The enabled path does the following:

1. Optionally configure caller-owned ERTM storage **before** `begin`, once only.
2. Queue `begin`, poll until `Runtime::Ready`, then queue a listener and wait for `Listening`.
3. Connect route 0 to the first peer's explicitly known RFCOMM channel. No discovery or candidate-selection policy is implied. Wait for a new route generation and `Open`, then obtain its current handle.
4. Submit `queryPort`, `configurePort` (115200/8 data bits/1 stop bit/no parity), `sendLineStatus(0)` and `sendModemStatus(0x0c)` in order. The latter flags are RTC and RTR; the SDK adds the wire EA bit. Submit only one control at a time.
5. Withdraw listeners and observe `Disabled`. `unlisten` does not disconnect an open route. Then close every SPP route, including accepted incoming routes.
6. If an extended application has an outstanding SDP query, cancel it and wait for `query.busy == false`. `Cancelled` alone is insufficient. Wait for all routes to be `Closed` and `ertmStatus().active == 0`.
7. Call `selectPeer(next_peer)`. The facade additionally checks its private pending events/SDK bindings. A false return means not queued; the example permits bounded retry after another poll. A true return still requires polling and observing `snapshot().config.peer == next_peer`. It does not connect to that peer automatically.

The workflow exclusively owns the facade. Do not issue competing mutators from callbacks, another thread/core or another loop. An incoming connection that takes route 0 before the outgoing request is processed causes conservative cleanup rather than controlling the wrong route. This example does not exchange application payloads; byte-stream flow control remains the facade's independent responsibility.

## Queue admission, local result and remote observation

The workflow's lifecycle/control methods return `bool` for **queue admission**, not successful completion. `configureErtm` is different: its return reports synchronous local validation/setup, not a queued operation or a negotiated mode. The workflow waits for state transitions and consumes bounded history by monotonically increasing sequence number. A gap is a diagnostic-loss failure, not evidence of success. Queue rejection, lifecycle rejection, runtime failure, unexpected closure and stage timeouts initiate bounded cleanup where possible. `failure()` identifies the problem; completion and failure are distinct terminal stages.

For each control, completion requires the current handle/generation, a new `control_token`, `control_pending == false` and a matching retained `ControlResult`. Its status is the local SDK return value, **not a peer acknowledgement**. Nonzero control statuses are reported and counted, but do not cause the otherwise-open byte stream to be treated as broken. The next control can still be submitted. The SDK may coalesce updates; these controls cannot guarantee delivery of every intermediate value.

`report().routes` copies the latest remote line/modem status, local/remote port configuration and their per-generation counters from snapshots. Initial notifications during connection setup can be observed too. Received modem values retain the wire EA bit. Port values include received XON/XOFF; this sample leaves them zero when configuring because the pinned SDK cannot set them. Snapshot values are the latest observations, not one event per print or transaction-correlated replies. A generation change invalidates the old counters/values. These are virtual serial settings, not UART hardware configuration or Bluetooth throughput controls.

Each route's `transport` field comes from `Client::transport(current_handle)`: `Basic`, `Ertm`, or `Unknown`. It is never inferred from a compile flag, requested configuration or allocation counter. Closed/stale/unobservable bindings report `Unknown`.

## Optional ERTM storage

The ERTM build has two separate `alignas(16)` static 12288-byte buffers. They outlive the static client and all asynchronous callbacks. `configureErtm` validates and copies the configuration, not the backing allocation. One lease is for an RFCOMM multiplexer, not each route; two routes may share it.

This example fixes `ertm_mandatory = 0`: Basic fallback is allowed. Both compile flags plus a configured pool still do not prove ERTM negotiation. Mandatory ERTM is deliberately not exposed as a sample option: pool exhaustion or an incompatible reused Basic multiplexer can cause a terminal fault that powers off the shared radio. See [peer/ERTM lifecycle](../../docs/spp_lifecycle.md).

Never free/reuse backing storage on a disconnect request, timeout or fault. Wait for actual matching SDK release callbacks; the sample never fabricates a release. It also does not delete bonds, alter another profile's peer selection or disconnect another profile's ACL. Resources here are for a standalone SPP build; shared A2DP/AVRCP applications need a combined resource configuration and separate validation.

After completion or a bounded cleanup failure, `main` retains all owners/storage and continues polling. It deliberately does not recreate the one-shot client or power off a shared radio. A failed cleanup is not reported as a clean disconnect. There is no public `Client::end`/restart contract. Products must choose their own terminal-failure policy.

## Build only

Use the pinned Pico SDK/toolchain described in the repository's [build guide](../../docs/build_guide.md). No command below runs firmware or accesses a device.

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S examples/SppControlMainLoopSample -B /tmp/spp-sample-basic
cmake --build /tmp/spp-sample-basic --parallel 2
cmake -S examples/SppControlMainLoopSample -B /tmp/spp-sample-ertm \
  -DSPP_SAMPLE_ENABLE_ERTM=ON
cmake --build /tmp/spp-sample-ertm --parallel 2
```

For compile/link coverage of the reachable enabled path, use a separate directory. The addresses below are **synthetic compile-only values**, not authorized real peers. Do not flash/run this output.

```sh
cmake -S examples/SppControlMainLoopSample -B /tmp/spp-sample-reachable \
  -DSPP_SAMPLE_ENABLE_RADIO=ON -DSPP_SAMPLE_ENABLE_ERTM=ON \
  -DSPP_SAMPLE_FIRST_PEER=02:00:00:00:00:01 \
  -DSPP_SAMPLE_NEXT_PEER=02:00:00:00:00:02
cmake --build /tmp/spp-sample-reachable --parallel 2
```

Repeat in another build directory with `SPP_SAMPLE_ENABLE_ERTM=OFF` for reachable Basic coverage. Radio-enabled configuration rejects zero/equal peers; addresses must have six hex octets. `SPP_SAMPLE_REMOTE_CHANNEL` and `SPP_SAMPLE_LOCAL_CHANNEL` default to 1 and 2 and must be 1–30. Real use requires explicit correct peer addresses/channel and permission to expose the service and change the peer's virtual port settings. Authentication stays enabled and automatic pairing confirmation is disabled. No persistent bond backend or credential import is configured here.

## Validation boundary

```sh
bash tests/host/run_spp_usage_sample_tests.sh
```

Host acceptance can inject a deterministic facade into the **same** workflow header, including delayed readiness/results, rejected controls, incoming observations, busy SDP, held leases, queued selection and timeout paths. Host ordering tests and Basic/ERTM ARM builds do not establish over-the-air behavior. This sample does not claim hardware validation of status delivery, peer switching, ERTM negotiation, retransmission, interoperability, persistent bonds or shared-profile coexistence.

See [serial controls](../../docs/spp_control.md), [peer/ERTM lifecycle](../../docs/spp_lifecycle.md) and [SDK integration](../../docs/spp_sdk_integration.md) for the underlying contracts.
