# Bounded PCM consumer acceptance

Run `bash tests/host/run_sink_pcm_sample_tests.sh` from the repository root.

The test includes the actual example's `BoundedPcmSink.h` and public `IAudioSink`
interface. It uses synthetic PCM only: no SDK, codec, receiver, radio, microphone,
file recording or device is involved. ASan/UBSan and warnings-as-errors are enabled.

Eight scenarios include borrowed-input reuse, signed extrema, stereo frame versus
sample units, partial/full FIFO behavior, wrap-around ordering, reset/format flush,
mono layout, invalid/null/zero/huge rejected input and odd destination capacities.
A separate deque oracle checks 40,000 deterministic randomized operations across
capacities 1, 2, 7 and 128 frames, with output canaries and cumulative statistics.

Receiver, codec and facade validation is separate. The consumer test alone does not establish callback ordering inside
BTstack, SBC decoding, radio interoperability, real-time playback or DMA ownership.

For included sample builds and runnable checks, see the [build guide](../../../docs/build_guide.md#compile-only). Those checks do not rerun every feature-specific validation described here.
