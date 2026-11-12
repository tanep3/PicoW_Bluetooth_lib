# AVRCP Target: メディア操作を受信する

[English](avrcp_target.md) | [Target 機能リファレンス](avrcp_target_features_jp.md)

`picow::avrcp::AvrcpTarget` は、設定した Controller（ヘッドセットなど）からボタン操作を受信します。受け付けた操作をアプリ自身の再生状態へ対応付けてください。この facade は音声再生、Controller 操作送信、Browsing データベースを提供しません。

## 前提とビルド

C++17、Pico SDK の polling 構成、対応する `btstack_config.h` を使います。`cmake/picow_avrcp_target.cmake` を include して `picow_target_avrcp_target(target config_directory)` を呼びます。SDK は peer ごとに AVRCP connection object を2個使うため、2 peer なら `MAX_NR_AVRCP_CONNECTIONS >= 4` を確保します。全体の構成は [main-loop サンプル](../examples/AvrcpMainLoopSample/README_jp.md)を参照してください。

Target と Controller は signalling 初期化と Bluetooth runtime を共有します。BTstack を二重初期化したり、旧 AVRCP plugin と混用したりしないでください。owner と callback context は process lifetime とし、すべてを単一 main loop で扱います。IRQ や別 core からは呼びません。

## 接続と操作処理

1. `AvrcpTarget::Config` に重複のない非ゼロ peer を1〜2件設定します。`setCallback` で操作 callback を登録し、必要なら[機能リファレンス](avrcp_target_features_jp.md)の同期 decision handler を設定します。
2. `begin(config)` を一度だけ呼び、結果を確認します。その後も `poll()` を続けます。初期化成功は接続完了ではありません。pairing、visibility、connectability は共有アプリの明示的な方針で設定します。
3. 許可 peer からの incoming 接続を受けるか、`connect(peer)` で outgoing 接続を要求します。他 profile が共有 SDP client を使用中なら要求は busy になり得ます。戻り値0はローカル受付です。`Connected` を観測し、`core().bindingForPeer(peer)` で接続を取得してから binding 付き API を使います。
4. callback の観測値を有界なアプリ所有領域へコピーし、main loop で処理します。decision handler の判断は同期して返す必要があり、後回しにはできません。decision handler と operation callback の両方で同じ操作を実行しないでください。
5. `setPlayback(length_ms, position_ms, status)` でアプリの再生状態を更新します。初期値は停止、長さ・位置は unknown です。facade はこの状態から play-status 要求へ応答します。

生成 SDK overlay が pass-through の wire 判断と応答 framing を担当します。二重 ACK は送らないでください。metadata、通知、音量/item 要求、player settings の制約と応答責務は [Target 機能リファレンス](avrcp_target_features_jp.md)にあります。SDP の version は完全な AVRCP 準拠保証ではありません。

## 切断と失敗時

`Binding` は peer、ACL epoch、CID、generation を含みます。古い binding を新しい接続へ付け替えないでください。`Disconnected` / `Reset` ではアプリの保持中ボタン状態を解除します。SDK callback に generation はないため、保存した生 packet を CID 再利用後に再投入しないでください。

`disconnect(binding)` は共有 signalling を閉じ、その CID の両 AVRCP role に影響します。接続準備中は `cancelConnection(peer)` と pending/status 照会を[共有取消契約](avrcp_controller_jp.md)に従って使います。非同期 cleanup が終わるまで poll と owner の保持を続けます。取消の受付は資源解放ではありません。

`stop()` は終端操作であり再接続 API ではありません。他 role が使用中の共有 channel は残る場合があります。古い／未知の release callback は `ignored` に加算されても、二度目の切断をアプリへ通知しません。owner の作り直しや `begin()` の繰返しで回復しようとしないでください。

## 結果の確認

イベントと状態は [API リファレンス](api_reference_jp.md)、個別の戻り値は機能リファレンスを参照してください。完全な checkout でのオフライン回帰:

同梱検査は[ビルドガイド](build_guide_jp.md#compile-only)を参照してください。

限定した Target 実機観測はありますが、全ボタン、保持中切断、任意ヘッドセット、長時間安定性の保証ではありません。計測値と過去の失敗は[検証範囲](validation_jp.md)に保存しています。host test や compile-only probe は実機検証ではありません。
