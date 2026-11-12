# 開発者向けサンプル入口

[English](developer_samples.md)

初めて公開 API を使う方は [DeveloperSamples の使い方](../examples/DeveloperSamples/README_jp.md) から始めてください。負荷試験の内部コードを読む前に、設定 → begin → poll → 状態確認の流れを小さな実行例で学べます。

1. [A2DP Source](../examples/DeveloperSamples/a2dp_source.cpp): `IAudioSource` から 10 秒の無音 PCM を送信する基本形。
2. [AVRCP Target](../examples/DeveloperSamples/avrcp_target.cpp): Controller からの操作受信、押下/解放の観察、アプリ側処理。
3. [SPP 1 route](../examples/DeveloperSamples/spp_client.cpp): channel / UUID 接続、候補選択、部分送信、受信、listen。
4. [SPP 2 routes](../examples/DeveloperSamples/spp_two_routes.cpp): 同じ peer に 2 route。共通ロジックは [SppSample.h](../examples/DeveloperSamples/SppSample.h)。
5. [SPP bounded reconnect](../examples/DeveloperSamples/spp_reconnect.cpp): 最大 3 回と待機/期限。無限リトライしないアプリ側ポリシー。
6. [A2DP + AVRCP](../examples/DeveloperSamples/a2dp_avrcp.cpp): 共有 runtime と callback から main loop への intent 引き渡し。
7. [A2DP + SPP + AVRCP](../examples/DeveloperSamples/a2dp_spp_avrcp.cpp): 2 SPP 受付口、同じ live ACL 上の明示的 A2DP 接続、全 profile の poll。

**範囲:** Pico W、Classic A2DP Source / AVRCP Target / SPP。HFP と FastStream は対象外です。A2DP Sink は完成した speaker facade ではありません。公開 API の別の例として、[受信](../examples/A2DPSinkReceiver/README_jp.md)、[上限付き PCM consumer](../examples/A2dpSinkPcmConsumer/README_jp.md)、[実測遅延コマンド](../examples/A2dpSinkDelayReportSample/README_jp.md)を用意しています。[START判定例](../examples/A2dpSinkStartDecisionSample/README_jp.md)では、事前選択の許可／拒否方針、要求token、切断処理を示し、既定は無線OFFです。[AVRCP main-loop 例](../examples/AvrcpMainLoopSample/README_jp.md)には明示的な Cover Art 接続・取得の流れもあります。これら後続例の検証範囲は各資料を参照し、元7例の実機結果を流用しないでください。

**安全な初期値:** peer 未設定では begin しません。設定後もコンソールの `b` まで無線を開始しません。USB CDC が標準、UART は選択式。固定 peer、SSP の明示 opt-in、RAM-only link key、A2DP facade 固有の pairing policy、世代付き handle と寿命の制約は詳細ガイドを読んでから実行してください。

## 役割と検証範囲

A2DP Sink は音声を受信し、AVRCP Controller は相手のプレーヤーへ操作を送ります。Target は逆に相手から操作を受けます。公開 media-control サンプルの操作は `PLAY` / `PAUSE` です。公開サンプル自体の実機検証は未完了で、別の試験の成功をそのまま適用できません。試験の詳細は [検証範囲](validation_jp.md) を参照してください。

## AVRCP Controllerからのメディア操作

[メディア操作コンソール](../examples/AvrcpMediaControlSample/README_jp.md)はA2DP Sink＋AVRCP Controller、明示的な再生・一時停止、callbackコピー、安全な押下・解放、PlayStatus照会、本人の確認を待つ手順を示します。radio/pairingは既定OFF、有効版もSTARTが必要です。個人peerアドレスや録音は含めません。別のWindows実機試験の知見を反映しましたが、組み直した公開Example自体は実機未確認です。上記Target例は操作を受ける逆方向で、両役を混同しません。

## Sink PCMと明示的なUSB録音

[PCM consumer](../examples/A2dpSinkPcmConsumer/README_jp.md) は PCM のコピーと main loop での消費を示します。5秒間の USB 録音は `SINK_PCM_USB_CAPTURE=ON` と明示的な `CAPTURE` 操作が必要です。radio／録音は既定 OFF、公開統合例は実機未検証です。個別試験の履歴は [検証範囲](validation_jp.md) に分離しています。

## 二路SPPのアプリ形式と双方向処理

[Framed duplex 例](../examples/SppFramedDuplexSample/README_jp.md) は制御と型付きデータの二路で部分 write/read と世代変更時の破棄を示します。形式はアプリ側のもので、SPP API は byte stream のままです。incoming／outgoing は排他的で、radio は既定 OFF です。個別試験の履歴は [検証範囲](validation_jp.md) に分離しています。

## ビルド後の確認

サンプルの README に従って設定・ビルドし、開始操作の前に peer と pairing 方針を確認してください。`begin()` の成功ではなく、接続状態、受信イベント、PCM やバイトの進捗を観測します。停止・切断後も非同期処理が終わるまで poll を続けます。

元の7例には限定した実機観測がありますが、全試験が無条件 PASS ではありません。未検証の経路や復元失敗を含む判定は [検証範囲](validation_jp.md) に保存しています。音質、任意相手、長時間動作、後から追加したサンプルへの保証ではありません。
