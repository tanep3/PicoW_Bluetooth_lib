# AVRCP main-loop use pattern

[日本語](README_jp.md)

This is a small integration example, not a player, an acceptance test, or a claim
that every AVRCP function works on hardware. The normal firmware returns from
`main()` without constructing owners, calling `begin()`, or starting the radio.
The radio path remains C++ type-checked in that build; linker garbage collection
may discard it. No audio, media database, image decoding, pairing UI, persistent
bond store, or automatic Cover Art request is implemented.

## Build independently

From the repository root, with the real Pico SDK installed:

```sh
PICO_SDK_PATH=/path/to/pico-sdk cmake \
  -S examples/AvrcpMainLoopSample \
  -B build/build-avrcp-mainloop-sample \
  -DAVRCP_SAMPLE_ENABLE_RADIO=OFF
cmake --build build/build-avrcp-mainloop-sample -j2
```

Outputs are `avrcp_mainloop_sample.elf`, `.bin`, and `.uf2` in that build directory.
The independent project uses the repository's Browsing/Cover Art CMake helpers,
which pull in the Controller/Target runtime and reviewed SDK overlays. It does
not initialize A2DP or SPP. `btstack_config.h` intentionally includes the existing
combined configuration in `../AvrcpCoverArtCompileProbe/`; this sample therefore
needs the repository, not just a copy of this directory. Overlay generation stays
in the build directory and does not edit the installed SDK.

## Read the sequence in main.cpp

1. One static `Application` owns Target, Controller, shared Browsing
   (Controller + Target), and Cover Art Client. It is created only on the enabled
   path. All owners use the same explicit peer allowlist. Never create replacement
   owners, call `begin()` again after failure/stop, or mix in the legacy AVRCP plugin.
2. Handlers are registered first. Target rejects player-control requests and
   reports unknown playback facts. `target.begin()` may already power the radio;
   startup is not deferred until `connect()`.
3. The main loop waits for `HCI_STATE_WORKING`, then makes one signalling-connect
   attempt. `0` is only local admission. Nonzero busy/error results are logged;
   this example does not retry automatically or infer connection success.
4. A copied Controller `Connected` event is consumed in the main loop, where
   PlayStatus is requested. Browsing connect waits until copied query events have
   drained and the same full signalling binding is still live. A copied, live
   Browsing `Connected` event triggers one media-player-list query. Complete bindings include CID,
   peer, epoch and generation; stale bindings can fail safely after disconnect.
5. Browsing request callbacks copy a `RequestToken`. The main loop calls
   `reject(token, 0x00)` (AVRCP INVALID_COMMAND), because no catalogue exists.
   Tokens that expire/disconnect before consumption are not retried. A real
   catalogue must copy any needed request bytes before the callback returns.
6. A single-thread, eight-entry queue copies at most 512 payload bytes per entry.
   No callback calls profile mutators, prints, performs I/O, or holds borrowed
   pointers. Queue mutation is the intended callback work. There is no silent
   truncation: oversize/full queue sets a fatal flag; the main loop shuts down.
   Cover Art's data callback additionally returns `false`, requesting deferred
   cancel, not pause. This intentionally small sink cannot accept every valid
   large chunk/list; resize within a measured RAM budget or implement a bounded
   streaming sink. Do not add another core, IRQ producer or background poller
   without replacing the queue's concurrency model.
7. `use_deadlines` selects timed poll calls and absolute budgets. Milliseconds
   are monotonic modulo 2^32; poll intervals must stay below 2^31 ms. The first
   timed poll after admission arms the budget, and progress does not renew it.
   Untimed `poll()` does not advance deadlines. A separate 60-second application
   lifetime remains enforced even if optional profile deadlines are disabled.
8. Shutdown demonstrates Cover Art pending/live cancellation and pending shared
   signalling cancellation. Their return values are local results, not evidence
   of remote acknowledgement or completed cleanup. `stop()` is terminal and
   does not power off the shared radio. Only this standalone program then calls
   runtime `powerOff()`; remove that policy when other profiles share the radio.

## Local status is not remote success

- Controller query return `0`: request accepted locally. The copied SDK packet
  still needs matching subevent/ctype decoding and length checks. This example
  only logs its size and does **not** claim PlayStatus success.
- Browsing `Response` event: `Event.status == 0` is not the remote AVRCP status.
  The copied payload's first byte must equal `0x04` for remote success. Other
  values are reported as remote failure. `TargetResponseSent` is a send outcome,
  not proof that the remote application acted.
- Cover Art chunks are partial data, never successful object completion. An
  integration must correlate binding/request and offsets and wait for `Complete`
  with successful status; an `Error`, cancellation, size limit or sink rejection
  invalidates incomplete output. The sample now includes a bounded RAM assembler (below), but does not
  display/save/decode an image.

## Integrate deliberately before any radio test

`AVRCP_SAMPLE_ENABLE_RADIO=ON` enables real radio startup and requires a nonzero
`AVRCP_SAMPLE_PEER=AA:BB:CC:DD:EE:FF` chosen for an authorized test peer. That
address is a syntax example, not a real configured peer. Set your actual peer address before flashing an enabled image. Changing a CMake
option is not proof of hardware acceptance.

There is deliberately no automatic pairing confirmation, key import, pairing UI
or discoverability change. Provide a reviewed pairing/bond policy and peer setup
before expecting an authenticated connection. The RAM-only default database does
not persist bonds across reboot. Local connect success can still lead to an
asynchronous connection failure; inspect status and terminal events in a product.

Cover Art is a **client only**. The explicit operator-driven flow below now
connects it and dispatches one selected GET. No production handle is built into
the program, and enabling the radio alone never sends a GET. Learn the real
peer's support and seven-digit handle from actual current metadata before input.

For a player, add truthful state/metadata and bounded synchronous Target decisions
backed by actual application capacity. A synchronous Target decision cannot be
postponed like a Browsing token: only return Accepted when the product can honor
that decision. Audio gain, playback, track selection and settings are application
responsibilities, not side effects of the AVRCP adapter.

Build verification establishes source/build compatibility only. Hardware pairing,
remote responses, cancellation timing, queue pressure and coexistence need their
own tests; this example does not supply those results.

## Offline callback queue test

`OwnedRecordQueue.h` is the sample's SDK-independent, single-loop queue. It copies up to 512 payload bytes into each of eight records. Empty input is permitted; null nonempty input, oversized payloads and a full queue are rejected without changing queued records. The failure flag is sticky so the main loop stops rather than silently continuing after loss. Metadata copying does not make pointers owned: callbacks must still clear borrowed pointer fields before enqueueing, as this sample does.

Run `bash tests/host/run_avrcp_sample_queue_tests.sh`. The exact queue header is tested with ASan/UBSan for borrowed-buffer mutation, empty/null/oversized input, capacity, FIFO ordering, sticky failure and 40,000 reference-queue steps. This is an application utility test, not adapter/SDK protocol review, callback integration proof or hardware acceptance.


## Explicit one-shot Cover Art client flow

`CoverArtFlow.h` is the SDK-independent application flow used by `main.cpp`, not
an uncalled hook or a force-reference probe. To enable this flow,
configure both `AVRCP_SAMPLE_ENABLE_RADIO=ON` and
`AVRCP_SAMPLE_ENABLE_ART_INPUT=ON`, plus your authorized `AVRCP_SAMPLE_PEER`.
Both switches default OFF; input ON with radio OFF is rejected at configuration.
This change adds no pairing policy, metadata discovery or persistent bonds.

After the `Cover Art ready` prompt, send one newline-terminated command through
the sample's USB stdio console. Replace the placeholder with the real handle
learned elsewhere from this peer's current metadata:

```text
art thumbnail <REAL_SEVEN_DIGIT_HANDLE>
art image <REAL_SEVEN_DIGIT_HANDLE>
art properties <REAL_SEVEN_DIGIT_HANDLE>
cancel
```

Choose **one** GET command, not all three. Commands are case-sensitive and use
one space between fields. The handle must be exactly seven ASCII digits. Syntax
validation cannot establish its provenance, freshness, peer support or content
validity. `image` requests the original image with no descriptor. `properties`
returns opaque properties bytes, not a parsed XML document. There is no fake
player/database, decoder, image server, file write or upload.

The parser owns a 32-byte line buffer, rejects an entire overlong or malformed
line and resumes at the next newline. CR is accepted only directly before LF;
embedded/repeated CR, other control bytes, extra spaces and invalid lengths are
rejected without splicing text. It is not an editing terminal: backspace is
invalid. Main loop reads at most 16 characters using nonblocking input each
iteration. USB stdout timeout is zero so a slow/missing terminal cannot hold the
poll loop waiting for log capacity; diagnostics may be dropped. Hardware timing
and throughput have not been measured.

### Sequence and session ownership

1. The first copied Controller `Connected` must match its current full live
   binding. One PlayStatus query is requested. Any events it produces are
   consumed before one Browsing connect is permitted; session/loss checks run
   again immediately before that attempt. A local failure is not retried.
2. A copied Browsing `Connected` must match its live peer, ACL epoch, generation
   and CID. Waiting for Browsing readiness is this **combined sample's policy**
   to serialize shared SDP startup, not an inherent Cover Art protocol
   requirement. A peer without a successful Browsing connection cannot reach
   the Cover Art prompt in this sample. Other SDP users may still cause a local
   busy rejection; the sample never blindly retries it.
3. A command is accepted only in Ready with a healthy empty application queue
   and no existing Cover Art binding. Readiness is captured at the first consumed
   command character and checked again at line completion. The owned command
   cannot be replaced by duplicate input. The first full signalling session is
   fixed for the flow's entire lifetime: loss, a changed epoch/generation/CID or
   a stale event is terminal, even before a command. Buffered/partial commands
   are never retagged onto a later connection. This does **not** prove when a
   host/USB backlog was originally typed; the operator must supply fresh input.
4. The main loop calls `cover.connect(peer)` once. Local zero means admission,
   not Connected. No GET follows merely because a live binding exists: the
   matching copied Cover Art `Connected` must first be consumed. Its full peer,
   ACL epoch, Cover generation/CID and signalling CID must match live state and
   the captured signalling session. Queued events must then drain.
5. The owned 16 KiB `CoverArtObject` is armed **before** exactly one selected
   `thumbnail`, `image` or `properties` API call. GET zero is admission, **not a
   request ID**. No other code issues Cover Art GETs; this flow has no reset,
   reconnect or retry. An empty queue alone would not prove an independent
   transport has no old GET, so do not add another command owner.
6. The first nonempty offset-zero Data establishes the request ID. Subsequent
   Data and successful Complete must match all binding fields, operation and
   request. Offsets must equal received bytes; Complete status must be zero and
   count must match exactly. Request zero, empty chunks/objects, null nonempty
   input, overflow, mismatched IDs, out-of-order/duplicate events and Complete
   without Data fail closed. Bytes are exposed only after successful Complete,
   and are still unvalidated image/XML content.

### Bounds, failure and cancellation

The object owns 16 KiB, separate from eight queue records with up to 512 payload
bytes each and all owner/record metadata. The adapter also limits an object to
16 KiB. Valid large objects or chunks can be rejected. Callback bodies only copy
bounded records, clear borrowed pointers and return; they never call a profile
mutator. Main loop owns assembly, connect, GET and cancellation.

Local rejection, any relevant lost/changed live session, queued Error or
Disconnected, malformed transfer, queue loss, cancel and stop discard/zero
object storage. The deliberately conservative policy also invalidates completed
bytes on later errors or disconnect. Object views expire on mutation; no failed
or late event can resurrect output. Failure is terminal and duplicate commands
cannot start a second operation.

The flow imposes an absolute 15-second connect/Connected-drain budget and
30-second GET budget from each local attempt. Profile deadlines remain enabled
as well; data does not renew either budget. Monotonic uint32 millisecond
subtraction handles wraparound; polling gaps must be below 2^31 ms. The separate
60-second application lifetime may end an operation sooner. A copied completion
still waiting in the queue at expiry is conservatively rejected.

`cancel` is accepted even before readiness and terminates the flow. After a
Cover Art connection attempt, the flow requests `cancelPeer` at most once,
including on local rejection or failed explicit cancellation. Its status is
recorded and is not proof of remote release. Repeated cancel/stop/events do not
retry that application request. `cover.stop()` separately retains the facade's
documented deferred terminal cleanup; an admitted cancellation is not a release
guarantee. The standalone program stops owners and powers off at shutdown as
before; that policy must not be copied into a program sharing A2DP/SPP.

### Focused offline validation

Run `bash tests/host/run_avrcp_sample_art_flow_tests.sh` for the **actual** helper
and 16 KiB object under ASan/UBSan. It tests the three operation selections,
owned input/bytes, parser/partial-line/CRLF behavior, deferred Browsing startup,
callback-like copied event ordering, complete success, every binding component,
stale events/sessions, admission failure, duplicate input, queue loss, cancel,
stop and deadline boundaries/wraparound. IO is modeled, not an SDK/OBEX peer.
Existing queue and object helper tests remain separate regression tests.

Hardware execution of this example is **NOT_RUN**. Pairing, peer responses, USB input, cancellation timing and coexistence remain unverified. See the [Validation scope](../../docs/validation.md).

## Troubleshooting

The default build exiting immediately is expected. If an enabled build never prints `Cover Art ready`, inspect signalling and Browsing connection outcomes; this sample cannot become ready with a peer lacking Browsing. For rejected input, check both CMake switches, the seven-digit handle, spaces and line ending. Input after failure/cancel does not restart the flow. Image display and file saving are not implemented.
