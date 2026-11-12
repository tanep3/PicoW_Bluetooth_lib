# SPP service lifecycle and serial-port controls

[日本語](spp_control_jp.md)

These APIs extend the same `spp::Client` used for locally initiated and accepted RFCOMM connections. The byte stream stays full duplex in both cases. The class name does not limit it to a client role. See [SDK integration](spp_sdk_integration.md) for the existing ownership, pairing, SDP, FIFO and credit contracts.

## Withdrawing a service

`unlisten(endpoint_slot)` queues withdrawal of that endpoint's SDP record and RFCOMM registration. `true` means queued, not finished. Observe `endpoints[slot].phase`: Listening → Unregistering → Disabled. The endpoint token protects a later registration from stale removal/completion. Invalid/non-listening slots return false; queued races and errors appear in history.

Existing routes remain connected and retain their generation and FIFOs. Incoming requests processed after withdrawal begins are rejected. Register the same endpoint again with `listen` after it becomes Disabled. One service's removal does not remove another service's SDP record. No shared GAP connectability or power setting is disabled by this operation.

## Sending serial-port controls

Every operation uses a current, open `Handle`, irrespective of which peer initiated the connection:

- `sendLineStatus(handle, value)`: no error `0`, or error-present bit `1` combined with overrun `2`, parity `4`, framing `8`. Combinations of error bits are accepted.
- `sendModemStatus(handle, flags)`: V.24 FC `0x02`, RTC `0x04`, RTR `0x08`, IC `0x40`, DV `0x80`. Pass only these flags. The adapter adds EA `0x01` to the SDK wire byte.
- `configurePort(handle, PortSettings)`: configure the peer's port. The typed fields cover baud 2400–230400, 5/6/7/8 data bits, 1/1.5 stop bits, none/odd/even/mark/space parity and six RFCOMM flow-control flags.
- `queryPort(handle)`: request the peer's current port configuration.
- `control(handle, ControlRequest)`: the common typed form of these operations.

`PortSettings` defaults to 9600 baud, eight data bits, one stop bit, no parity and no flow control. Its `xon`/`xoff` fields expose received values. The pinned BTstack configuration API cannot set these characters, so nonzero `xon`/`xoff` in `configurePort` are rejected rather than silently discarded. Port settings describe a virtual serial port; they do not configure an attached UART or impose a physical UART baud rate on Bluetooth throughput.

## Acceptance is not remote completion

A true return means the request entered the bounded event queue. After `poll`, inspect `routes[slot].control`, `control_token`, `control_pending`, `control_status` and the corresponding history entries. Only one local SDK control submission per route may be pending; another queued request can be rejected with Busy. Tokens reject duplicate/late local results. Invalid values, stale handles and non-open routes are rejected without touching the peer or another route.

`ControlResult` records the SDK return status, not a peer acknowledgement. In particular, the pinned SDK may coalesce repeated modem/configuration updates internally. A zero status does not prove that every intermediate update reached the peer. Do not build reliable application commands on these status operations. A control failure leaves the independent byte stream open. After close/generation change, old control observations do not describe the new connection.

## Receiving status and configuration

Route snapshots retain the latest copied values plus per-generation counters:

- `remote_line_status`, `line_status_events`.
- `remote_modem_status`, `modem_status_events`. Received modem bytes retain their wire EA bit.
- `local_port`, `local_port_events`, for a local-port update from the peer.
- `remote_port`, `remote_port_events`, for a remote-port response/update.

The bounded history contains the individual normalized events while retained. These are observations, not transaction-correlated acknowledgements. Notifications carry the route generation and current ACL epoch. Initial MSC/RPN notifications before CHANNEL_OPENED are retained without pretending the connection is already open. A retired ACL, wrong CID, short packet or invalid port encoding cannot update the current connection. Queue overflow is an explicit runtime fault, not an unlimited notification buffer.

## Pinned SDK compatibility

BTstack `501e6d2b86e6c92bfb9c390bcf55709938e25ac1` defines `RPN_DATA_BITS_5`, `_6`, `_7`, `_8` all as zero. Its actual serializer expects wire encodings 0/1/2/3. `spp::DataBits` has explicit correct values. `SdkPortConfiguration.c` performs the conversion in C, where these values are valid integer-compatible enum values, avoiding an out-of-range C++ conversion to the defective SDK enum. The receiving adapter reads the verified two-bit field directly for the same reason. The SDK checkout is not edited. The [CMake helper](../cmake/picow_spp.cmake) includes this C bridge automatically.

## Validation and remaining work

Host tests exercise both connection directions, service removal/re-registration without disconnecting data routes, explicit status forwarding, all four data-bit wire values, SDK rejection isolation, malformed packets, setup-time status and stale ACL/route/token handling. Tests use the real pinned SDK headers with mocked transport calls, ASan and UBSan. The existing deterministic FIFO/credit/SDP/reconnect regression suite remains required. A real Pico SDK 2.1.1 ARM build/link of `PicoSppSdkProbe` checks adapter integration.

Run:

For included checks, see the [build guide](build_guide.md#compile-only).

Limited Pico/Linux tests cover local control submission and intact byte streams. Applying 5/6/7 data bits at the peer, incoming line status, arbitrary peers and endurance remain unverified. Status zero is local acceptance. See [Validation scope](validation.md) for details.

Evidence: test results and hashes.

The control test also extracts and compiles the pinned SDK RPN serializer unchanged in C, asserting its wire flags and parameter masks for all four data-bit values. Only channel lookup and scheduling are mocked; the extracted function hash is printed in the log.

[Offline-by-default control and peer-switching example](spp_examples.md)
