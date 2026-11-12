# Integrating A2DP Source + Sink

[日本語](a2dp_dual_role_jp.md) · [SDK acceptance backlog](a2dp_sdk_coverage.md)

The current implementation permits **different-peer** Source and Sink owners; normal same-peer registration remains rejected. An explicit opt-in [SDK experiment](a2dp_same_peer_experiment.md) permits it for host/ARM development only; its remaining cancellation and hardware requirements are not accepted.

## Ownership and dispatch

The same-peer experimental helper cannot share a CMake configuration with the public Sink helper. See [experimental limits](a2dp_same_peer_experiment.md).

- Construct one `A2dpSourcePlayer` and one `A2dpSinkReceiver` for program lifetime, with different configured peer addresses. Keep both PCM objects alive and call both `poll()` methods frequently on the same main loop.
- `A2dpShared.h` records each role's fixed peer after successful runtime claim. Either registration order is supported. A duplicate role or the same nonzero peer in both roles is rejected before SDK initialization. The Source API may have an empty target for a disabled example, but that is not a wildcard or routing policy.
- The SDK sends signaling notifications to both roles and then starts automatic SEP discovery. Filtering only the application callback is too late. `A2dpSharedSdk.h` replaces the SDK's thin raw AVDTP adapters with equivalent forwarding plus a fixed-peer allowlist and explicit initial-admission check **before** the SDK configuration process. Unknown or unadmitted successful signaling is closed without entering either SDK configuration process; another role's owned peer is not closed by the wrong role.
- Source delay-report and send-credit events retain their original SDK forwarding path. The shim handles SDK-generated meta events, not arbitrary Bluetooth packets; the SDK remains responsible for wire parsing. Framing, selected fixed-event lengths and connection existence are checked defensively.
- Disconnecting a role's distinct-peer signaling session does not power off the radio or disconnect another peer's A2DP session. SPP and AVRCP remain separate runtime owners. Actual simultaneous radio load and cross-profile behavior still need hardware acceptance.
- Do not mix these owners with direct SDK callback registration or legacy profiles. A later raw AVDTP registration would replace the role gate.

## Build resources

Include both helpers after `pico_sdk_init()` and create one target:

```cmake
include(${PICOW_BT_ROOT}/cmake/picow_a2dp_source.cmake)
include(${PICOW_BT_ROOT}/cmake/picow_a2dp_sink.cmake)
add_executable(app main.cpp)
picow_target_a2dp_source(app ${CMAKE_CURRENT_LIST_DIR})
picow_target_a2dp_sink(app ${CMAKE_CURRENT_LIST_DIR})
```

Set `PICO_CXX_ENABLE_EXCEPTIONS=1` before SDK initialization, as required by the existing Source pipeline. Supply an application `btstack_config.h`. For two distinct peers and one endpoint per role, it must provide at least:

```c
#define MAX_NR_AVDTP_CONNECTIONS 2
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 2
#define MAX_NR_HCI_CONNECTIONS 2
```

These three minima are not a complete configuration. Budget L2CAP channels/services, SDP records, ACL buffers and any SPP/AVRCP peers as well. The existing standalone Source and Sink examples each reserve only one AVDTP connection and endpoint; do not reuse that configuration unchanged for dual-role use. The Sink helper reserves a 32 KiB stack. A successful link does not measure peak heap/stack usage or guarantee real-time PCM throughput.

## Discovery queue progress

The installed SDK has a global SEP-discovery owner. Some configuration-completion and signaling-release paths clear that owner without advancing waiters. The shared shim now resubmits only SDK-marked queued work for registered peers through the SDK's exported scheduling function, without changing its private lock. Initial signaling admission and continued-discovery admission use separate callbacks; cancelled, failed or no-longer-live owners cannot restart queued work. The releasing CID is excluded and reentry is bounded.

The real-SDK fixture verifies queued Sink progress after Source release, queued Source progress after Sink configuration, and cancellation suppression. This is not full concurrent codec/media negotiation or hardware evidence. Registration, dispatch isolation, simultaneous media and parallel negotiation remain separate acceptance items.

## Same-peer SDK work still required

BTstack 1.6.2 `avdtp_connection_t` has one `configuration_state` and one signaling `local_seid` per peer connection. Initiator and acceptor configuration paths reject a second stream configuration. Both roles also share signaling disconnect and deinitialization state. Therefore, lifting the same-peer guard is not a valid implementation.

Required work includes per-stream configuration/signaling ownership, opening and routing both media channels, role-local close/abort, safe signaling teardown, reconnect/role switching, and tests against the actual SDK and hardware. Broader codec, endpoint and encoded-media APIs remain in the [coverage backlog](a2dp_sdk_coverage.md).

## Validation scope

Distinct-peer dual-stream negotiation, audio timing and radio coexistence remain unverified. Ownership, dispatch and discovery host tests plus ARM linkage do not establish simultaneous radio operation.

[Validation scope](validation.md)
