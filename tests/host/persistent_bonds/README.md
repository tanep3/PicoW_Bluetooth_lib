# Persistent Classic backend tests

Run from the repository root, with a dedicated writable temporary directory:

```sh
TMPDIR=/your/task/tmp PICO_SDK_PATH=/path/to/pico-sdk \
  bash tests/host/run_persistent_bond_tests.sh
```

Only installed host compilers and the SDK's unchanged upstream TLV/flash-bank
source are used. No hardware, flash device, radio, network or real keys are read.
Build products are retained under TMPDIR. ASan/UBSan and compiler warnings are
fatal. The format tests use a fake TLV with the real flash implementation's
copied-length read semantics; journal tests use the real upstream implementation.

The suite deliberately prints **LOSS** observations from logical prefix faults.
Successful process exit means the implementation assertions and documented fault
expectations completed, not that storage is durable. Current-view silent write
and delete failures are tested, along with a migration-header silent failure
which readback cannot detect before reconstruction. Prefix fault cases are not
a physical Pico flash model. The modeled faults can lose preexisting records; successful test exit is not a durability guarantee.
