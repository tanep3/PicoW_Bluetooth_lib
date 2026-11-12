# AVRCP target pure core

`AvrcpTargetCore.h` is a bounded, header-only C++17 state machine with no
BTstack, Pico, heap allocation or application button mapping dependency.
Two peers can each own one control channel. The adapter must validate the
peer allowlist and the current shared-runtime ACL before `connect`.

- Supply `Binding {peer, epoch, cid}` to `connect`, retain the returned binding
  (including its assigned `generation`), and use it for operations/disconnect.
- `epoch` identifies the current ACL. `generation` distinguishes AVRCP channel
  incarnations even when a peer reconnects on the same ACL and reuses its CID.
- `operation(binding, operation_id, pressed)` accepts one press followed by one
  release per supported operation. Independent operation IDs and peers retain
  independent held states. Duplicate presses and unmatched releases are rejected
  rather than repeated as application commands.
- `validateOperation` checks without changing state, events or counters.
- Configure the supported 7-bit operation IDs while no peer is connected.
  Defaults are volume up/down, play, stop, pause, rewind, fast forward, forward,
  and backward. Support is an application delivery policy; the adapter/stack
  determines protocol responses.
- Disconnect clears the peer's held states and emits `Disconnected`. Reset
  clears all held states and emits `Reset`. Neither invents release commands;
  applications cancel any held action on these lifecycle events.
- Status counters are cumulative across reset. Generation is also preserved,
  and exhaustion fails closed. The object and its callbacks use one main loop.
  Callbacks may inspect state, but must not recursively mutate the core.

The core does not parse BTstack packets, verify ACL facts, manage radio power,
or issue wire responses. An adapter must not infer a historical generation
from a reused CID: it must resolve SDK events only against its current bound
channel and live ACL. Stale events carrying a retained old binding are rejected.

Core validation used strict compiler warnings and ASan/UBSan checks of malformed inputs, support policy, peer/CID bounds, stale
bindings, two-peer independence, press/release ordering and reset/reconnect.

For included sample builds and runnable checks, see the [build guide](../docs/build_guide.md#compile-only). Those checks do not rerun every feature-specific validation described here.
