# Same-peer A2DP: experimental mechanism and limits

[日本語](a2dp_same_peer_experiment_jp.md) · [Normal dual-role use](a2dp_dual_role.md)

**This describes experimental components, not a supported integration recipe.** Normal Source/Sink helpers reject registration of both roles for the same peer. Two opposite-direction audio streams have not been validated on hardware.

## Compatibility with public helpers

`cmake/picow_a2dp_same_peer_experiment.cmake` and `picow_enable_a2dp_same_peer_experiment(target)` are experimental components. The current public Sink helper also replaces SDK `avdtp_acceptor.c` with its raw SBC guard, so **they cannot share one CMake configuration**. Reordering calls or selecting another target does not resolve the conflict. Follow the [build guide](build_guide.md) without the experimental helper for ordinary Sink applications.

The generator targets five hash-pinned SDK inputs. Unfamiliar input is rejected, not patched speculatively; the installed SDK stays unchanged. Shipping these components does not establish composition with normal helpers or radio interoperability.

## Experimental boundaries

- Distinguish connection-wide configuration state from each SEP's ownership. Do not erase another SEP's pending transaction to admit configuration.
- Validate role-local SEID, media CID and current ACL before dispatching control or media events.
- `detachSharedRole()` handles stable media CLOSE only. Retirement occurs in a later poll after STREAM_RELEASED and SDK endpoint reset. `false` means no request was accepted; busy CLOSE preserves the stream.
- `cancelSharedDiscovery()` is exposed only by the experimental Sink for discovery. Success admits a request; completion waits for SDK transaction drain. A ten-second timeout quarantines the failed owner without freeing a shared transaction or disrupting other-role media.

## Incomplete or unverified paths

Source-facade discovery cancellation, SET_CONFIGURATION/OPEN cancellation, ABORT of a configured SEP without a media CID, complete negotiation into two streams, repeated detach/rejoin, combined resource/load limits and radio interoperability are not completed or accepted. Ordinary `disconnect()` must not tear down the other role's shared signalling.

Software fixtures and ARM linkage are not real negotiation, streaming or reconnect results. Development-specific experimental runners are not included. Ordinary applications should use the [supported feature scope](supported_features.md).

## Licensing

BTstack-derived generator excerpts and generated source retain upstream terms. Keep the [provenance manifest](../licenses/a2dp-same-peer-manifest.json), [standard terms](../licenses/BTstack.txt) and [Raspberry Pi grant](../licenses/BTstack-Raspberry-Pi.txt).
