# Sink START decision sample

[日本語](README_jp.md)

A bounded application example for the optional public START allow/refuse API. It has **no audio renderer**: all PCM is dropped. Pairing confirmation stays disabled. No automatic listen, connect or reconnect.

## Build only

From the repository root, with an installed Pico SDK and local build tools:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S examples/A2dpSinkStartDecisionSample -B build/sink-start
cmake --build build/sink-start
```

This default has radio OFF and optional START support OFF. To compile support, add `-DSINK_START_DECISIONS=ON`. To compile the manually activated radio path, also supply `-DSINK_START_ENABLE_RADIO=ON -DSINK_START_PEER=<intended-source-address>`. An empty, zero or broadcast peer is rejected for that path. See the [build guide](../../docs/build_guide.md) for SDK/picotool setup. Offline builds require a compatible installed picotool; set `picotool_DIR` to its CMake package directory if needed. Building produces firmware files; it does not flash or run them.

## Console operation after flashing your Pico W

- `a`: allow future START requests.
- `r`: refuse future START requests, the default policy.
- `b`: initialize the receiver, only in a radio-enabled build. Missing optional support returns `FeatureUnavailable`.
- `l`: listen for the configured peer.
- `c`: request an outgoing A2DP connection to that peer.
- `x`: disconnect and cancel a pending decision.
- `s`: print build support and receiver state.

Choose policy **before** the peer sends START. The example answers each request immediately after polling with the selected policy. It never waits for human input while the peer's response timer is running. Suspend/restart generates a new request and applies the current policy. It processes at most one console character per loop. USB stdout has a zero blocking timeout, so logs may be lost rather than hold the Bluetooth loop.

A printed `Accepted` result (enum value 0) means only SDK admission of the requested allow/refuse. It is not a wire ACK or streaming proof. Inspect the `streaming` status for the ordinary completed START transition. Any other decision result causes disconnect in this sample, with no retry.

See the [feature contract](../../docs/sink_start_decisions.md). Host and ARM build results are in the [Validation scope](../../docs/validation.md). Hardware execution is **NOT_RUN**; this example does not establish real-peer START responses or audio output.

## Troubleshooting

If `feature=0`, rebuild with START support. If the console reports radio disabled, check the radio option and actual peer setting. Confirm `begun=1` before `l` or `c`. If playback does not start after connection, remember that refusal is the default; select `a` before the next START request. The peer also needs authentication readiness because automatic pairing confirmation is disabled.
