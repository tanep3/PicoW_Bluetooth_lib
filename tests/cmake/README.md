# Configure-only CMake tests

## Public helper source-root scope

Run from the repository root:

```sh
python3 tests/cmake/check_public_helper_scopes.py
```

Requires CMake, a host C/C++ compiler, Make, and Python 3. The runner creates a
unique directory under `.work/` (or `--work-parent`) and keeps its fixtures and
configure logs. It never builds the generated targets or accesses hardware,
the network, or an installed Pico SDK. CMake may compile its ordinary host
compiler-detection probes.

The fixture copies only the CMake helpers into a synthetic library tree. Library
sources, SDK interface targets, SDK sources, the sink linker template, and Python
patchers are stand-ins. This tests source-root lookup, helper composition,
per-consumer source selection, and one-time overlay preparation. It does **not**
validate real SDK patches, firmware, linker layouts, runtime behavior, multiple
library versions, or different boards/SDKs in one build.

The cases cover normal and repeated include/call, sibling application directories,
an include-only sibling, a function-local include followed by parent/sibling-scope
calls, parent include/child call, caller-variable shadowing, and separately
included AVRCP helper composition. Repeated calls use both the same target and
new targets. Unmodified consumers must retain the synthetic original SDK files.

Current coverage is **18 positive and 10 expected-failure cases**. Normal helpers
run eight scope cases together, including the Sink raw-configuration helper.
The same-peer A2DP experiment runs those eight cases together with Source, SPP
and AVRCP, but without the ordinary Sink helper. Two additional cases reverse
that cohort's experiment-first order. The ordinary Sink and experimental
overlays intentionally conflict in one CMake configuration; two expected-failure
cases preserve that production guard in both preparation orders.

Six negative controls remove individual source-root fixes only in disposable
helper copies and require specific source-root or patch-invocation failures.
Two more deliberately route the SPP or experimental generator to a wrong existing
SDK input. Configuration still succeeds, but verification must reject the result.
Stand-in output markers record both generator identity and SDK-relative input
path; output filenames alone would miss this routing error. Exact selected
sources and the complete one-time generator call multiset are checked.

This updates the synthetic fixture after the normal Sink acquired its own
overlay. The earlier eight-positive/four-negative result remains evidence for
its older helper revision, not a passing test of the current tree. These are
ordinary test assertions, not attempts to repeat denied operations. Only fake
SDK inputs and fake patchers are executed; no production protocol parser or
earlier raw-input/exhaustive test is run.

The helpers keep `include_guard(GLOBAL)`. Each definition root is captured once
in a global property and read into the calling function's local scope, including
composed functions whose nested calls cannot export local variables. Existing
include-time root variables remain available. This uses CMake commands supported
by the project's 3.13 minimum and avoids `CMAKE_CURRENT_FUNCTION_LIST_DIR` (3.17).
Verification used CMake 3.28.3; no 3.13 executable was run.

## Existing SPP overlay source-selection test

`check_spp_overlay_scope.sh` is a separate test that uses actual SDK sources and
production patchers. It is not invoked by the synthetic public-helper scope test.
