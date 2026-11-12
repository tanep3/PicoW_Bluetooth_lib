# Application API reference

Companion references: [Source lifecycle controls](a2dp_source_lifecycle.md), [AVRCP Controller](avrcp_controller.md), [SPP control/port APIs](spp_control.md), [SPP lifecycle/ERTM](spp_lifecycle.md), and [optional bond storage](bluetooth_bond_storage.md). These companion references describe the API contracts and their validation limits.

[Optional Sink START decisions](sink_start_decisions.md): `startDecisionsAvailable()`, `pendingStart()` and `decideStart(request, accept)` require the optional build helper plus `Settings::require_start_decision`. Preserve the complete request token, decide outside callbacks, and distinguish local admission from streaming.

Receiving API: [A2dpSinkReceiver and IAudioSink](../examples/A2DPSinkReceiver/README.md). [Complete SDK coverage and remaining acceptance work](a2dp_sdk_coverage.md) tracks the requirement for Source/Sink and combined roles; this reference is not a claim that all of it is complete.

[日本語](api_reference_jp.md) · [Project overview](../README.md) · [Runnable developer samples](../examples/DeveloperSamples/README.md)

This reference describes the current single-main-loop application APIs, checked against their headers and implementations. A successful request is not necessarily a completed Bluetooth connection. Hardware coverage and experimental limitations belong to the [project overview](../README.md); a declaration here is not a claim of interoperability with every device.

Practical examples: [SPP controls and sequential peer selection](spp_examples.md), [Sink PCM FIFO](../examples/A2dpSinkPcmConsumer/README.md), and [AVRCP application ordering versus link checks](avrcp_examples.md). Detailed AVRCP contracts: [Target](avrcp_target_features.md), [Controller](avrcp_controller.md), [Browsing](avrcp_browsing.md), and [Cover Art Client](avrcp_cover_art.md). API availability and hardware acceptance are separate.

## Choose an entry point

| Task | Include | API |
| --- | --- | --- |
| Receive PCM from a Bluetooth source | `A2dpSinkReceiver.h` | `A2dpSinkReceiver`, `IAudioSink` |
| Send your PCM audio to a Bluetooth speaker | `A2dpSourcePlayer.h` | `A2dpSourcePlayer`, `IAudioSource` |
| Exchange serial bytes over RFCOMM | `SppClient.h` | `spp::Client` |
| Receive media-button commands | `AvrcpTarget.h` | `picow::avrcp::AvrcpTarget` |
| Send media commands and queries | `AvrcpController.h` | `picow::avrcp::AvrcpController` |
| Handle Browsing requests and responses | `AvrcpBrowsingController.h` | `picow::avrcp::AvrcpBrowsingController` |
| Receive Cover Art image/property chunks | `AvrcpCoverArt.h` | `picow::avrcp::AvrcpCoverArt` |
| Inspect the shared ACL or write a profile adapter | `BluetoothRuntime.h` | `BluetoothRuntime` (advanced) |

These include names work with the CMake helpers below. The source headers are [A2DP](../audio-core/A2dpSourcePlayer.h), [PCM](../audio-core/IAudioSource.h), [SPP](../spp-sdk/SppClient.h), [AVRCP](../avrcp-sdk/AvrcpTarget.h) and [runtime](../bluetooth-runtime/BluetoothRuntime.h).

### Shared rules

- Use C++17 and the polling Pico SDK architecture. Run every method, callback and `poll()` on one main loop, never from an IRQ or a second core/thread.
- Construct at most one owner for each facade/role, subject to its shared-ownership contract, and keep owners, PCM sources/sinks and callback contexts alive for the program lifetime. There is no supported destroy/recreate/restart lifecycle. `begin()` is not a reconnect function.
- The profiles share one radio. Do not call `cyw43_arch_init()` yourself, mix legacy Bluetooth plugins with these owners, or initialize a second BTstack profile implementation.
- Call each enabled profile's `poll()` frequently. Avoid blocking console output, long sleeps and lengthy callbacks. Each facade services the shared runtime itself.
- Do not infer authenticated status from an ACL handle, accepted command, `begin()` result or SDP result. Profile establishment and security are asynchronous.
- Default shared-runtime link keys are held in memory, not persisted across reset.

### CMake integration

After `pico_sdk_init()` and `add_executable(app main.cpp)`:

```cmake
set(PICOW_BT_ROOT /path/to/PicoW_Bluetooth_lib)
include(${PICOW_BT_ROOT}/cmake/picow_a2dp_source.cmake)
include(${PICOW_BT_ROOT}/cmake/picow_spp.cmake)
include(${PICOW_BT_ROOT}/cmake/picow_avrcp_target.cmake)
picow_target_a2dp_source(app ${CMAKE_CURRENT_LIST_DIR})
picow_target_spp(app ${CMAKE_CURRENT_LIST_DIR})
picow_target_avrcp_target(app ${CMAKE_CURRENT_LIST_DIR})
```

Use only the helpers needed by your application. Each second argument is the directory containing your compatible `btstack_config.h`. The helpers add source files, includes, C++17, `CYW43_LWIP=0`, and polling Bluetooth libraries. Start with [DeveloperSamples/CMakeLists.txt](../examples/DeveloperSamples/CMakeLists.txt) and its [shared configuration](../examples/DeveloperSamples/btstack_config.h), not a mixture of unrelated example configurations. SPP requires `HCI_ACL_PAYLOAD_SIZE <= 1033`; optional RFCOMM ERTM is a separate resource/configuration contract, not enabled by these generic instructions. AVRCP without `HAVE_MALLOC` requires `MAX_NR_AVRCP_CONNECTIONS >= 4` for the SDK's two roles.

Additional helpers (select only those needed): Sink uses `picow_a2dp_sink.cmake` / `picow_target_a2dp_sink`; Controller uses `picow_avrcp_controller.cmake` / `picow_target_avrcp_controller`; Browsing uses `picow_avrcp_browsing.cmake` / `picow_target_avrcp_browsing`; Cover Art uses `picow_avrcp_cover_art.cmake` / `picow_target_avrcp_cover_art`. Each takes `(app config_directory)`. Do not reuse a Source-only configuration blindly for Sink/Browsing: follow each feature guide for buffers, stack and shared owners. Successful linking does not qualify same-peer Source/Sink or all-profile runtime coexistence.

## A2DP: PCM to a speaker

### `IAudioSource`

Implement these virtual methods; the player borrows your source, it does not own it.

| Signature | Contract |
| --- | --- |
| `bool hasSamples() const` | Whether PCM can currently be read. Return promptly. |
| `size_t read(int16_t* buffer, size_t samples)` | Copy at most `samples` signed 16-bit values and return the number copied, not bytes or stereo frames. Partial reads are allowed; zero means no samples now. |
| `virtual ~IAudioSource()` | Virtual destruction; source must outlive the player. |

PCM must be interleaved stereo (`L,R,L,R,...`) at the configured rate. There is no resampler or mono conversion. Preserve the channel ordering across partial reads. Never retain the supplied buffer or write beyond its capacity. The pipeline accumulates partial PCM and retains an unsent encoded batch under backpressure.

### Settings and results

`A2dpSourcePlayer(IAudioSource& source, Settings settings)` constructs the owner; copying and assignment are deleted.

| `Settings` field | Default | Meaning |
| --- | --- | --- |
| `const char* target_address` | `""` | Exact nonzero peer, `XX:XX:XX:XX:XX:XX` (17 characters); use a string with stable lifetime. The zero address is not a wildcard. |
| `uint32_t sample_rate` | `48000` | Only `48000` or `44100` Hz; source PCM must match. |
| `uint64_t start_delay_us` | `0` | Delay before local automatic START after stream setup, in microseconds; does not delay a remote START. |
| `uint64_t play_duration_us` | `0` | Duration of each START/resumed segment, in microseconds; `0` is continuous. Expiry requests disconnect. |
| `bool auto_connect` | `true` | Start the configured discovery connection when the radio becomes working. Not an automatic retry policy. |
| `bool auto_start` | `true` | Automatically request START after OPEN. Set false for explicit `resume()` or remote START. |
| `uint64_t control_timeout_us` | `10000000` | Nonzero deadline for local START/SUSPEND/RECONFIGURE completion; timeout fails closed and requests disconnect. |
| `bool allow_sample_rate_change` | `false` | Opt in to both 44.1 and 48 kHz. Requires `configure_source`; no resampler is supplied. |
| `bool (*configure_source)(uint32_t, void*)` | `nullptr` | Configure the application's PCM rate on accepted initial/change configuration. Return false if unsupported; do not reenter the player. |
| `void* source_context` | `nullptr` | Caller-owned context passed to `configure_source`; keep it alive while the owner can receive callbacks. |

`BeginResult`: `Ok` (initialization/power request succeeded), `InvalidSettings` (invalid address/rate, zero timeout, or missing variable-rate callback), `RuntimeAlreadyOwned` (owner claim unavailable), `InitializationFailed` (runtime/profile setup or power request failed). `Ok` does not mean connected or playing. An initialization failure may consume ownership; do not retry `begin()` as recovery.

### Methods

| Signature | Use and result |
| --- | --- |
| `BeginResult begin()` | Initialize the profile and request radio power once. |
| `void poll()` | Process Bluetooth, validate an existing-ACL binding and advance scheduling/audio. |
| `void connect()` | Request inquiry for the exact configured address. No return status and no automatic retry; inspect state/history. |
| `bool connectOverExistingAcl(const a2dp::AclBinding& expected)` | Explicit opt-in to reuse a current shared-runtime ACL to the configured peer. `true` means accepted and immediate SDK establishment succeeded, not connected/authenticated. |
| `bool listen(bool enabled = true)` | Enable/disable incoming admission for the configured peer; disabling does not disconnect an active session. |
| `bool pause()` | Cancel automatic start while Open, or request SUSPEND while Playing. Pending audio is discarded. |
| `bool resume()` | Request START for an Open/Suspended stream. |
| `bool reconfigureSamplingFrequency(uint32_t sample_rate)` | Request 44.1/48 kHz change while Open/Suspended with explicit variable-rate opt-in and source callback. Does not resume automatically. |
| `void disconnect()` | Request profile disconnect, or cancel pending connection intent. Does not power off other profiles. |
| `a2dp::State snapshot() const` | Copy the current state, without polling. |
| `Diagnostics diagnostics() const` | Copy transport and scheduling counters. |
| `size_t historySize() const` | Retained history count (default capacity 64). |
| `a2dp::HistoryEntry history(size_t oldest_index) const` | Oldest-first entry; out of range returns a default entry. |
| `uint64_t overwrittenHistory() const` | Entries overwritten by the bounded history. |
| `uint64_t droppedEvents() const` | Events lost by the bounded event queue. |

The player exposes nonblocking pause/resume and sample-rate reconfiguration. It has no track-selection or volume API. See [Source lifecycle controls](a2dp_source_lifecycle.md) for settings, transitions, callback obligations and tests. Receiving an AVRCP button does not automatically change playback. Serious state errors are not a reconnect policy; inspect and report them rather than looping on `begin()` or `connect()`.

`connectOverExistingAcl()` requires `auto_connect=false`, a begun/working/idle owner, idle inquiry/signaling/stream, no outstanding connection intent and no error. `AclBinding` contains `peer` (six bytes), `epoch` (nonzero runtime incarnation) and `handle` (actual current HCI handle). Obtain these from `BluetoothRuntime::linkForPeer()`, check `active`, then pass that exact identity. The implementation validates it again on completion and polls. It neither bypasses security nor cancels inquiry. ACL loss/replacement or radio-off makes this opt-in path terminal until reset. It cannot identify arbitrarily replayed historical SDK packets after handle/CID reuse.

The negotiated codec must be SBC stereo at the configured rate, or either 44.1/48 kHz when variable-rate operation is explicitly enabled, 16 blocks, 8 subbands, loudness allocation, and bitpool bounds within 2–53 (minimum no greater than maximum). Other codecs/modes are rejected. The player sends one to four complete frames when they fit. Otherwise its private transport sends one supported frame in 2–15 fragments; a payload that cannot meet that bound is rejected before PCM input. The payload includes the one-byte SBC header and excludes RTP. Sample/pacing completion advances only after the final fragment, while packet counters advance per accepted physical packet. The generic engine keeps its legacy 473-byte default; this player applies exact codec and fragment-count admission. See [complete-frame checkpoint](source_mtu_batching_contract.md) and [current bounded Source fragmentation](source_sbc_fragmentation_contract.md). Codec restrictions are unchanged.

### State and diagnostics

[a2dp::State and history definitions](../audio-core/connection/A2dpConnection.h):

- `power`: `Off`, `Starting`, `Working`; `inquiry`: `Idle`, `Active`, `Stopping`.
- `signaling`: `Idle`, `Connecting`, `Connected`; `stream`: `Idle`, `Configuring`, `Open`, `StartRequested`, `Playing`, `SuspendRequested`, `Suspended`, `ReconfigureRequested`; `credit`: `None`, `Requested`, `Granted`.
- `cid`, `seid`, `payload_limit`, `codec`, `initialized`, `connect_intent`, `paired`, `codec_ready` describe setup. `existing_acl`, `existing_acl_bound`, `existing_acl_stopped` describe explicit ACL reuse.
- `accepting` permits new incoming connections; `incoming` and `incoming_acl` describe an admitted link. `requested_rate` and `control_due_us` describe a pending local control. `delay_100us` stores the latest delay report in units of 100 microseconds; `delay_reports` counts reports.
- `packets` counts accepted physical packets, including incomplete fragments. `completed_media` counts completed encoded batches; `samples` advances only on completion. `media_deferrals` counts attempts with no accepted progress, not successful fragment sends. `max_lateness_us`, `pacing_rebases`, `pacing_discarded_us` describe scheduling. Timing fields ending in `_us` use microseconds.
- `error`: `None`, `InvalidInput`, `UnsupportedOperation`, `Failed`, `UnsupportedCodec`, `QueueOverflow`, `ClockRegression`. `status` preserves an SDK/internal result; `rejected` counts rejected events.
- `HistoryEntry` has `sequence`, `event`, `before`, `after`, `actions` and `count`. It is a bounded diagnostic record, not a permanent log.

`Diagnostics` fields: `rtp_timestamp`, `rtp_sequence`, physical `sent_packets`, `last_send_status`, `media_deferrals`, `pending_encoded_bytes`, completed-batch `completed_media`, and `abandoned_partial_frames` (discarded groups with at least one accepted nonfinal fragment, counted once). An unsent Busy batch is not partial abandonment. Lifetime playing maxima are `max_poll_gap_us`, `max_pump_us`, `max_credit_wait_us`. Use counters rather than per-packet printing. Timing/accounting guarantees retain the supported transport assumptions in the fragmentation contract.

### Minimal PCM example

This complete `main.cpp` sends silence to a configured speaker; replace the address and PCM provider. It intentionally has no automatic retry and no flash/pairing command.

```cpp
#include "A2dpSourcePlayer.h"
#include "pico/stdlib.h"
#include <algorithm>
class Silence : public IAudioSource {
public:
  bool hasSamples() const override { return true; }
  size_t read(int16_t* out, size_t count) override {
    std::fill_n(out, count, int16_t{0});
    return count;
  }
};
int main() {
  static Silence pcm;
  A2dpSourcePlayer::Settings settings;
  settings.target_address = "00:11:22:33:44:55"; // Replace with your speaker.
  settings.auto_connect = true;
  static A2dpSourcePlayer player(pcm, settings);
  if (player.begin() != A2dpSourcePlayer::BeginResult::Ok)
    while (true) tight_loop_contents();
  while (true) player.poll();
}
```

A2DP permits SSP confirmation for its configured peer; use an intended, authorized peer. See [manual connection sample](../examples/DeveloperSamples/a2dp_source.cpp) and [tone/diagnostics example](../examples/A2DPSourcePlayer/main.cpp) for application controls.

[Source + Sink integration and resource requirements](a2dp_dual_role.md)

## A2DP Sink: receive PCM from a Bluetooth source

This facade has bounded validation coverage. See the [complete runnable sample](../examples/A2DPSinkReceiver/README.md), [header](../audio-core/A2dpSinkReceiver.h) and [remaining SDK coverage](a2dp_sdk_coverage.md). Add `cmake/picow_a2dp_sink.cmake` and call `picow_target_a2dp_sink(app ${CMAKE_CURRENT_LIST_DIR})`. This helper reserves a 32 KiB stack; check the application's complete RAM budget.

### Output, settings and lifetime

Implement [IAudioSink](../audio-core/IAudioSink.h):

- `size_t write(const int16_t *pcm, size_t frames, uint32_t sample_rate, uint8_t channels)`: consume or copy borrowed interleaved signed-16 PCM before returning. `frames` is per-channel frame count, not bytes or `int16_t` count. Return the accepted prefix length, between zero and `frames`. Unaccepted frames are counted as dropped and never retried. This does not provide remote backpressure.
- `void reset()`: flush application-owned queued PCM on configuration/session/start/stop boundaries. It also runs synchronously from `disconnect()`. `write()` runs from `poll()`, not the radio callback. Keep both callbacks short and do not reenter receiver APIs.

Construct `A2dpSinkReceiver(output, settings)` with process-lifetime objects. `Settings::peer` is exactly one permitted source's `Address` (`std::array<uint8_t, 6>`); all-zero is invalid. `allow_pairing_confirmation` defaults to false. Setting it true permits the runtime's pairing-confirmation policy for this configured peer; it does not by itself implement an application's explicit IO-capability-reply handler.

`BeginResult` is `Ok`, `InvalidSettings`, `RuntimeAlreadyOwned`, `InitializationFailed` or `FeatureUnavailable`. `Settings::require_start_decision` defaults to false; enabling it without optional build support returns `FeatureUnavailable`. Repeated initialization attempts are not restart/recovery. An attempted owner retains SDK-referenced storage when destroyed; design for static lifetime rather than repeatedly constructing owners.

### Receiver methods

| Method | Meaning and result |
| --- | --- |
| `BeginResult begin()` | Claim the profile, initialize it and request radio power. Does not listen or connect. `Ok` does not mean the radio or stream is ready. |
| `void poll()` | Advance shared radio work, ordered control events and bounded media decoding. Call frequently from the sole main loop. |
| `bool listen()` | Enable incoming admission for the configured peer and shared-radio connectability. Requires a working, nonfailed owner with no closing session or queued control events. Does not enable discoverability. |
| `bool connect()` | Request one outgoing connection to the configured source. True means SDK acceptance, not establishment. Rejects duplicate attempts, closing sessions and an existing SDK connection, including a still-pending incoming one. |
| `void disconnect()` | Disable incoming admission and request closure of this A2DP signaling session, including a pending outgoing attempt. Clear queued media and reset the consumer. Keep polling until asynchronous closure completes. No radio power-off or automatic reconnect. |
| `DelayReportResult reportDelay100us(uint32_t delay_100us, uint32_t expected_generation)` | Report measured latency, 0–65535 units of 100 µs. Preserve the measurement's session generation. `Accepted` is local SDK admission, not delivery/remote acceptance. |
| `static bool startDecisionsAvailable()` | Check optional START-decision build support, not stream readiness. |
| `StartRequest pendingStart() const` | Copy the current `generation/request_id` token after poll; zero request ID means none actionable. |
| `StartDecisionResult decideStart(StartRequest request, bool accept)` | Decide outside callbacks with the complete token. `Accepted` and `SdkRejected` consume it; after SDK rejection disconnect rather than retrying that token. Observe `streaming` separately. |
| `State snapshot() const` | Return a value copy of current state. |
| `Diagnostics diagnostics() const` | Return a value copy of reception, parsing, decode and PCM counters. |

`State` includes `begun/working/accepting/connecting/closing/signaling/configured/streaming/failed`, session `cid/seid/generation`, codec `sample_rate/channels/sbc_channel_mode`, and `last_event/last_status/last_operation_status`. Distinguish the last event status from the last initialization/outgoing-request status; neither is a complete error history. Treat codec fields as meaningful only with `configured`, and do not infer pairing/security from state flags.

`Diagnostics` groups packet/byte counts; media/event overflow and oversize; discarded/wrong-state/rejected-peer/malformed events; missing/reordered packets and timestamp/source changes; decoded SBC frames and failures; generated/delivered/dropped PCM frames and invalid output returns; queue depth/high-water mark. `decoded_frames` counts SBC frames, whereas `pcm_frames`, `delivered_frames` and `dropped_frames` count per-channel PCM frames. `parse_errors[0..7]` corresponds to `None, Short, Rtp, Fragmented, Count, Codec, Bounds, Trailing` in the current parser.

Delay results use `DelayReportStatus::{Accepted, InvalidDelay, StaleSession, NotReady, NotNegotiated, SdkRejected}`; START results use `StartDecisionStatus::{Accepted, FeatureUnavailable, NotReady, StaleRequest, SdkRejected}`. Both result structs carry `sdk_status`, meaningful only for `Accepted/SdkRejected`. Neither API retries automatically. See [PCM ownership and delay reporting](a2dp_sink_pcm.md) and [opt-in START decisions](sink_start_decisions.md). Invoke them outside **all** callbacks on the main loop. Experimental `detachSharedRole()` / `cancelSharedDiscovery()` are macro-gated and are not part of the ordinary Sink + AVRCP use sequence.

### Scope and use sequence

1. Keep a static PCM consumer and receiver, with the exact source address configured.
2. Call `begin()` once, then continuously `poll()` until `snapshot().working`.
3. Explicitly call `listen()` for an incoming source, or `connect()` for one outgoing attempt. Watch `configured` and `streaming`; consume PCM only through `write()`.
4. Call `disconnect()` when needed, continue polling while `closing`, then explicitly choose the next operation.

The current PCM path implements the [128 documented SBC formats](sink_sbc_configurations_contract.md): 44.1/48 kHz, mono/dual/stereo/joint stereo, 4/8/12/16 blocks, 4/8 subbands and loudness/SNR allocation, with advertised bitpool 2–86 and format-specific negotiated limits. It accepts complete packets and [bounded SBC fragment groups](a2dp_sink_fragments.md). The full format/fragment matrix has offline evidence, not comprehensive wireless acceptance or independent codec conformance; the limited SBC hardware results in the development history is separate. Other codecs and optional 16/32 kHz remain unsupported. This receiver does not supply a DAC, I2S output, resampler, recording or AVRCP controller; AVRCP is a separate facade. Distinct-peer Source/Sink ownership and raw AVDTP role gating have bounded host evidence; same-peer registration remains rejected. Broader combined-role and SDK coverage remains a separate backlog, not a new prerequisite for the bounded SBC + AVRCP milestone.

## A2DP Sink with AVRCP: direction and coexistence

A2DP direction and AVRCP role are independent. For a Windows/phone player sending audio to Pico, the remote is **A2DP Source**, Pico is **A2DP Sink**, and Pico's **AVRCP Controller** sends playback commands to the remote **AVRCP Target**. Adding a local Target lets the remote Controller send requests in the opposite direction. An outgoing `connect()` does not change a role. Sink + AVRCP is not same-peer A2DP Source + Sink dual-media operation.

Use `picow_target_a2dp_sink` and `picow_target_avrcp_controller` with one compatible `btstack_config.h`; the Controller helper already includes Target support. Construct only one owner per role, configure allowed peers explicitly, begin each once, and poll every active facade on the same main loop. Observe Sink readiness/stream state and AVRCP `Connected`/live binding separately. Neither profile automatically opens the other, maps buttons to playback, or applies PCM gain. Shared AVRCP cancellation/disconnect affects both AVRCP roles; do not use it as an audio-stream stop command. See [combined public media-control sample](../examples/AvrcpMediaControlSample/README.md).

Host tests and successful builds do not qualify the public sample on hardware. Do not transfer results from a different test application; consult [Validation scope](validation.md) for the recorded scope.

## SPP: serial byte streams

### Types and bounds

[Core type definitions](../spp-core/SppCore.h):

| Type | Meaning |
| --- | --- |
| `Address = std::array<uint8_t,6>` | Bluetooth address in display order, e.g. `00:11:22:33:44:55` becomes `{0x00,0x11,0x22,0x33,0x44,0x55}`. |
| `Uuid` | 16-byte UUID. `Uuid::from16(0x1101)` produces the Bluetooth Serial Port UUID; `bytes` allows a 128-bit UUID. |
| `Config` | `peer`: exactly one allowed peer. `init_timeout_us` and `sdp_timeout_us`: each defaults to 10,000,000 microseconds. |
| `Client::Security` | `allow_confirmation=false`, `authentication=true`. Explicitly opt in to SSP confirmation if needed; do not disable authentication merely to hide a pairing problem. |
| `Handle` | `{generation, slot}`. Generation must be nonzero and current. Use `handleOf(snapshot, slot)`, never invent or reuse stale handles. |
| `IoResult` | `{size_t bytes, Error error}`. A copied byte count, not remote receipt. |
| `Candidate` | `{record, channel}` from SDP. Up to eight candidates. |

Two routes (`slot` 0/1), two listening endpoints, one allowed peer, 1024-byte TX and RX buffers **per route**, maximum 1024-byte RFCOMM frame, and one shared SDP query. Valid RFCOMM channels are 1–30. Two routes cannot use the same peer/channel. This is a byte-stream API: application message framing, acknowledgements, deduplication and retries belong to your protocol.

### Every `spp::Client` method

Construct with `Client(Config)` or `Client(Config, Security)`. It is noncopyable. Connection and serial-control requests enqueue work; even `true` can later become a rejected/failed operation. `configureErtm` instead validates/configures local storage before startup, and `ertmCompiled` is a compile-time capability check. Observe `snapshot()` and `history()` after polling.

| Signature | Purpose / completion check |
| --- | --- |
| `bool begin()` | Queue startup; wait for `runtime == Runtime::Ready`. |
| `void poll()` | Service runtime, tick timeouts and drain queued events. |
| `bool connect(uint8_t slot, Address peer, uint8_t channel)` | Connect a closed route to a known channel. Peer must equal `Config.peer`. Wait for `routes[slot].phase == Phase::Open`. |
| `bool connect(uint8_t slot, Address peer, Uuid uuid)` | Resolve UUID, then connect. A single candidate is selected automatically; multiple candidates require `select()`. |
| `bool discover(Address peer, Uuid uuid)` | SDP discovery without opening a route. Inspect `query.phase`, `query.candidates[0..count)`, `query.error/status`. |
| `bool cancelDiscovery(uint32_t token)` | Cancel the matching query token from the current snapshot. SDK completion must still drain; cancellation is not immediate SDP availability. |
| `bool select(Handle h, uint8_t index)` | Select a candidate for a route in `Selecting`; index is zero-based. Immediately returns false for a stale handle, otherwise queues the selection. |
| `bool disconnect(Handle h)` | Request route teardown/cancel. Buffered unsent TX/unread RX is discarded. Wait for `Closed`; a failed SDK disconnect can leave `Closing`. |
| `bool listen(uint8_t slot, uint8_t channel, Uuid uuid)` | Register a local endpoint, not an outgoing connect. Inspect `endpoints[slot].phase`. Incoming connections still require the configured peer and a free route. |
| `bool unlisten(uint8_t slot)` | Withdraw a listening service/SDP registration; existing routes remain connected. |
| `bool control(Handle h, ControlRequest request)` | Queue a validated control for an open current handle; inspect local SDK status, not a peer ACK. |
| `bool sendLineStatus(Handle h, uint8_t value)` | Queue line status; valid wire values follow the control guide. |
| `bool sendModemStatus(Handle h, uint8_t value)` | Queue modem status; does not itself stop the byte stream. |
| `bool configurePort(Handle h, PortSettings port)` | Request virtual-port settings; does not configure a physical UART. |
| `bool queryPort(Handle h)` | Queue a remote-port configuration query. |
| `bool selectPeer(Address peer)` | Queue quiescent one-peer selection; wait for snapshot confirmation after routes, SDP and leases drain. |
| `static constexpr bool ertmCompiled()` | Check required ERTM defines and overlay support; not negotiated transport. |
| `bool configureErtm(const ErtmPool::Config& config)` | Configure caller-owned buffers before startup, while stopped with no queued work. |
| `ErtmPool::Status ertmStatus() const` | Copy pool/lease status; an allocated lease does not prove ERTM negotiation. |
| `Transport transport(Handle h) const` | Observe actual open transport; stale, closed or unobservable handles yield Unknown. |
| `IoResult tryWrite(Handle h, const uint8_t* p, size_t n)` | Copy as much as fits into TX now. Retain and retry only the unaccepted suffix. No caller pointer is retained. |
| `IoResult read(Handle h, uint8_t* p, size_t n)` | Copy up to capacity from RX; returns bytes removed. No caller pointer retained. |
| `State snapshot() const` | Current state copy, no polling. |
| `size_t historySize() const` | Retained history count (default 16). |
| `HistoryEntry history(size_t i) const` | Oldest-first; invalid index yields default entry. |
| `uint64_t droppedEvents() const` | Dropped queued events, not bytes. |

There is no facade `stopListening()`, `stop()` or `overwrittenHistory()` method. Core-engine methods are not implicitly available through `Client`. `listen()` registers a service but does not itself make the radio globally discoverable/connectable; use an intentional application policy, as in the developer samples, for incoming connections.

### State, results and backpressure

- `Runtime`: `Stopped`, `Starting`, `Ready`, `Failed`. A failed runtime is terminal until reset.
- Route `Phase`: `Closed`, `Resolving`, `Selecting`, `Connecting`, `Open`, `Closing`.
- `EndpointPhase`: `Disabled`, `Registering`, `Listening`, `Failed`.
- `QueryPhase`: `Idle`, `Querying`, `Complete`, `Failed`, `TimedOut`, `Cancelled`; inspect `busy` as well before another query.
- Route fields include `generation`, `peer`, `channel`, `cid`, `mtu`, `error`, `status`, `candidate_count`, `candidates`, `query_token`, `tx_size`, `rx_size` and `txFree()`.
- `tx_accepted` counts copied bytes; `tx_sent` counts SDK-accepted sends, not application acknowledgement. `tx_discarded`, `rx_received`, `rx_read`, `rx_discarded` make loss/consumption explicit. `writable_epoch` changes when a route goes from no TX space to some TX space.
- `HistoryEntry` contains `sequence`, `event`, `before/after` views, `actions`, `count`, `result`, `writable_mask`. `State.error/status` alone does not describe every rejected command; consult route/query and history results.

`Error` values and handling:

| Values | Interpretation |
| --- | --- |
| `None` | No error for that operation; partial I/O can still return `None`. Always use `bytes`. |
| `WouldBlock` | Nonzero write with no TX space, or nonzero read with empty RX. Poll and retry later. |
| `Busy` | I/O attempted while events are pending/draining, or another operation owns a needed resource. Poll before retrying; do not spin without polling. |
| `InvalidInput`, `InvalidHandle`, `NotReady`, `NotConnected`, `DuplicateChannel` | Fix arguments, current generation or phase before retrying. |
| `Failed`, `Timeout`, `NotFound`, `CandidateOverflow`, `Cancelled` | Inspect route/query, SDK `status`, and history. Do not silently choose a peer/channel or restart the owner. |
| `StaleEvent` | Event no longer matches the live connection/query. |
| `QueueOverflow`, `ClockRegression`, `RxOverflow`, `TokenExhausted` | Bounded-resource/invariant failure; inspect whether the route or runtime has failed. |

A zero-length read/write can succeed with zero bytes if the handle and connection are valid; null pointers are invalid only with nonzero length. Even after an `Open` snapshot, I/O can report `Busy` because callbacks queued additional work.

### One connection, partial write and read

This loop fragment assumes a process-lifetime `client` constructed with the intended `Config.peer`, successful `client.begin()`, and a peer actually serving RFCOMM channel 1. Do not guess a service channel for a real device. Keep the saved handle for the lifetime of this transfer; never continue the same suffix on a new generation without an explicit application retry policy.

```cpp
bool requested = false, bound = false, finished = false;
spp::Handle transfer{};
const uint8_t message[] = {'p', 'i', 'n', 'g', '\n'};
size_t offset = 0;
while (true) {
  client.poll();
  const auto state = client.snapshot();
  if (!requested && state.runtime == spp::Runtime::Ready) {
    requested = true; // Exactly one attempt, even on queue rejection.
    if (!client.connect(0, state.config.peer, uint8_t{1})) finished = true;
    continue;
  }
  const auto& route = state.routes[0];
  if (!bound && requested && route.phase == spp::Phase::Open) {
    transfer = spp::handleOf(state, 0);
    bound = true;
  }
  if (!bound || finished) continue;
  if (!spp::matches(state, transfer) || route.phase != spp::Phase::Open) {
    finished = true; // Report failure; do not replay into another connection.
    continue;
  }
  if (offset < sizeof(message)) {
    const auto result = client.tryWrite(transfer, message + offset,
                                        sizeof(message) - offset);
    offset += result.bytes;
    if (result.error != spp::Error::None && result.error != spp::Error::Busy &&
        result.error != spp::Error::WouldBlock) finished = true;
  }
  uint8_t received[64];
  const auto result = client.read(transfer, received, sizeof(received));
  // Consume only received[0..result.bytes); it is not a C string.
  // Check result.error; Busy/WouldBlock mean try again after polling.
}
```

For full status/error handling, two routes, listening and explicit reconnect controls, see [SppSample.h](../examples/DeveloperSamples/SppSample.h), [PendingMessage.h](../examples/DeveloperSamples/PendingMessage.h), [spp_two_routes.cpp](../examples/DeveloperSamples/spp_two_routes.cpp), and [spp_reconnect.cpp](../examples/DeveloperSamples/spp_reconnect.cpp).

## AVRCP Controller: send commands and observe responses

Include [AvrcpController.h](../avrcp-sdk/AvrcpController.h); Controller-specific types below are nested in `picow::avrcp::AvrcpController`; `Address` and `Binding` belong to `picow::avrcp`. `Config` aliases `AvrcpTarget::Config`. Begin once with one or two distinct nonzero allowed peers; stop is terminal. A `Binding` contains peer, ACL epoch, CID and owner-local generation. Obtain it from this Controller's `Connected` event or `bindingForPeer()`, not from Target or a CID alone; reject stale queued work after disconnect/reset.

| Signature | Contract |
| --- | --- |
| `bool begin(const Config&, bool advertise_browsing = false)` | Local initialization result, not remote connection. Advertising browsing does not create a Browsing owner. |
| `void poll()` / `void poll(uint32_t now_ms)` | Service shared runtime; only the timed overload advances the opt-in command deadline. |
| `bool setCommandTimeout(uint32_t milliseconds)` | 0 disables; maximum `0x7fffffff`. Use monotonic timed polls. Expiry requests shared signaling teardown, not guaranteed immediate cleanup. |
| `void stop()` | Terminal owner shutdown. Normally retains signaling owned by Target; failed held-button release can force shared disconnect. |
| `int connect(const Address&)` / `int disconnect(const Binding&)` | Local request status; shared-signaling effects are the same as Target. |
| `int cancelConnection(const Address&)` | Cancel pending shared connection setup; keep polling for cleanup. |
| `bool cancellationPending(const Address&) const` / `int cancellationStatus(const Address&) const` | Inspect cancellation, not new-connection success. |
| `Binding bindingForPeer(const Address&) const` | Current binding or zero/default binding if absent. |
| `Phase phase(const Binding&) const` | `Idle`, `PressPending`, `Held`, `ReleasePending`. Phase alone is not proof of a live binding. |
| `int press(const Binding&, uint8_t operation)` / `int release(const Binding&)` | Send a standard no-operand button and end SDK hold/repetition. Explicit release is required; it may be requested before the press response. |
| `int query(const Binding&, Query)` | Query capabilities, play status, now-playing information or settings; decode later response, not the return value. |
| `int group(const Binding&, bool next)` | Vendor-unique next/previous **group** click, not track NEXT/PREVIOUS. |
| `int notification(const Binding&, avrcp_notification_event_id_t, bool enable)` | Register or stop renewal. Disable is not an immediate wire unsubscribe; pending INTERIM may reject it locally. |
| `int absoluteVolume(const Binding&, uint8_t volume)` | 7-bit 0–127 request, not a 0–100 UI percentage or local gain setting. Separate from `press(0x41/0x42)` volume buttons. |
| `const Status& status() const` | `started/stopped`, `malformed/ignored/connection_errors/responses`, `last_error`; not an application playback result. |

Command methods returning `int` use zero for local SDK acceptance, nonzero for local failure. Do not treat these as `bool` success values. `setCallback(void (*)(const Event&, void*), void* = nullptr)` receives `Connected`, `Disconnected`, `PressResponse`, `ReleaseResponse`, `Reset`, `Timeout` or `CustomError`. Button responses expose AV/C response 9 Accepted, 10 Rejected or 8 Not Implemented; even Accepted does not prove a player changed track or UI volume. An early release can yield completion without a separate press response. Keep one button operation pending per peer, check results and never blindly replay after a generation change.

`setSdkCallback` supplies `SdkEvent::{binding, bytes, size}`. `setSettingsCallback` and `setCustomCallback` supply `SettingsChunk` / `CustomChunk` with `binding, response, pdu, total, offset, bytes, length` and `final()`. These bytes and callback references are **borrowed only during the synchronous callback**. Copy bounded bytes, not just the pointer-containing struct, before retaining work. Validate sizes and complete chunk assembly before decoding nested fields; payloads are not necessarily C strings. Keep callbacks short and observational, then issue commands outside all callbacks on the main loop.

Additional `int` adapters include `elementAttributes`, `settingValues`, `batteryStatus`, `addressedPlayer`, `maximumFragments`, `playItem`, `addItem`, `displayableCharsets`, `settingAttributeText`, `settingValueText`, `currentSettings`, `setSettings`, `shuffleMode`, `repeatMode` and `customCommand`. Parameter types/bounds, overlay requirements, notifications, cancellation and response assembly are in the [Controller contract](avrcp_controller.md) and [SDK overlay guide](avrcp_sdk_overlay.md). Presence here does not claim radio validation of those features.

## AVRCP: receive remote buttons

`picow::avrcp::AvrcpTarget` handles the Target role. It now also exposes explicit outgoing `connect(peer)` and `disconnect(binding)`; see [shared Target/Controller lifecycle](avrcp_controller.md). Target and Controller share signaling/SDP ownership. They do not automatically connect, permit pairing, enable discoverability or map buttons to player operations.

### Configuration and methods

`Config` has `std::array<Address,2> peers` and `uint8_t peer_count=0`. Supply one or two distinct nonzero addresses; unused entries remain zero. `Address` is a six-byte array.

| Signature | Contract |
| --- | --- |
| `AvrcpTarget()` / `~AvrcpTarget()` | Noncopyable owner. Keep alive for the program lifetime; destructor calls `stop()`, not a reusable restart. |
| `bool begin(const Config&, bool advertise_browsing = false)` | Validate peers, claim profile, register SDP and request power. `true` means started, not connected. Once ownership/setup is attempted, do not retry on this owner. |
| `void poll()` | Service runtime; stop this profile on terminal runtime failure. |
| `void stop()` | Terminal for this owner; reset this role; retain signaling still owned by the other role. Does not power off A2DP/SPP. |
| `int connect(const Address&)` / `int disconnect(const Binding&)` | Zero is local SDK acceptance; explicit disconnect closes shared AVRCP signaling for both roles, not the A2DP stream or radio. |
| `int cancelConnection(const Address&)` | Cancel pending shared signaling setup, not an established link. Keep polling; acceptance does not mean resources are released. |
| `bool cancellationPending(const Address&) const` / `int cancellationStatus(const Address&) const` | Observe cancellation cleanup and its local status; not proof of remote connection. |
| `void setCallback(void (*)(const Event&, void*), void* context)` | Set callback and borrowed context. Passing null disables callback. |
| `bool setSupported(uint8_t operation, bool enabled)` | Change advertised operation policy **before `begin()`**. Invalid IDs or an already-attempted begin return false. |
| `bool setPlayback(uint32_t length_ms, uint32_t position_ms, uint8_t playback_status)` | Store response to play-status queries. Length/position in milliseconds; `UINT32_MAX` means unknown. Status: 0 stopped, 1 playing, 2 paused, 3 forward seek, 4 reverse seek, 0xff error. Other values return false and do not update. |
| `const AvrcpTargetCore& core() const` | Read-only button state, bindings and counters. |
| `const Status& status() const` | Adapter status: `started`, `stopped`, `malformed`, `ignored`, `connection_errors`, `response_errors`, `last_error`. |

Default supported operation IDs: `0x41` volume up, `0x42` volume down, `0x44` play, `0x45` stop, `0x46` pause, `0x48` rewind, `0x49` fast forward, `0x4b` forward/next, `0x4c` backward/previous. These are pass-through button IDs, not a volume-setting or music-player implementation.

### Metadata, notifications and incoming requests

`int setMetadata(const Binding&, const Metadata&)` copies title/artist/album/genre (maximum 255 bytes each) into owner storage. It may return command-disallowed while a response/continuation retains that storage. `int setCompanies(...)` and `int setSubunitInfo(...)` also copy capability buffers; consult the [header](../avrcp-sdk/AvrcpTarget.h) and [Target feature contract](avrcp_target_features.md) for exact bounds, `setUnitInfo`, notification APIs and player-setting support. `bool configurePlayerSetting(uint8_t attribute, uint8_t initial_value)` configures initial support before begin; `bool updatePlayerSetting(uint8_t attribute, uint8_t value)` updates an enabled value.

`int supportNotification(const Binding&, uint8_t event_id)` and `notifyPlayback/notifyVolume/notifyTrack/...` manage supported events and reported state. `notifyVolume` accepts 0–127 and does not apply PCM gain. Local zero results are not proof that a remote UI changed. `uint32_t positionNotificationInterval(const Binding&) const` returns seconds, unlike millisecond position values.

`setRequestHandler(Response (*)(const Request&, void*), void* context)` answers bounded synchronous requests for absolute volume, item actions, addressed player and player settings. `Response` carries AVRCP `status` and actual `volume`; the default volume response preserves the request without implementing audio gain. `setPassThroughHandler(PassThroughDecision (*)(const PassThroughRequest&, void*), void* context)` is overlay-only and returns `NotImplemented=8`, `Accepted=9` or `Rejected=10`. These decision handlers are exceptions to purely observational callbacks: return the immediate protocol decision, copy application intent, and defer playback/hardware work until the main loop. Do not reenter APIs, wait for a person, or retain request references. Supporting or accepting an operation does not execute its application effect automatically.

### Events and callback safety

`Event` fields: `kind`, `binding`, `operation`, `pressed`, `result`. `Kind`: `Connected`, `Disconnected`, `Operation`, `Reset`, `Rejected`. `Binding` contains `peer`, nonzero ACL `epoch`, AVRCP `cid`, and monotonically assigned `generation`. Do not identify a live connection by CID alone.

The callback may inspect state but must not mutate the core/target reentrantly. Copy the event or record application intent, then act after `poll()` returns. Treat `pressed=true/false` as press/release, not two clicks. Cancel held-button actions on `Disconnected` or `Reset`; the core does not invent release events. Duplicate press/release sequences are rejected.

```cpp
struct Buttons {
  bool stop_requested = false;
  static void receive(const picow::avrcp::Event& e, void* context) {
    auto& self = *static_cast<Buttons*>(context);
    if (e.kind == picow::avrcp::Kind::Operation &&
        e.operation == 0x45 && e.pressed) self.stop_requested = true;
  }
};
// Setup, before the main loop (keep these objects/context alive):
static Buttons buttons;
static picow::avrcp::AvrcpTarget target;
// target.setCallback(Buttons::receive, &buttons);
// target.setPlayback(UINT32_MAX, UINT32_MAX, 1);
// if (!target.begin(config)) { /* report startup failure */ }
// Loop, with an already-begun A2dpSourcePlayer named player:
// target.poll();
// player.poll();
// if (buttons.stop_requested) {
//   buttons.stop_requested = false;
//   player.disconnect(); // Explicit application policy, not automatic mapping.
// }
```

See [a2dp_avrcp.cpp](../examples/DeveloperSamples/a2dp_avrcp.cpp) and [three-profile example](../examples/DeveloperSamples/a2dp_spp_avrcp.cpp) for complete integration.

### Read-only core and advanced standalone core

[AvrcpTargetCore](../avrcp-core/AvrcpTargetCore.h) is also usable without hardware for deterministic tests. Through `target.core()`, use `status()`, `supported(operation)`, `bindingForCid(cid)`, `bindingForPeer(peer)`, `isPressed(binding, operation)` and `validateOperation(binding, operation, pressed)`. Missing bindings return a default/zero binding. Core `Status` contains `connections`, `disconnections`, `operations`, `rejected`, `resets`, `active`, `last_result`.

For a separately constructed core only, mutators are `setCallback(callback, context=nullptr)`, `setSupported(operation, enabled)`, `connect(requested, accepted=nullptr)`, `disconnect(binding)`, `operation(binding, operation, pressed)` and `reset()`. They do not perform Bluetooth I/O. `connect()` assigns the accepted generation. `setSupported()` requires no active peers and operation < 128. `reset()` clears connections but preserves counters, support policy and the monotonic generation. `Result` is `Ok`, `InvalidInput`, `NotConnected`, `StaleBinding`, `Capacity`, `Duplicate`, `Unsupported`, `InvalidSequence` or `Exhausted`. Never use a second core to inject state into the SDK adapter.

## Shared runtime (advanced)

Normally the facades own runtime setup. These public methods are for inspection or writing a new adapter, not extra initialization steps for an ordinary application.

| Method | Meaning / return |
| --- | --- |
| `static BluetoothRuntime& instance()` | Process singleton. |
| `bool claim(Profile)` | One lifetime claim per `A2dp`, `Spp`, `Avrcp`, `A2dpSink`; no release. False if already claimed, failed, invalid profile, or externally initialized CYW43. |
| `int initialize()` | Initialize once; 0 success, nonzero failure. Already-initialized owned runtime returns 0. External CYW43 ownership fails. |
| `int powerOn()` / `int powerOff()` | Shared-radio requests, 0 success/nonzero error. Completion is asynchronous. Power-off affects every profile; not a per-profile disconnect. |
| `void poll()` | Poll CYW43 and process pending confirmation decisions. |
| `bool ownsRadio() const` / `bool failed() const` | Initialized ownership / terminal runtime failure, not a connection or authentication check. |
| `void setHandler(Profile, btstack_packet_handler_t)` | Set one borrowed profile handler; do not overwrite a facade's handler. |
| `void setPairingPolicy(Profile, Address, bool)` | Allow or deny SSP confirmation for that profile's exact peer. |
| `void confirmPairing(const Address&)` | Respond only to a pending confirmation; approve only if a claimed profile's matching policy allows it. |
| `void setSecurityLevel(Profile, uint8_t)` | Clamp level to 0–4; global level is the maximum of claimed profiles. |
| `Link linkForPeer(const Address&) const` / `Link linkForHandle(uint16_t) const` | Current active ACL (`peer`, `epoch`, `handle`, `active`), or default inactive link. Check `active`. |
| `int startSdp(uint32_t token, const Address&, const std::array<uint8_t,16>& uuid, btstack_packet_handler_t)` | Acquire shared SDP query; 0 means SDK start succeeded, nonzero failure/busy. Use nonzero token and non-null callback. |
| `void cancelSdp(uint32_t token)` | Mark matching query cancelled; wait for SDK completion to free ownership. Does not reset SDK-private SDP state. |
| `bool sdpOwned() const` | Whether the shared SDP token is still held. |

## Legacy and internal interfaces

PicoWBluetooth.h is a **legacy aggregate header**, not the entry point for the current facades. Its `AudioManager`, connection plugins, controllers and HFP-related classes belong to the older plugin architecture. Their presence does not establish working HFP microphone transport, FastStream support or an A2DP sink application API. Do not initialize them alongside the current profile owners.

`AudioPipeline`, `BTStackA2DPTransport`, SBC encoder adapters, `a2dp::Rules/Engine`, `spp::Rules/Engine` and SDP/parser helpers are lower-level implementation/testing surfaces. This reference documents the application facades and their returned types, not a stability promise for every public C++ declaration in internal headers. In particular, do not configure the singleton `AudioPipeline` separately while `A2dpSourcePlayer` owns it.

## Practical troubleshooting

1. Check startup result, then keep polling and check the actual profile state. Queue acceptance and radio working do not imply profile connection.
2. For SPP, retain a generation-bound handle, check both `IoResult.bytes` and `error`, and keep unsent suffixes. Full TX or empty RX is not necessarily a connection failure.
3. For audio gaps, compare source availability, `media_deferrals`, poll-gap and credit-wait maxima before increasing buffers. Avoid per-packet logging.
4. For buttons, inspect both adapter `status()` and core counters, configure the exact allowed peer, and handle press/release plus disconnect/reset.
5. Validate your own hardware and peer. The [developer sample checks](../examples/DeveloperSamples/check.sh) compile and run host tests; they do not flash, pair or verify over-the-air behavior.

Experimental same-peer role join/detach APIs are isolated behind an explicit target macro and are not part of the normal guaranteed interface. See the [same-peer experiment](a2dp_same_peer_experiment.md) for build setup, lifecycle and remaining limitations.
