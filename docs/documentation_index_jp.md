# ドキュメント一覧

[English](documentation_index.md)

## 初めて使う場合の読む順序

1. [README](../README_jp.md): 用途・利点・使うサンプルを選ぶ。
2. [ビルドガイド](build_guide_jp.md): SDK/ツール、接続先アドレス、書き込み、困ったとき。
3. [はじめてのアプリ](getting_started_jp.md): 自分のPicoアプリへ組み込む完全な例。
4. [APIリファレンス](api_reference_jp.md): 必要な関数・引数・戻り値を調べる。

対応機能や制約を先に確認したい場合は、[機能と制約](supported_features_jp.md)を参照してください。以下は目的別の索引です。必要な項目だけお読みください。

## 音声受信・プレーヤー操作

- **SBCを受けてPCMを使う:** [PCMの受け渡し](a2dp_sink_pcm_jp.md)、[PCM受信サンプル](../examples/A2dpSinkPcmConsumer/README_jp.md)。
- **Picoから送信元を操作する:** [受信＋操作サンプル](../examples/AvrcpMediaControlSample/README_jp.md)、[Controller API](avrcp_controller_jp.md)、[ビルド手順](build_guide_jp.md#sink-avrcp)。
- **相手からPicoを操作する:** [Targetガイド](avrcp_target_jp.md)、[Targetの機能](avrcp_target_features_jp.md)。再生や音量調整の処理はアプリ側で実装します。

## プロジェクトの入口

| 資料 | 日本語 | English |
|---|---|---|
| PicoW Bluetooth Library（日本語） | [日本語](../README_jp.md) | [English](../README.md) |
| Features and limitations / 機能と制約 | [日本語](supported_features_jp.md) | [English](supported_features.md) |
| Your first application / はじめてのアプリ | [日本語](getting_started_jp.md) | [English](getting_started.md) |

## アプリケーションのサンプル

| 資料 | 日本語 | English |
|---|---|---|
| `A2DPSourcePlayer` | [日本語](../examples/A2DPSourcePlayer/README_jp.md) | [English](../examples/A2DPSourcePlayer/README.md) |
| `A2DPSinkReceiver` | [日本語](../examples/A2DPSinkReceiver/README_jp.md) | [English](../examples/A2DPSinkReceiver/README.md) |
| `A2dpSinkPcmConsumer` | [日本語](../examples/A2dpSinkPcmConsumer/README_jp.md) | [English](../examples/A2dpSinkPcmConsumer/README.md) |
| `AvrcpMediaControlSample` | [日本語](../examples/AvrcpMediaControlSample/README_jp.md) | [English](../examples/AvrcpMediaControlSample/README.md) |
| `DeveloperSamples` | [日本語](../examples/DeveloperSamples/README_jp.md) | [English](../examples/DeveloperSamples/README.md) |
| `SppFramedDuplexSample` | [日本語](../examples/SppFramedDuplexSample/README_jp.md) | [English](../examples/SppFramedDuplexSample/README.md) |

## API・組み込みガイド

| 資料 | 日本語 | English |
|---|---|---|
| A2DP Source lifecycle / 接続・再生操作 | [日本語](a2dp_source_lifecycle_jp.md) | [English](a2dp_source_lifecycle.md) |
| SBCパケットのまとめ送りと分割 | [日本語](source_sbc_fragmentation_contract_jp.md) | [English](source_sbc_fragmentation_contract.md) |
| アプリケーション API リファレンス | [日本語](api_reference_jp.md) | [English](api_reference.md) |
| AVRCP target: ヘッドセット → Pico の操作受信 | [日本語](avrcp_target_jp.md) | [English](avrcp_target.md) |
| Bluetooth Classic の解説 | [日本語](bluetooth_classic_jp.md) | [English](bluetooth_classic.md) |
| Pico W Bluetooth Library ビルドガイド | [日本語](build_guide_jp.md) | [English](build_guide.md) |
| 開発者向けサンプル入口 | [日本語](developer_samples_jp.md) | [English](developer_samples.md) |
| SPPのSDKへの組み込み | [日本語](spp_sdk_integration_jp.md) | [English](spp_sdk_integration.md) |

## 追加機能の組み込みリファレンス

| Topic | 日本語 | English |
|---|---|---|
| A2DP Sink / PCM handoff | [日本語](../examples/A2DPSinkReceiver/README_jp.md) | [English](../examples/A2DPSinkReceiver/README.md) |
| A2DP Sink 実測遅延コマンド例 | [日本語](../examples/A2dpSinkDelayReportSample/README_jp.md) | [English](../examples/A2dpSinkDelayReportSample/README.md) |
| PCMの受け渡し | [日本語](a2dp_sink_pcm_jp.md) | [English](a2dp_sink_pcm.md) |
| SBC分割パケットの受信 | [日本語](a2dp_sink_fragments_jp.md) | [English](a2dp_sink_fragments.md) |
| A2DP Sink SBC設定 / mono PCM | [日本語](sink_sbc_configurations_contract_jp.md) | [English](sink_sbc_configurations_contract.md) |
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

AVRCPを初めて使う場合は、[利用ガイド](avrcp_examples_jp.md)から始めてください。

Cover Artの取得を含む例は、[AvrcpMainLoopSample](../examples/AvrcpMainLoopSample/README_jp.md)にあります。実行に必要な設定や操作は、サンプルの説明を参照してください。

以下は実行手順ではなく compile/link 検査用です。
- `AvrcpBrowsingCompileProbe`: [日本語](../examples/AvrcpBrowsingCompileProbe/README_jp.md) / [English](../examples/AvrcpBrowsingCompileProbe/README.md)
- `AvrcpCoverArtCompileProbe`: [日本語](../examples/AvrcpCoverArtCompileProbe/README_jp.md) / [English](../examples/AvrcpCoverArtCompileProbe/README.md)

## 設計・制約・検証

- [アーキテクチャ](architecture_jp.md)と[仕様](specification_jp.md)
- [接続状態の遷移](connection_state_transitions_jp.md)と[SPPの状態遷移](spp_state_transitions_jp.md)
- [検証範囲と制約](validation_jp.md)
- [HFPが非対応である理由](HFP_UNSUPPORTED_REASON.md)
- [同一相手のA2DP送受信実験](a2dp_same_peer_experiment_jp.md): 通常用途向けではない追加機能
- [AVRCP pure core](../avrcp-core/README.md): 実装者向けの状態機械と境界

## ライセンス

独自部分は[MIT License](../LICENSE)、第三者部分には[別の条件](../THIRD_PARTY_NOTICES.md)が適用されます。`licenses/`の原文と出典情報を保持してください。ライセンス本文や実装者向けのテスト説明は、すべてが日本語訳されているわけではありません。
