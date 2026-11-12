# A2DP Source: incoming connections and stream controls

English | [日本語](a2dp_source_lifecycle_jp.md)

This extends the existing [A2dpSourcePlayer API](api_reference.md) for the SBC PCM path. It does **not** complete every A2DP SDK operation or establish same-peer Source/Sink simultaneous media. Host/ARM checks below are not hardware interoperability results.

## Operations

| API | Accepted state and effect |
|---|---|
| `listen(true)` | After `begin()` reaches Working, with no inquiry or outgoing connection in progress. Admits the configured peer's incoming signaling connection and enables shared radio connectability. It does not enable discoverability. |
| `listen(false)` | Disables new incoming admission. It does not close the current connection or disable shared connectability needed by other profiles. |
| `pause()` | Playing: stops local media production and requests SDK SUSPEND. Open: cancels automatic start without a wire SUSPEND. |
| `resume()` | Open or Suspended: requests SDK START. |
| `reconfigureSamplingFrequency(rate)` | Open or Suspended, with explicit variable-rate support: requests 44,100 or 48,000 Hz. Reconfiguration never resumes automatically. |
| `disconnect()` | Disables incoming admission and closes this signaling session. Cleanup also remains available after a terminal profile error. |

Control methods return `true` for acceptance, **not remote completion**. Inspect `snapshot().stream`, `error`, `status`, and history. The new stream values `SuspendRequested`, `Suspended`, and `ReconfigureRequested` are appended, preserving the previous enum values. A nonzero immediate SDK result restores the prior stable stream state; remote START/SUSPEND rejection is terminal because the installed SDK also changes its configuration state. A rejected reconfiguration preserves the previous format and leaves the stream suspended. No automatic retry is added.

`Settings.control_timeout_us` defaults to 10 seconds and must be nonzero. A missing locally requested START/SUSPEND/RECONFIGURE completion fails closed and requests disconnection. Incoming connection and configuration do not gain a new connection-wide timeout in this change.

## Default behavior and application setup

`target_address` must be a nonzero Bluetooth address; `00:00:00:00:00:00` is not a wildcard and returns `InvalidSettings`.

Existing applications retain fixed-rate SBC and automatic stream start by default. Set `auto_connect=false` before explicitly listening; otherwise startup may already be doing discovery. Set `auto_start=false` to leave an opened stream idle until `resume()` or a remote START. `start_delay_us` applies to the local automatic start, not a peer's accepted START.

To allow a rate change, set `allow_sample_rate_change=true` and provide `configure_source`. Without that callback, `begin()` returns `InvalidSettings` for variable-rate mode. The callback is invoked on initial accepted configuration and later accepted format changes. It must arrange for the source to supply PCM at that rate, return false if it cannot, and must not reenter the player. There is no resampler.

```cpp
static bool configure_silence(uint32_t rate, void *) {
    return rate == 44100 || rate == 48000;
}
// A silence generator can accept either rate without changing sample values.
// A waveform, decoder or capture source must actually change its PCM production.
A2dpSourcePlayer::Settings settings;
settings.target_address = "12:34:56:78:9A:BC"; // Your permitted receiver.
settings.auto_connect = false;
settings.auto_start = false;
settings.allow_sample_rate_change = true;
settings.configure_source = configure_silence;
// Construct A2dpSourcePlayer with your IAudioSource, then begin() and poll().
```

For a local format change: wait for Open/Suspended, request reconfiguration, wait for `stream == Suspended`, `error == None`, `status == 0`, and the requested `codec.rate`, then call `resume()`. Do not assume that a `true` return means the PCM format has already changed. The Source callback and encoder are updated only after the successful reconfiguration event sequence.

Pause discards buffered PCM and encoded media. Resume retains the transport RTP sequence/timestamp counters and rebases scheduling to the new start time, rather than catching up the paused wall time. `play_duration_us` bounds each successfully started segment, so a resume begins a new duration interval. Packet/sample totals remain cumulative.

## Incoming and remote behavior

Incoming admission checks the fixed peer, CID, local SEID where present, and the current runtime ACL handle/epoch. Replacing that admitted ACL cannot silently revive the stream. Disabling listening does not retroactively withdraw an existing connection; use `disconnect()` for that. These checks identify the runtime link and are not an additional authentication protocol.

The SBC configuration validator rejects unsupported modes/rates before accepting remote configuration. The supported path remains stereo, 16 blocks, 8 subbands, loudness allocation, and bitpool 2–53. With variable-rate mode off, the configured initial sample rate is the only accepted rate. Remote START and SUSPEND update state without treating an ordinary suspension as an unsupported-operation error. Remote reconfiguration is applied only while Open/Suspended. STOP/RELEASE clear local media credit while retaining the format for OPEN on the same configured endpoint. A later configuration may replace it while Idle; signaling release clears it. Delay reports are available as `snapshot().delay_100us` and `delay_reports`; one unit is 100 microseconds.

## Media-payload adaptation and bounded fragmentation

The [earlier complete-frame checkpoint](source_mtu_batching_contract.md) selects one to four frames per packet. The current [bounded Source SBC fragmentation](source_sbc_fragmentation_contract.md) also sends one supported frame as 2–15 fragments when no whole frame fits. Physical packet counters advance per accepted fragment; samples, pacing and completed media advance only on the final fragment. Busy retains the same encoded bytes. Pause, reconfiguration and disconnect discard incomplete batches. Codec restrictions remain unchanged. This is a bounded transport contract, not hardware interoperability or support for arbitrary codec formats.

## Verification and boundaries


On the installed Pico SDK 2.1.1-series environment:

For included checks, see the [build guide](build_guide.md#compile-only).

The lifecycle suite uses real SDK headers and mocked SDK operations, with ASan/UBSan. It covers zero-address rejection, incoming admission/identity, inquiry-phase rejection before SDK configuration, duplicate and malformed events, remote/local suspend/start, successful/rejected reconfiguration, immediate errors, timeout, stale ACLs, stale credits/CIDs, STOP, ordering, and clock bounds. Existing ACL and real SBC encoder/transport/pacing regressions are separate checks. A build-only Pico W application calling all new APIs produced an ELF and UF2; the known newlib `_getentropy` warning remains.

No new firmware was flashed or radio interaction performed for this change. Real peer acceptance of incoming Source, suspend/resume and rate reconfiguration remains to be checked. Source and Sink can now register together for **distinct fixed peers**, with at least two AVDTP connections and endpoints in `btstack_config.h`. A shared raw-AVDTP shim admits only the role's fixed peer before the SDK configuration process, not just after an application notification. Unknown peers and successful connections not currently admitted are closed without starting endpoint discovery. Initial admission rejects the inquiry phase and validates the current incoming ACL. A separate predicate allows queued discovery only for a still-active, admitted connection. Otherwise the SDK can start unwanted endpoint discovery even when the application ignores that notification. Local disconnect also checks other-role CID ownership. A separate host regression links the actual SDK `a2dp.c` and verifies role-local configuration-state changes, SUSPEND/release routing, rejected unknown/unadmitted peers, queued discovery after release or configuration completion, cancelled-queue suppression, and a bypass negative control; lower transport/timers/lookup are mocked, and full codec negotiation is not exercised there.

Same-peer dual media remains rejected at registration: the pinned SDK uses one connection-wide `configuration_state` and refuses a second SET_CONFIGURATION even for another endpoint. Same-peer switching without reset and two simultaneous opposite-direction streams remain open work; do not call distinct-peer registration proof of those behaviors. Arbitrary codec configuration, non-SBC raw media, multiple endpoints, caller-owned SEP lifecycle and general MTU adaptation are not supplied by this extension. Do not label this subset as full SDK coverage.
