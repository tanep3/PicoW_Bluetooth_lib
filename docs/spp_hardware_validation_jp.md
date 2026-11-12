# SPPの実機確認範囲

[English](spp_hardware_validation.md) · [SPPの組み込み](spp_sdk_integration_jp.md) · [検証の全体像](validation_jp.md)

Pico SDK 2.1.1 / BTstack 1.6.2、CYW43 polling、一つの相手という条件で公開SPP APIを確認しています。basic RFCOMM試験には2本の着信endpoint、ルート単位の再接続、読取りの一時停止、合成負荷、ACL再接続が含まれます。後の条件付き確認は[ライフサイクル](spp_lifecycle_jp.md)も参照してください。

別の相手・transport・負荷・長時間運用を保証する結果ではありません。機器専用runner、生ログ、復元用ファームウェアは同梱していません。

## アプリで確認すること

- endpoint slotとroute slotは別です。着信は空いたrouteへ入り、固定slotを仮定せずchannelと現在のbindingで識別します。
- 交渉したMTUと書込み可能量を守ります。2ルートはACLと無線を共有し、処理順を交互にしてもライブラリ全体の公平性は保証されません。
- 読取りと送信をブロックせず、部分受付を扱います。未受付データはroute世代に結び付け、再接続後へ黙って持ち越しません。
- 読取りを止めるとRX容量を消費し、最終的にcreditが止まります。ホストsocketへの書込み成功だけではPico側の受信・消費を証明しません。
- API成功は要求の受付で、完了は状態・イベントで判断します。古いsocket handleではなく、現在のACL tokenとroute世代を確認します。

## バイト数と送信延期

各世代で次の収支を確認できます。

- `tx_accepted = tx_sent + tx_size + tx_discarded`
- `rx_received = rx_read + rx_size + rx_discarded`

CAN_SEND_NOWは準備完了の通知で、後の呼出しまで共有HCI容量を予約するものではありません。他のprofileが資源を使った場合、未送信バイトを保持します。延期と永久的なtransport障害は区別してください。[状態遷移](spp_state_transitions_jp.md)と[ポート制御](spp_control_jp.md)に詳細があります。

未読バイトと既にcreditを与えた最大frame群は受信容量内に収めます。残量がMTU一つ分より小さければ、完全に満杯でなくてもcreditは止まります。

## 切断と再接続の限界

ACL切断より先にrouteが正常に閉じる場合があります。routeのerrorが0だけでは旧接続の生存を判断できません。routeのClosed、ACLの無効化、旧handleの終端、新しいtoken・世代を確認します。

限定したルート・ACL再接続の結果は、3機能負荷中の任意の回復、連続音声の品質、すべてのSDK失敗経路、各ホストの認証方式まで保証しません。ERTMには個別の明示設定と資源所有条件があり、basic RFCOMMの結果をERTMの合格とは扱えません。

実行する入口には[DeveloperSamples](../examples/DeveloperSamples/README_jp.md)や[framed duplexサンプル](../examples/SppFramedDuplexSample/README_jp.md)を使ってください。
