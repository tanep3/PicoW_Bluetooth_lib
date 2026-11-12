# SPP control and peer-switching examples

[日本語](spp_examples_jp.md) | [Serial controls](spp_control.md) | [Peer selection and ERTM](spp_lifecycle.md)

[SppControlMainLoopSample](../examples/SppControlMainLoopSample/README.md) demonstrates explicit application ordering using the public `spp::Client` API. The default executable does not begin Bluetooth. Its optional enabled path is for an application you deliberately configure; compiling it is not permission to flash or operate hardware.

The workflow distinguishes queued requests from completed local state changes. It registers a service, connects an explicit peer/channel, submits serial controls one at a time, observes copied status counters and actual transport, withdraws the service, drains routes and transport resources, then selects the next peer. The sample does not implement a remote media/player application or interpret control status as a peer acknowledgement.

- One selected peer and at most two routes are the current resource contract. Selecting a peer is sequential reconfiguration, not simultaneous multi-peer communication.
- `control_status == 0` records local SDK acceptance. Received RPN/MSC/line observations are not transaction-correlated acknowledgements.
- The optional ERTM proposal may fall back to Basic. Read `transport(handle)` on an open current handle; an allocated buffer does not prove ERTM negotiation.
- Backing ERTM buffers and the client have process lifetime. A disconnect request, route closure or application timeout is not permission to recycle an outstanding multiplexer lease.
- The example uses optional ERTM. Required-mode exhaustion or an unacceptable reused transport can fail the shared radio in the current implementation, so mandatory ERTM requires a deliberate application-wide policy.
- Bounded history may overwrite old entries. An application must detect lost diagnostic events rather than manufacture completion from a missing entry.

Host tests of the sample sequence and existing core/adapter/pool regressions are separate from the Pico SDK compile/link check. Neither proves radio peer switching, ERTM retransmission, remote control handling or A2DP/AVRCP coexistence. Follow the hardware checklist in the lifecycle guide before claiming those capabilities validated.

## Repeatable local checks

For included checks, see the [build guide](build_guide.md#compile-only).

The sample host test uses the same workflow header and actual core, with modeled transport outcomes. The workflow selects the second peer and confirms the result; it does not connect to that second peer.

## Application-framed two-route duplex traffic

[SppFramedDuplexSample](../examples/SppFramedDuplexSample/README.md) demonstrates one control stream and a second stream carrying two synthetic data types, temperature and vector. Its version/type/length format belongs to the application; the library remains raw byte streams. A process selects outgoing or incoming mode, and maps roles by observed channel rather than arrival-allocated slot.

It includes bounded partial writes, fragmented/coalesced reads, typed values and generation-bound discard. Refused/delayed disconnect cannot resume the aborted generation. Four-poll rotation gives each route/direction a first-operation opportunity; it is not wireless fairness or priority. There is no ACK, auto-echo, automatic reconnect or replay protocol. Radio is OFF by default.

Host tests and successful builds do not qualify the public sample on hardware. Do not transfer results from a different test application; consult [Validation scope](validation.md) for the recorded scope.
