# SPP hardware-validation scope

[日本語](spp_hardware_validation_jp.md) · [SPP integration](spp_sdk_integration.md) · [Validation overview](validation.md)

Bounded tests exercised the public SPP API with Pico SDK 2.1.1 / BTstack 1.6.2, CYW43 polling and one peer. Basic-RFCOMM checks included two incoming endpoints, per-route reconnect, paused application reads, synthetic load and ACL reconnection. Later bounded conditions are described by the [lifecycle guide](spp_lifecycle.md).

These observations are not a guarantee for other peers, transports, traffic patterns or long-term use. Device-specific runners, raw logs and restoration firmware are not distributed.

## What to observe in an application

- Endpoint slots and route slots are separate. Incoming connections use a free route; identify them by accepted channel and current binding, not an assumed slot.
- Respect the negotiated MTU and current writable capacity. Two routes share the ACL and radio; alternating application service order is not a library fairness guarantee.
- Keep reads and sends nonblocking. Handle partial acceptance and bind unaccepted bytes to the route generation. Never silently carry them into a reconnected route.
- Pausing reads consumes RX capacity and eventually withholds credits. A host socket accepting bytes is not proof that Pico received or consumed them.
- API success admits a request; state/events determine completion. Check current ACL tokens and route generations rather than old socket handles.

## Byte accounting and send deferral

Per-generation counters should satisfy:

- `tx_accepted = tx_sent + tx_size + tx_discarded`
- `rx_received = rx_read + rx_size + rx_discarded`

CAN_SEND_NOW is readiness, not a reservation of shared HCI capacity until a later call. The adapter preserves pending bytes if another profile consumes the resource. Deferral is distinct from permanent transport failure. See [state transitions](spp_state_transitions.md) and [port controls](spp_control.md).

Unread bytes plus already granted maximum-size frames must fit the receive capacity. Credit may stop before the queue is full when the remaining space is smaller than one MTU-sized frame.

## Disconnect and reconnect limits

A route can close normally before an ACL-close event arrives. A zero route error alone therefore does not prove the old connection is live. Check closed routes, inactive ACL, terminal old handles and new tokens/generations after reconnection.

Limited route/ACL reconnection results do not establish arbitrary recovery under three-profile traffic, continuous audio quality, every SDK error path or every host's authentication behavior. ERTM has separate opt-in source and ownership requirements; basic-RFCOMM results are not ERTM acceptance.

For a runnable entry point, use [DeveloperSamples](../examples/DeveloperSamples/README.md) or the [framed duplex sample](../examples/SppFramedDuplexSample/README.md).
