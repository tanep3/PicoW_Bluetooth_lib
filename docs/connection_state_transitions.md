# Connection states, events, and side effects (Phase 1)

**Historical Source state-observation record; its original scope and logs are retained below.**

[日本語](connection_state_transitions_jp.md)

The evidence is the 2026-11-07 LBS hardware record,
especially the continuous USB logs device-03 and device-04 (sequence 1815 onward). This scope covers observation of one A2DP Source peer.
Do not first invent a general state machine for all Bluetooth, AVRCP, or HFP behavior.

## States and observations

One State is the source of truth for connection management.

- Power: off / starting / working.
- Inquiry: idle / active / stopping. Discovery does not mean connection.
- Target: the Bluetooth address supplied by the application. Other addresses are not connection targets.
- Pairing: not observed / request answered / complete (status retained).
- A2DP signaling: idle / connecting / connected, plus cid.
- Codec: unconfigured / accepted 48 kHz or 44.1 kHz, stereo, 16 blocks, 8 bands, loudness, and bitpool.
- Stream: idle / configuring / open / start_requested / playing.
- Send permission: not requested / pending / one packet available. This is separate from playing.
- Time: monotonic time supplied through Tick; start delay, next transmission, and short-test completion deadlines.
- Connection intent: explicit connect or configured auto_connect. Disconnect withdraws intent so a late connection cannot advance to playback.
- Last action result, transmission totals, and errors. Missing logs are reported separately as observation-system state.

Transport readiness is a projection of State. Do not collapse connected and able-to-send into one Boolean.
Query only copies State and history; it never calls Bluetooth APIs or changes state.

## Observed transitions

| Previous state / condition | Normalized Event | Next state | Action (returned as data) | Evidence |
|---|---|---|---|---|
| Power off | PowerRequested | starting | PowerOn | seq2–6 |
| starting | PowerWorking | working | None | seq51–52 |
| working, no connection, inquiry idle | DiscoveryRequested | inquiry active, found=false | StartInquiry | seq54 |
| inquiry active, other address | DeviceSeen | Unchanged | None | seq57–58 |
| inquiry active, target address | DeviceSeen | found=true, inquiry stopping | StopInquiry | seq61–64 |
| inquiry stopped, found=true | InquiryFinished | inquiry idle, signaling connecting | EstablishA 2dp(target) | seq65–68 |
| Connecting to specified peer | ConfirmationRequested | Pairing request answered | ConfirmPairing(target) | seq114–115 |
| Connecting | PairingFinished(status) | Store pairing result | None | HCI pairing complete |
| signaling connecting, status=0 | SignalingEstablished | signaling connected, store cid | None | seq144–145 |
| Peer capabilities received | CapabilitiesObserved | Observation only | None (selection currently belongs to BTstack A2DP) | seq154–166/WIRE18–24 |
| Peer accepts SET_CONFIGURATION | CodecConfigured | Store codec configuration | ConfigureAudio(codec) | WIRE25/seq170–171 |
| Media connection succeeds | StreamOpened | stream configuring | ConfigureTransport | WIRE33/seq190–192 |
| Transport configuration succeeds | TransportConfigured | stream open, start deadline=now+configured delay, store payload limit | None | Configuration API result explicitly returned by Executor |
| stream open, start deadline reached | Tick | start_requested | StartStream(cid,seid) | seq208–210 |
| Peer accepts START | StreamStarted | playing, next send=now, end=now+3000 ms | SetTransportStreaming(true) | WIRE39/seq212–213 |
| playing, deadline reached, permission not requested | Tick | Permission pending | RequestCanSend | Immediately before first media transmission |
| playing, notification matching request | SendCreditGranted | Permission for one packet | GrantTransportCredit | A2DP CAN_SEND |
| playing, permission available | Tick | Consume permission, await send result | PumpAudioOnce | WIRE40–42 |
| Media-send API result | MediaResult | Update successes/samples and advance next deadline by 512/rate seconds, or record error | Stop transmission on failure (no automatic retry) | STATE totals; failure branch not hardware-validated |
| playing, short-test deadline reached | Tick | End playing, clear permission | SetTransportStreaming(false), DisconnectA 2dp | seq932–936 |
| Stream release notification | StreamReleased | stream idle, clear permission | SetTransportStreaming(false) | seq939–940 |
| Signaling release notification | SignalingReleased | signaling idle, cid=0, codec unconfigured | ClearConnection | seq943–944 |

After normal signaling release, receiving another explicit DiscoveryRequested and repeating the procedure was confirmed in device-04 sequences 1815–2699.
Clear connection-specific codec, credit, deadlines, and pairing-response flags. Do not reconnect automatically or advance incoming connections without explicit connection intent to playback.

The four-second start delay and three-second duration are **observation-application policy**, not universal Bluetooth rules to hard-code inside the engine.
The application supplies delay and test duration through State/configuration data. Source and output configuration should also be visible in the example.

## Scope of raw-event interpretation

- Normalize inquiry, pairing/security, and A2DP codec/open/start/release into the table above.
- Preserve HCI initialization CommandComplete/CommandStatus, transport packet sent, completed packets, and feature/version/role/security notifications in history as low-level BTstack facts. These alone do not advance connection State or create custom connection side effects.
- BTstack handles the raw exchanges for SDP queries, L2CAP configuration, and AVDTP Discover/GetCapabilities/SetConfiguration/Open/Start. Rules correlate acceptance notifications with raw traces rather than reimplementing those exchanges.
- WIRE36–37 AVRCP PSM 0017 requests/unsupported responses are observations only. Do not invent an AVRCP-success State.
- Preserve unknown events as Observed without changing state or side effects. Do not silently count them as success.
- Only the first three transmission payloads are captured, not every packet. Distinguish cumulative counts from observed media content.

## Unobserved inputs and safety boundaries

Complete pause/resume, failure-status, and interrupted-disconnection rules cannot be labeled validated from this success log alone.
Only explicit reconnection after normal release was additionally measured, not automatic retry after failure.
The implementation explicitly rejects unsupported/invalid requests, records errors, and does not retry automatically.
Update this table and host tests when additional required events are observed.
Do not add HFP. Do not invent AVRCP success beyond the measured unsupported request.

## Implementation boundaries (Phase 2)

- Rule generates Transition from (State, Event) only. It reads neither external time nor BTstack.
- Only ActionExecutor calls Bluetooth APIs or operates Transport/AudioPipeline.
- Engine owns the event queue, rule application, action execution, state commitment, and history.
- If BTstack invokes a synchronous callback during an Action, enqueue the reentrant Event and process it only after the current Transition is committed. Do not reenter uncommitted State.
- Actions explicitly receive the Transition's next State and return execution results as Events.
- Retain the Phase 0 example as a comparison. This is distinct from repairing the entire old plugin path.

## Protective rules (host-tested, not reproduced on hardware)

These are safety boundaries that prevent continued sound output for unsupported input, not rules inferred about normal peer behavior from a success log.
Fix the input sequences and outcomes in host tests without describing them as hardware-validated.

- Disconnect withdraws connection intent even during power startup, discovery, or pending connection. If the connect API has not yet returned a cid, explicitly disconnect the cid when it arrives late.
- Reject/disconnect connections from other addresses, incoming connections without intent, and established cids different from the existing connection.
- Treat StreamSuspended as unsupported and stop transmission. Automatic resume is not implemented.
- PIN pairing, peer command rejection, and API failure produce explicit errors and stop transmission. SDK A2DP COMMAND_REJECTED does not expose the individual AVDTP error byte; retain command/cid/seid and a local diagnostic status without fabricating the peer's error value.
- Retain raw event type/subevent/command identifiers for unknown notifications, but never keys or authentication data.
- Process reentrant events after committing the current transition. Queue overflow stops transmission and reports a lost-event count.
- History stores previous/next State, Event, generated Actions, and sequence. Ticks that merely advance time must not evict history. High-rate transmission history can be overwritten in bounded storage, so observe overwrite counts and transmission totals separately.
