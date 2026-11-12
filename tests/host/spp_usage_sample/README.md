# SPP application workflow acceptance

Run `bash tests/host/run_spp_usage_sample_tests.sh` from the repository root.

This executes the exact `SppControlWorkflow.h` used by the example against the
production deterministic `spp::Engine<>`. The fake facade and ActionExecutor model
transport outcomes; they do not execute BTstack or radio. ASan/UBSan and compiler
warnings-as-errors are enabled. Existing adapter/pool/SDK tests remain separate.

Cases cover queued begin vs readiness, Basic/ERTM observation, serialized four
controls, local rejection without stream destruction, copied incoming observations,
route-generation retirement, service removal before route teardown, closed DLC vs
retained ERTM lease, queued peer selection vs completion, safe retry of nonqueued
selection, cancellation versus SDP quiescence, stop during startup and queued listen/connect/selection, application
deadline without invented release, route closure while control is pending, bounded
history loss, queue refusal and invalid options. The fake polls one event per step
to expose asynchronous gaps rather than completing every operation immediately.

No hardware, pairing, flashing, SSH or recording is involved. This is ordinary
application acceptance, not a replacement for an interrupted independent review.
