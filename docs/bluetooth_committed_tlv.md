# Opt-in committed TLV snapshots

Use this optional storage path to persist TLV snapshots. It does **not** repair or migrate legacy journal images. Physical flash reliability and production operation remain unqualified.

## Scope and startup

`picow_enable_persistent_bonds(final_executable, offset, total_size)` now selects
`PicoCommittedTlv` for that executable's **global** TLV, before the original SDK
journal initializer could run. Runtime Classic bond selection remains explicit;
RAM remains the runtime default. A generated, hash-pinned copy of Pico SDK 2.1.1
`btstack_cyw43.c` substitutes only storage setup. Original SDK files are unchanged.
Other executables retain original SDK startup, including its legacy risks.
There is no linker wrapper or dependency on preventing LTO/inlining.

The helper rejects unsupported SDK/version/source hashes, board, source layout,
non-executable targets, dynamic flash offset overrides and incompatible geometry.
Currently only `pico_w`, 256-byte physical pages and 4096..16384-byte equal banks
are supported. Banks must be sector aligned; all SDK flash code is source-built
with the final target's geometry. The Release linker overlap assertion remains.
Geometry does not establish ownership against filesystems, OTA, other firmware
or flash writers. Do not add separately precompiled/custom SDK storage objects.
Do not call the old journal initializer yourself or rebind global TLV.

The TLV is generic, not a bond-only substitute. Any uint32 tag is supported,
with variable nonzero lengths bounded by bank capacity. Existing native BTL/BTD
and application tags can coexist **within this new format**. The Classic adapter
still reserves tags 0x50424b00..0x50424b07 and validates controller-bound 48-byte
records. No legacy BTL/BTD import is provided. BLE protocol behavior is untested.

## Media format and publication

Each bank is a complete snapshot, not an append journal:

- First 32 bytes: magic `PWTLVS01`, little-endian bank size, version 1,
  nonzero uint64 generation, payload length and CRC-32/IEEE. The CRC covers all
  body bytes, representing its own four bytes as zero.
- Payload: repeated little-endian uint32 tag, uint32 length, then value bytes.
  Lengths are checked by subtraction before use; duplicates and zero-length
  records are rejected. Unused body bytes must be 0xff.
- Last 256 bytes: a dedicated commit page, valid only when every byte is zero.
  No valid prior snapshot page is programmed again.

An update/delete copies all retained tags into RAM, erases only a recognized
older valid bank if needed, verifies erased bytes, writes and verifies each body
page, then programs the commit page. Reopening and immediate publication use
exactly the same complete-image validation and generation selection. Deletion
is omission from the new snapshot, not an in-place tombstone. Every update is a
compaction; unrelated tags are copied unchanged. Eight bond entries consume
448 payload bytes; with 4096-byte banks, the total TLV payload budget is 3808
bytes (including each eight-byte entry header). The implementation uses two
body-sized static RAM buffers plus small page buffers, with no allocation.

Two valid banks must have adjacent, unequal generations. Maximum generation
remains explicitly recoverable but cannot be incremented; no modular epoch/wrap selection.
The final representable generation can be published successfully, but the
resulting state is EXHAUSTED and its bytes are available only through recovery.
Capacity failure leaves media unchanged. Zero-length stores delete a tag.
`get_tag` with a null buffer returns length; otherwise it returns copied bytes,
matching BTstack's interface. Tags 0 and 0xffffffff are supported.

## Conservative state and recovery

Initialization/reopening reads only, including on blank storage. It never
formats, migrates, erases or resumes an interrupted operation.

- `BLANK`: both entire banks are 0xff; not writable until explicit provisioning.
- `READY`: a valid snapshot with a blank other bank, or two valid adjacent
  snapshots. Readable and writable.
- `READ_ONLY`: unknown, legacy, future, corrupt or interrupted media is present.
  If exactly one valid snapshot remains, `readable == 1` and that snapshot is
  available **only via explicit `picow_committed_tlv_recovery_get()`**; its bytes
  are untouched. Normal global TLV reads return absent, and the bond adapter
  refuses authentication/iteration. The other unknown/future bank may contain
  a newer authoritative bond set: validating an older snapshot is not proof
  that its keys remain authorized. Without any valid snapshot, `readable == 0`
  and even recovery exposes no tags. **This does not decode legacy data.**
- `AMBIGUOUS`: two valid equal/nonadjacent generations; neither is exposed.
- `EXHAUSTED`: latest generation is UINT64_MAX; explicit recovery reads only, no mutation or normal authentication.
- `UNAVAILABLE`: unsupported/missing HAL/geometry.

A torn inactive-bank draft commonly leaves the previous complete snapshot
recoverable but blocks normal reads and later writes. This intentionally trades availability for
preserving unknown bytes. No erase/reclaim/reformat/recovery API is supplied.
Recovery requires a separately reviewed, explicitly authorized workflow that
preserves required original bytes, establishes destination ownership, and
handles all other TLV users. In-place conversion cannot promise preservation
of both old bank images. Use a separate reviewed destination when conversion
is eventually designed. Already destroyed data cannot be reconstructed here.

`picow_committed_tlv_provision_blank()` is the only provisioning entry point. It
rechecks that **both complete banks** are blank, then writes an empty snapshot
without erasing either bank. It is never called by SDK startup or sample main.
A partly provisioned bank is retained and blocks retries unless no bit changed.

## Application order and failure handling

The sample supplies `preparePersistentBondStorage(bool)` and
`configurePersistentBondsBeforeProfiles()`. The application must first establish
flash ownership, safe execution and any necessary operator authorization.

1. Before runtime/profile initialization, call preflight with `false`. It reads
   and validates media but does not provision. Stop if not READY.
2. For a deliberately new, reviewed, blank region only, the application may
   instead pass `true` after explicit authorization. This is not erase consent
   for unknown/old data, and such bytes will still be refused.
3. The Classic adapter dynamically requires READY on open, reads, iteration
   and status, not merely a cached `readable` flag. Do not feed the explicit
   recovery view back into normal authentication callbacks.
   Configure the bond database and check success. Call
   `BluetoothRuntime::initialize()` and check success, then check TLV READY
   again **before** any profile `begin()` or radio `powerOn()`.
4. Stop normal pairing/startup on blocked persistence. Do not silently switch
   to RAM, manufacture an empty successful store or treat missing legacy tags
   as permission to overwrite them.
5. Poll both `picow_committed_tlv_status()` and `PicoPersistentBonds::status()`
   in the owning main loop. Generic write/capacity counters are sticky until
   explicit reinitialization; bond counters/errors follow their existing API.
   A void delete failure is exposed through the generic write-failure counter,
   without key logs. Failures do not automatically power off the radio.

Single main-loop ownership is required for preflight, startup and all accesses.
Reinitialization invalidates the current instance state; no concurrent readers,
interrupt callers, multicore callers, or external writers are supported.

## Evidence and limits

The storage model has offline fault-injection coverage; it is not execution of actual flash hardware. Normal authentication and explicit recovery access are checked separately. See the development history for test details.

The new engine deliberately issues **aligned full-page writes only**. Therefore
Pico HAL's partial-page neighbor-copy branches are not used: a commit is on a
separate fresh physical page. The host model checks this contract, not execution
of actual SDK flash hardware routines. Readback returning success establishes
only the validated modeled/current readable image. This is not a physical
brownout guarantee. Faults spanning another page/bank, arbitrary data/CRC
collisions, stale/unreliable reads, retention, wear, flash_safe_execute timeout,
interrupt safety, multicore safety and supply-voltage behavior need hardware
validation. The HAL exposes no write/erase result; readable bytes are the only
available verification. Every mutation erases/programs almost a whole bank,
so wear and latency are materially worse than an append journal.

No encryption, authentication, anti-rollback, secure deletion or atomic
multi-bond deletion is provided. Old snapshots retain deleted key material.
No real keys, flash, firmware execution, HCI or radio tests were used.

[Validation scope](validation.md)
