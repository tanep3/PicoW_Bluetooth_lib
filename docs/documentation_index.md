# Documentation index

[日本語](documentation_index_jp.md)

## A reading order for new users

1. [README](../README.md): choose a use case and sample.
2. [Build guide](build_guide.md): SDK/tools, peer address, flashing and troubleshooting.
3. [Your first app](getting_started.md): a complete integration project of your own.
4. [API reference](api_reference.md): look up functions, arguments and return values as needed.

For a quick overview of supported behavior, see [Features and limitations](supported_features.md). Use the topic index below as needed.

## A2DP Sink and AVRCP: choose the direction

- **Receive SBC and consume PCM:** [Sink PCM contract](a2dp_sink_pcm.md) and [PCM consumer example](../examples/A2dpSinkPcmConsumer/README.md).
- **Pico controls the source player:** [Sink + Controller console](../examples/AvrcpMediaControlSample/README.md), [Controller API](avrcp_controller.md), and [separate build path](build_guide.md#sink-avrcp).
- **A remote controller controls Pico:** [Target guide](avrcp_target.md) and [Target feature contract](avrcp_target_features.md). Application playback/gain behavior is not automatic.

## Project overview

| Document | 日本語 | English |
|---|---|---|
| PicoW Bluetooth Library (English) | [日本語](../README_jp.md) | [English](../README.md) |
| Features and limitations / 機能と制約 | [日本語](supported_features_jp.md) | [English](supported_features.md) |
| Your first application / はじめてのアプリ | [日本語](getting_started_jp.md) | [English](getting_started.md) |

## Application examples

| Document | 日本語 | English |
|---|---|---|
| `A2DPSourcePlayer` | [日本語](../examples/A2DPSourcePlayer/README_jp.md) | [English](../examples/A2DPSourcePlayer/README.md) |
| `A2DPSinkReceiver` | [日本語](../examples/A2DPSinkReceiver/README_jp.md) | [English](../examples/A2DPSinkReceiver/README.md) |
| `A2dpSinkPcmConsumer` | [日本語](../examples/A2dpSinkPcmConsumer/README_jp.md) | [English](../examples/A2dpSinkPcmConsumer/README.md) |
| `AvrcpMediaControlSample` | [日本語](../examples/AvrcpMediaControlSample/README_jp.md) | [English](../examples/AvrcpMediaControlSample/README.md) |
| `DeveloperSamples` | [日本語](../examples/DeveloperSamples/README_jp.md) | [English](../examples/DeveloperSamples/README.md) |
| `SppFramedDuplexSample` | [日本語](../examples/SppFramedDuplexSample/README_jp.md) | [English](../examples/SppFramedDuplexSample/README.md) |

## API and integration guides

| Document | 日本語 | English |
|---|---|---|
| A2DP Source lifecycle / 接続・再生操作 | [日本語](a2dp_source_lifecycle_jp.md) | [English](a2dp_source_lifecycle.md) |
| Source SBC batching and bounded fragmentation / 送信の集約・有界断片化 | [日本語](source_sbc_fragmentation_contract_jp.md) | [English](source_sbc_fragmentation_contract.md) |
| Application API reference | [日本語](api_reference_jp.md) | [English](api_reference.md) |
| AVRCP target: receiving headset controls on Pico | [日本語](avrcp_target_jp.md) | [English](avrcp_target.md) |
| Bluetooth Classic Explained | [日本語](bluetooth_classic_jp.md) | [English](bluetooth_classic.md) |
| Pico W Bluetooth Library Build Guide | [日本語](build_guide_jp.md) | [English](build_guide.md) |
| Developer sample entry points | [日本語](developer_samples_jp.md) | [English](developer_samples.md) |
| SPP SDK integration | [日本語](spp_sdk_integration_jp.md) | [English](spp_sdk_integration.md) |

## Additional integration references

| Topic | 日本語 | English |
|---|---|---|
| A2DP Sink / PCM handoff | [日本語](../examples/A2DPSinkReceiver/README_jp.md) | [English](../examples/A2DPSinkReceiver/README.md) |
| A2DP Sink measured-delay command flow | [日本語](../examples/A2dpSinkDelayReportSample/README_jp.md) | [English](../examples/A2dpSinkDelayReportSample/README.md) |
| A2DP Sink bounded PCM consumer | [日本語](a2dp_sink_pcm_jp.md) | [English](a2dp_sink_pcm.md) |
| A2DP Sink bounded SBC fragment receive | [日本語](a2dp_sink_fragments_jp.md) | [English](a2dp_sink_fragments.md) |
| A2DP Sink SBC formats / mono PCM | [日本語](sink_sbc_configurations_contract_jp.md) | [English](sink_sbc_configurations_contract.md) |
| A2DP Sink raw configuration guard / 生設定検証 | [日本語](sink_raw_configuration_contract.md) | [English](sink_raw_configuration_contract.md) |
| A2DP Sink validator refusal recovery / 検証拒否後の回復 | [日本語](sink_validator_recovery_contract_jp.md) | [English](sink_validator_recovery_contract.md) |
| A2DP Sink optional START decisions / 開始の許可・拒否 | [日本語](sink_start_decisions_jp.md) | [English](sink_start_decisions.md) |
| A2DP dual role | [日本語](a2dp_dual_role_jp.md) | [English](a2dp_dual_role.md) |
| SPP status and port controls | [日本語](spp_control_jp.md) | [English](spp_control.md) |
| SPP peer selection and ERTM | [日本語](spp_lifecycle_jp.md) | [English](spp_lifecycle.md) |
| SPP control/peer-switching main-loop example | [日本語](spp_examples_jp.md) | [English](spp_examples.md) |
| Shared Classic bond backend | [日本語](bluetooth_bond_storage_jp.md) | [English](bluetooth_bond_storage.md) |
| Committed TLV storage / opt-in protection | [日本語](bluetooth_committed_tlv_jp.md) | [English](bluetooth_committed_tlv.md) |
| AVRCP Controller | [日本語](avrcp_controller_jp.md) | [English](avrcp_controller.md) |
| AVRCP Target features | [日本語](avrcp_target_features_jp.md) | [English](avrcp_target_features.md) |
| AVRCP Browsing | [日本語](avrcp_browsing_jp.md) | [English](avrcp_browsing.md) |
| AVRCP Cover Art Client | [日本語](avrcp_cover_art_jp.md) | [English](avrcp_cover_art.md) |
| AVRCP SDK overlays | [日本語](avrcp_sdk_overlay_jp.md) | [English](avrcp_sdk_overlay.md) |

Start with the [AVRCP usage guide](avrcp_examples.md) if you are new to these APIs.

[AvrcpMainLoopSample](../examples/AvrcpMainLoopSample/README.md) includes Cover Art retrieval. Follow its guide for the required configuration and controls.

The following are compile/link checks, not runtime transaction workflows.
- `AvrcpBrowsingCompileProbe`: [日本語](../examples/AvrcpBrowsingCompileProbe/README_jp.md) / [English](../examples/AvrcpBrowsingCompileProbe/README.md)
- `AvrcpCoverArtCompileProbe`: [日本語](../examples/AvrcpCoverArtCompileProbe/README_jp.md) / [English](../examples/AvrcpCoverArtCompileProbe/README.md)

## Design, limitations and validation

- [Architecture](architecture.md) and [specification](specification.md)
- [Connection state transitions](connection_state_transitions.md) and [SPP state transitions](spp_state_transitions.md)
- [Validation scope and limitations](validation.md)
- [Why HFP is unsupported](HFP_UNSUPPORTED_REASON_en.md)
- [Same-peer A2DP experiment](a2dp_same_peer_experiment.md): opt-in work, not a normal integration path
- [AVRCP pure core](../avrcp-core/README.md): implementation-level state-machine boundaries

## Licensing

Project-original portions use the [MIT License](../LICENSE); third-party portions have [separate terms](../THIRD_PARTY_NOTICES.md). Retain the original grants and provenance under `licenses/`. License texts and maintainer-oriented test documentation are not all translated into Japanese.
