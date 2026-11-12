# Bluetooth Classic bond storage

[日本語](bluetooth_bond_storage_jp.md)

Persistence is opt-in. Read the [committed snapshot TLV](bluetooth_committed_tlv.md) provisioning/read-only contract before enabling it. Link keys use RAM by default. Original SDK fault-test history is retained in [Validation scope](validation.md).

## Runtime contract

`BluetoothRuntime::setLinkKeyDatabase(const btstack_link_key_db_t *database)`
selects the **shared Bluetooth Classic** link-key backend used by A2DP Source,
A2DP Sink, SPP, and AVRCP. It does not configure a BLE device database.

- Call before any profile's `begin()` / successful runtime `claim()`, and before
  `initialize()`. All profiles use the same radio and database.
- Omitting the call, or passing `nullptr`, selects BTstack's existing RAM
  backend. This is not persistent across MCU reset.
- A valid custom backend can replace an earlier selection until the first
  successful claim or initialization attempt. A rejected call returns `false`
  without changing the selection or calling the backend.
- Selection is locked during initialization, after initialization failure, and
  for the runtime's remaining lifetime, including radio OFF/ON cycles. A
  foreign initialized CYW43 instance is also rejected.
- The caller owns the backend. Its immutable function table and backing state
  must remain valid for the runtime's lifetime. The runtime does not allocate,
  free, erase, migrate, or enumerate its storage.
- `open`, `set_local_bd_addr`, `close`, `get_link_key`, `put_link_key`, and
  `delete_link_key` must all be non-null. Even a fixed-controller backend needs
  a no-op `set_local_bd_addr`, because the SDK invokes it without a null check.
- Iterator callbacks must be either all present or all absent. Without them,
  BTstack's normal iterator initialization reports unavailable and
  `gap_delete_all_link_keys()` cannot enumerate/delete all bonds.
- Selection only validates the callback shape. It cannot establish storage
  readiness, capacity, integrity, encryption, or durability. BTstack's `open`
  and `put_link_key` callbacks return `void`; runtime initialization success is
  not a successful-persistence acknowledgement.
- Use the normal single-main-loop ownership model. Do not replace the database
  directly with `hci_set_link_key_db()` after runtime initialization.

Application startup, with a backend defined by the application:

```cpp
#include "BluetoothRuntime.h"

extern const btstack_link_key_db_t application_link_key_db;

bool configure_bonds_before_starting_profiles() {
    return BluetoothRuntime::instance().setLinkKeyDatabase(
        &application_link_key_db);
}
```

Check the return before starting a profile. No existing example opts into
persistent storage automatically.

## Application-provided TLV storage and initialization order

BTstack offers `btstack_link_key_db_tlv_get_instance(tlv, context)`. The
application supplies a long-lived `btstack_tlv_t` implementation and context,
with `get_tag`, `store_tag`, and `delete_tag`. The application's build must
provide a suitable `NVM_NUM_LINK_KEYS`; records use the upstream `BTL` tag
namespace and its finite-capacity eviction policy. Reserve that namespace and
plan capacity alongside other records. The SDK stores a native record layout,
so firmware/ABI changes require an explicit compatible-format or migration
policy. Validate stored records before returning them: the current SDK treats
any nonzero `get_tag` result as a usable link-key record, rather than validating
its exact size.

**Do not pre-bind the upstream TLV singleton and pass its pointer directly to
`setLinkKeyDatabase()` expecting that binding to survive Pico initialization.**
In the reviewed Pico SDK, `cyw43_arch_init()` invokes `btstack_cyw43_init()` and
`setup_tlv()`. That configures the same upstream singleton before this library
installs the selected backend.

Instead, supply a caller-owned forwarding `btstack_link_key_db_t`:

1. Its `open()` binds `btstack_link_key_db_tlv_get_instance(application_tlv,
   application_context)` **after** CYW43 initialization, then opens that
   delegate.
2. All other callbacks forward to the bound delegate.
3. Keep the application TLV table, context, delegate wrapper, and storage valid
   for the process lifetime. Do not let another component rebind this singleton.
4. Prepare and validate the storage using the application's platform-specific
   lifecycle. Do not start another profile or recursively initialize the
   runtime from a database callback.

The complete forwarding pattern is exercised by
`test_selection.cpp`, using
only an in-memory fake TLV store and synthetic records. It is an integration
pattern, not a production flash driver.

## Pico SDK flash warning

**Selecting RAM, or a custom backend, does not make SDK startup flash-free.**
The reviewed SDK initializes its own flash-bank TLV before this library's
backend selection takes effect. Startup can read existing flash, write headers,
erase an uninitialized bank, or recover/migrate records. `powerOff()` does not
undo that work and does not erase saved bonds.

The SDK flash implementation defaults to two sectors at the end of flash,
configured by `PICO_FLASH_BANK_STORAGE_OFFSET` and
`PICO_FLASH_BANK_TOTAL_SIZE`. The library does not choose, reserve, or change
those values. Applications must review the actual SDK version and reserve
non-overlapping storage against firmware, filesystem, OTA slots, and all other
flash users. If **no flash access at all** is required, an application-controlled
SDK/port initialization path is needed; this setter alone cannot provide it.

For persistent deployments the application must also define erase/reset
consent, failed-write reporting, power-loss recovery, wear handling, and safe
flash execution across interrupts/cores. Neither raw TLV nor this wrapper
provides encryption or access control. Treat link keys as credentials: never
print them, include them in packet/log dumps, or commit real storage images.
The selection implementation adds no key logging. Audit the application's
existing HCI dump settings separately.

Reviewed source paths, relative to the Pico SDK:

- `src/rp2_common/pico_cyw43_driver/btstack_cyw43.c`
- `src/rp2_common/pico_btstack/btstack_flash_bank.c`
- `src/rp2_common/pico_btstack/include/pico/btstack_flash_bank.h`
- `lib/btstack/platform/embedded/btstack_tlv_flash_bank.c`
- `lib/btstack/src/classic/btstack_link_key_db{,_memory,_tlv}.h` and implementations
- `lib/btstack/src/hci.c`

## Verification and limits

Run:

For included checks, see the [build guide](build_guide.md#compile-only).

The host suite uses the real SDK declarations and upstream TLV implementation,
with ASan/UBSan. It checks RAM default/reset, callback validation, optional
iterators, all profile claim locks, initialization failure/foreign ownership,
open-time reentrancy, retained selection across radio power cycles, late TLV
binding, synthetic store/read/delete/iteration, and backend reconstruction with
the same fake store. The ARM check compiles the actual runtime using the
Pico/BTstack flags from an existing CMake compilation database.

These checks do not flash a device, read or save real bonds, test actual
persistent media, prove power-loss safety, or demonstrate reconnect after an
MCU reboot. Those remain application and hardware validation tasks.

## Opt-in SDK-backed implementation

[`PicoPersistentBonds`](../bluetooth-runtime/PicoPersistentBonds.h) is a concrete
optional backend, integrated by
[`picow_enable_persistent_bonds`](../cmake/picow_persistent_bonds.cmake).
See the compile/link and application integration example.
The existing runtime remains RAM by default. The optional source must be added
explicitly and its table selected before any profile begins.

`open()` fetches the SDK's already initialized global TLV; it neither creates a
second flash journal nor rebinds the upstream link-key singleton. It remains
unready until `set_local_bd_addr()` supplies the controller address. A change
of controller within an open lifecycle fails closed until close/open. All
iterators are invalid after close/open; restart them. Calls belong to the
single owning main loop, never an interrupt, second core or parallel worker.

The backend reserves **all eight tags 0x50424b00 through 0x50424b07**, separately
from SDK BTL/BTD tags. No other component may use this range or replace the
SDK global TLV while the backend is open. It does not import native BTL records
or automatically migrate future/unknown formats. Native Classic, BLE and application tags share capacity in the new snapshot
format. Legacy journal images are blocked and preserved, never auto-imported.

Each slot stores 48 serialized bytes: `PBK`, version 1, little-endian uint64
sequence, local controller address, peer address, 16-byte key, key type, slot
number, two reserved zero bytes, and little-endian CRC-32/IEEE over the first
44 bytes. The sequence is at offset 4; addresses at 12 and 18; key at 24; type
at 40; slot at 41; CRC at 44. Serialization is ABI-independent. Exact size is
queried before copying because the SDK TLV read returns the **copied** length.
CRC detects accidental record corruption; it is not authentication, encryption,
anti-rollback protection, or a validator for the underlying TLV journal headers.

Capacity is eight records for one local controller. New records use the lowest
empty slot, updates reuse the peer's slot, and a full valid database evicts the
lowest sequence (least recently stored), with lowest slot breaking ties.
Reads do not refresh age. Sequence exhaustion refuses writes. Any malformed,
unknown-version or foreign-controller record encountered by a put blocks the
whole put without changing storage. Foreign records are never automatically
evicted. Reads/iteration return only valid current-controller records. Deletion
removes matching valid records only, verifying current absence. It cannot
securely erase old flash copies or remove malformed/foreign records. Whole-bank
reset/migration is intentionally not supplied: it could destroy other TLV users'
data and needs an application-owned, authorized recovery procedure.

`status().ready` means callbacks and local address are known and, with the
protected provider, that its state is READY. Mixed/unknown read-only snapshots
are available only to an explicit recovery reader, never normal authentication.
READY is **not** physical durability or overall media health. Sticky error flags distinguish unavailable
storage, invalid records, foreign controller, exhausted sequence, invalid type,
write/readback failure and delete/readback failure. Counters record verified
current-view mutations only. They persist across close/open within the process;
`clearErrors()` does not repair data, reset counters or restore readiness.
BTstack callbacks return void, so applications must poll status; pairing success
is not a persistence acknowledgement. A backend error does not automatically
power the radio off or invalidate the peer's bond.

The CMake helper requires explicit offset/total geometry, compile-checks actual
SDK geometry/alignment/flash capacity, and adds an unconditional linker assertion
against `__flash_binary_end`. This checks the linked image even in Release mode.
It cannot validate filesystem/OTA/bootloader ownership or separately built SDK
objects. Applications must ensure the same geometry compiled into all SDK flash
users. No default safe physical address is selected. Protected SDK initialization reads flash **before** this backend opens, but
never mutates on initialization. Without this helper the original SDK startup
can still mutate flash, including when runtime RAM is used.

The original unprotected SDK journal has modeled interrupted-write cases that lose records. Do not confuse those results with the protected format. The tests and limits are retained in [Validation scope](validation.md). Real power loss, wear and reconnect after reboot remain unqualified.

The helper supports the final executable target only, not a static/interface
library. Dynamic `pico_flash_bank_get_storage_offset_func` overrides are rejected;
custom SDK ports or separately precompiled SDK flash objects are unsupported by
these checks and need a separately reviewed layout contract.
