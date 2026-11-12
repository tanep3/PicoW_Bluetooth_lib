# AVRCP examples and repeatable local checks

[日本語](avrcp_examples_jp.md) | [Validation scope](validation.md)

These examples distinguish correct application ordering from link coverage. None of their build results is hardware acceptance. Do not initialize SDK profiles twice or combine these owners with the legacy AVRCP plugin.

## Roles and validation scope

A2DP Sink receives audio; AVRCP Controller sends commands to the source player. Target receives commands in the opposite direction. The public media-control console implements `PLAY` / `PAUSE`. Its hardware validation remains open; results from a different test application do not qualify it. See [Validation scope](validation.md) for trial details.

## Basic media control in the opposite direction

[AvrcpMediaControlSample](../examples/AvrcpMediaControlSample/README.md) combines A2DP Sink and AVRCP Controller to send explicit Play/Pause to a source player. It separates local admission, both remote responses, PlayStatus and human UI observation. Radio/pairing default OFF; the enabled console still waits for START. The public example has host/build checks; its own hardware validation remains open. Existing Target examples receive commands in the other direction; no automatic forwarding or dual initialization is implied.

## Application pattern

[AvrcpMainLoopSample](../examples/AvrcpMainLoopSample/README.md) demonstrates one main loop, process-lifetime shared owners, copied bounded work between callbacks and the main loop, explicit error handling and request/response ordering. The shipped default does not call begin or start Bluetooth. Follow its README before opting into a real program; configuring/compiling the sample is not permission to flash or operate devices.

A synchronous callback is not a place to issue another Bluetooth command. Copy a request token or data into owned bounded storage and return; execute the operation later. Queue overflow must be handled explicitly, not by retaining the borrowed pointer. If stopping or timing out, keep the owner/storage alive while transport cleanup completes. A successful local command return means admitted work, not a remote acknowledgment.

## Start your first application

1. Build the complete [main-loop project](../examples/AvrcpMainLoopSample/CMakeLists.txt) with its default radio-off setting, following its README. Read [the API reference](api_reference.md) for role ownership and method contracts, then the [Controller](avrcp_controller.md), [Target](avrcp_target_features.md), [Browsing](avrcp_browsing.md), or [Cover Art](avrcp_cover_art.md) guide for the roles you need.
2. Keep the library directory layout when vendoring it. Copying only `main.cpp` or the sample directory is insufficient: it uses `OwnedRecordQueue.h`, generated `sample_config.h`, and a relative include of `examples/AvrcpCoverArtCompileProbe/btstack_config.h`. For a separate application, provide its own configuration header and settings, and adapt the sample CMake helper calls to that location. Do not copy the wrapper `btstack_config.h` to an unrelated directory unchanged.
3. Separate synchronous decisions from deferred events. A Target decision must return a truthful result during its callback; it cannot wait for the main loop. Browsing request tokens and copied events can be queued for later handling. Retaining a token does not retain borrowed request bytes, and copying a struct does not make its pointer fields owned. The sample's queue header is a bounded storage utility, not a complete application event policy.
4. Replace the sample's rejecting Target/catalogue policy with application-owned playback state and bounded work admission before calling it a player. Decode and correlate remote responses instead of treating local `0` returns or received chunks as success. Supply pairing/bond policy, queue-overflow handling, deadlines and terminal cleanup. Before flashing, configure the intended peer and pairing policy and review the example's runtime limits.

The [Validation scope](validation.md) distinguishes implementation, host-only evidence and limited hardware observations. This entry guide does not change those acceptance statuses.

## Link coverage only

- [AvrcpBrowsingCompileProbe](../examples/AvrcpBrowsingCompileProbe/README.md): reachable common Browsing and both role APIs.
- [AvrcpCoverArtCompileProbe](../examples/AvrcpCoverArtCompileProbe/README.md): shared signalling roles, Browsing, Cover Art, cancellation and timeout API linkage.

Both probes return without radio initialization by default. Their sequential API calls are intentionally not a connected runtime workflow. In particular, issuing connect does not make a binding immediately usable; a pending connection cannot be followed immediately by GET and treated as a successful example.

## Host functional regressions

These developer regressions require the full repository checkout and its test sources. A selected source archive may omit the runners; do not treat a missing runner as a library compile failure. Consult the archive’s included-file manifest and validation scope. The application sample and its build instructions are a separate path.

From the full repository root:

For included checks, see the [build guide](build_guide.md#compile-only).

Change the SDK path to your installed supported SDK. The runner executes thirteen named suites serially and stops at the first failure. It performs no SSH, flashing, radio operations or recording. Most binaries use a temporary build directory; the core suite uses `.work/avrcp/host`. ASan/UBSan are enabled by the component runners. The generated overlays fail closed on unknown source hashes and never modify the SDK installation.

- Core/attribute and facade suites test bounded application contracts. Facade transport effects are mocked even when the real SDK headers are used.
- Controller/Target/Cover Art overlay suites exercise actual generated SDK parsers/serializers against modeled transport boundaries.
- Browsing wire suites test framing and production SDK command serialization within their documented limits.
- Common overlay tests exercise pending cancellation and actual shared-helper/role-serializer composition. The exhaustive MTU/length matrix is slower than a quick header build.

- Dedicated metadata-continuation tests exercise actual Target/common overlays with mocked L2CAP, long attribute values, byte-exact reconstruction, abort/restart and queued notifications.

There is no new independent-audit claim in this runner. The previously interrupted independent review remains incomplete. See each evidence checkpoint for its exact tested source hashes and limits; a local runner PASS does not close the hardware acceptance rows.

## Device acceptance remains separate

Build/link, host function tests, and observed remote device behavior are separate results. Use the [Validation scope](validation.md) for roles, connection directions, packet/application outcomes, loss/reconnect and coexistence. Obtain any required device/flash approvals, always obtain the required per-use SSH approval, and announce before recording. No example silently enables pairing, makes the device globally discoverable, records audio or uploads received bytes.

The public `AvrcpMediaControlSample` media commands are `PLAY` / `PAUSE` only.
