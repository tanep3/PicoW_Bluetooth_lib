# PicoW Bluetooth Library

[English](README.md)

**Pico Wに、Bluetoothの音声送受信・再生操作・データ通信を組み込むためのC++ライブラリです。**

Bluetoothスピーカーへ音を送る、PCから音声を受け取る、ボタンでPCのプレーヤーを操作する、といったアプリを作れます。接続管理や音声パケットの処理をライブラリが受け持つので、音源やアプリの機能づくりに集中できます。

対象は **Raspberry Pi Pico W / Pico SDK / C++17** です。

## できること

- **音声を送る・受け取る（A2DP）**: アプリのPCM音声を送信し、受信した音声はPCMとして取り出せます。対応コーデックはSBCです。
- **プレーヤーを操作する・操作を受け取る（AVRCP）**: 再生、一時停止、曲送り、音量などを扱います。
- **データをやり取りする（SPP）**: 設定値やセンサーデータなどを、シリアル通信のように送受信します。

たとえば、PCから音声を受け取りながら、そのPCへ再生操作を送ることもできます。

## サンプルを試す

まず[開発環境を準備](docs/build_guide_jp.md#prerequisites)し、目的に合うサンプルを一つ選んでください。各サンプルに、接続相手の設定からビルド、Pico Wへの書き込み、操作までの手順があります。

| やりたいこと | サンプル |
|---|---|
| Bluetoothスピーカーへ音声を送る | [A2DP Source](examples/A2DPSourcePlayer/README_jp.md) |
| PCの音声を受け取り、そのプレーヤーを一時停止・再生する | [A2DP Sink＋AVRCP Controller](examples/AvrcpMediaControlSample/README_jp.md) |
| 受信したPCMを自分の処理で使う | [PCM受信](examples/A2dpSinkPcmConsumer/README_jp.md#first-use) |
| データ通信や、相手からのボタン操作の受信を試す | [DeveloperSamples](examples/DeveloperSamples/README_jp.md) |

A2DP Sourceサンプルは、スピーカーへ短いテスト音を送ります。受信サンプルはPCMを取り出すところまでで、Picoから音を鳴らすには、別途DACなどへの出力処理が必要です。

<a id="building-the-project"></a>
## 自分のアプリに組み込む

[アプリへの組み込み手順](docs/getting_started_jp.md)に、ファイルの配置、CMakeの設定、C++のコード例をまとめています。既存のPico SDKプロジェクトにも組み込めます。

- 関数や引数を調べる → [APIリファレンス](docs/api_reference_jp.md)
- ビルドや接続で困った → [トラブルシューティング](docs/build_guide_jp.md#troubleshooting)
- ほかのサンプルや詳しい仕様を探す → [ドキュメント一覧](docs/documentation_index_jp.md)

<a id="sink-controller"></a>
## 利用にあたって

- **Bluetooth Classic用**です。BLEやHFP通話には対応していません。Arduino / MicroPython用のライブラリではありません。
- 再生処理やスピーカーへの出力、受け取った操作に合わせた動作は、アプリ側で実装します。
- 相手機器によって使える機能は異なります。[対応状況と動作確認の範囲](docs/supported_features_jp.md)を確認してください。

## ライセンス

プロジェクト独自部分は[MIT License](LICENSE)です。Copyright (c) 2026 Tane Channel Technology。
依存ライブラリや第三者由来のコードには別の条件があるため、利用・再配布前に[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)も確認してください。
