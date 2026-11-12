# Optional Sink START decisions

## Scope and build

The public `A2dpSinkReceiver` can explicitly accept or refuse a remote AVDTP START. This is optional at two levels:

1. After `picow_target_a2dp_sink(target config_directory)`, call `picow_enable_sink_start_decisions(target)`.
2. Before `begin()`, set `Settings::require_start_decision = true`.

`startDecisionsAvailable()` reports compiled support. A receiver requesting manual decisions without support returns `BeginResult::FeatureUnavailable` before claiming the runtime. The default setting remains automatic START, even when support is compiled. Source endpoints retain the installed SDK's automatic START path, including Source and Sink compiled in the same executable. This isolation is not a claim of complete bidirectional/shared-profile or hardware interoperability.

The CMake helper sets the SDK explicit-confirmation macro together with the library's feature macro. Do not set the SDK macro globally by itself: the generated acceptor rejects that unsupported combination at compile time because it would change Source behavior. The existing same-peer experimental overlay cannot share this configure. The installed SDK is never edited.

## Application contract

After `poll()`, obtain `pendingStart()`. A nonzero `request_id` is actionable and contains a signaling-session `generation` plus a monotonically allocated request identity. Pass the whole value to `decideStart(request, true)` to allow or `decideStart(request, false)` to refuse. There is one pending request, no dynamically growing queue, and no application callback inside the SDK START hook.

Call these operations only from the single application loop, outside `poll()`, PCM `write()` and `reset()` callbacks. Keep polling. Do not block waiting for user input. The library does not automatically accept, reject, time out or retry a manual request. The application must decide promptly or disconnect. The remote peer can time out first; local admission cannot prove that it is still waiting.

Results:

- `Accepted`: the SDK accepted the decision for later sending, not proof of wire transmission, remote receipt, or streaming. Observe the ordinary `snapshot().streaming` transition from `STREAM_STARTED` after accepting.
- `FeatureUnavailable`: optional build support is absent.
- `NotReady`: lifecycle work is still pending, or the call is reentrant/not begun. Continue polling before reconsidering a still-current request.
- `StaleRequest`: the identity no longer identifies a live waiting START. Never substitute a current CID into an old decision.
- `SdkRejected`: the exact SDK error is in `sdk_status`; this is different from an application refusal. The attempted decision consumes the token even on SDK error. Disconnect to abandon it; there is no automatic retry.

Both an admitted allow and an admitted refusal return `Accepted`. The Boolean argument identifies which decision was queued. `sdk_status` is meaningful only for `Accepted` and `SdkRejected`.

START decisions never set `streaming` directly. Repeated can-send notifications do not renew a request ID. A later START, including suspend/restart in the same signaling session or reused transaction label, receives a fresh ID. Decisions are bound to the live ACL epoch, signaling generation, SDK connection, owned Sink endpoint, START transaction label, local SEID and SDK waiting state. Disconnect, stop/close/abort/release, configuration boundaries, runtime shutdown/failure or stale SDK state invalidate access to the request. Event queue loss fails closed. Counter exhaustion fails closed instead of reusing an identity. A public snapshot can lag SDK callbacks; it does not grant decision authority.

A refusal uses the AVDTP START reject body with failed ACP SEID and BAD_STATE, as specified by [AVDTP 1.3 §8.13.3/Table 8.30](https://www.bluetooth.org/DocMan/handlers/DownloadDoc.ashx?doc_id=260860). This corrects the installed SDK's bare reject only for Sink; existing raw SBC/configuration guard and normalized application-veto recovery stay in the generated acceptor.

## Example

`examples/A2dpSinkStartDecisionSample` drops PCM and has no audio renderer. Radio is OFF by default and the peer is empty. `SINK_START_DECISIONS=ON` compiles support. A radio-enabled build additionally requires `SINK_START_ENABLE_RADIO=ON` and an explicit `SINK_START_PEER`. Building is not permission to flash or run it.

The example chooses the policy before a request: `a` permits future STARTs, `r` refuses them (default). It immediately answers each observed request with that policy, rather than waiting for a human during the peer's response timeout. `b` initializes only an explicitly radio-enabled build, `l` listens, `c` requests connection, `x` disconnects, `s` reports status. No automatic listen/connect/reconnect. The fixed one-command-per-loop budget and nonblocking USB output preserve loop progress; this is not a real-time guarantee.

## Validation scope

Optional START decisions have host and compile-only ARM coverage. Actual peer response timers, radio execution of manual decisions, Source interoperability and complete shared-profile behavior remain unverified. Keep polling and decide promptly; no local timeout or automatic retry is supplied.

[Validation scope](validation.md)
