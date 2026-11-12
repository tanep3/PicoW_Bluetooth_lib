# SPPサービスの解除とシリアルポート制御

[English](spp_control.md)

これらは、発信接続にも着信受け入れにも使う同じ `spp::Client` の追加APIです。どちらの接続方向でもバイトストリームは全二重です。Clientという名前で発信役だけに限定されません。従来の所有権、ペアリング、SDP、FIFO、creditの契約は [SDK統合](spp_sdk_integration_jp.md) を参照してください。

## サービスの解除

`unlisten(endpoint_slot)` は、そのendpointのSDPレコードとRFCOMM登録の解除をキューに入れます。戻り値trueは完了ではなく受付です。`endpoints[slot].phase` が Listening → Unregistering → Disabled と進みます。endpoint tokenにより、遅延した解除要求・完了通知が後の再登録を破壊しないようにします。範囲外・Listening以外はfalse、キュー内競合やエラーはhistoryに残ります。

既存routeの接続、generation、FIFOは維持します。解除開始後に処理する着信要求は拒否します。Disabledになれば同じendpointを `listen` で再登録できます。他サービスのSDPレコードは解除しません。共有GAPのconnectable設定や電源も無効にしません。

## シリアルポート制御の送信

発信・着信のどちらでも、現在Openの `Handle` を使います。

- `sendLineStatus(handle, value)`: エラーなしは `0`。エラーありbit `1` と、overrun `2`、parity `4`、framing `8` を組み合わせます。複数エラーの組み合わせも受付します。
- `sendModemStatus(handle, flags)`: V.24のFC `0x02`、RTC `0x04`、RTR `0x08`、IC `0x40`、DV `0x80` のみ指定します。アダプタがwire用EA `0x01` を追加します。
- `configurePort(handle, PortSettings)`: 相手側ポートを設定します。型付きフィールドでbaud 2400～230400、data bits 5/6/7/8、stop bits 1/1.5、parity none/odd/even/mark/space、6種類のRFCOMM flow-control flagsを扱います。
- `queryPort(handle)`: 相手側の現在のポート設定を問い合わせます。
- `control(handle, ControlRequest)`: 上記操作に共通する型付き形式です。

`PortSettings` の初期値は9600 baud、8 data bits、1 stop bit、parityなし、flow controlなしです。`xon` / `xoff` は受信した文字を観測するためのフィールドです。固定SDKの設定APIはこれらを送信設定できないため、`configurePort` で非ゼロを渡すと、黙って無視せず拒否します。これは仮想シリアルポートの属性であり、実UARTの設定変更やBluetooth転送速度の制限ではありません。

## 受付と相手側完了の違い

trueは有限のイベントキューへの受付です。`poll` 後に `routes[slot].control`、`control_token`、`control_pending`、`control_status` とhistoryを確認します。同じrouteでローカルSDK呼出結果を待つ制御は1つまでです。続くキュー要求がBusyになる場合があります。tokenで重複・遅延結果を拒否します。不正値、古いhandle、Open以外のrouteは、相手や別routeに作用せず拒否します。

`ControlResult` はSDKの戻り値であり、相手の確認応答ではありません。特に固定SDK内部では連続したmodem/configuration更新がまとめられる場合があります。status 0でも全中間値の到達は保証しません。確実なアプリコマンド送信の代替にしないでください。制御失敗で独立したバイトストリームを切断しません。切断・generation更新後に、古い制御状態を新接続の状態として使わないでください。

## 状態・設定の受信

route snapshotには、コピー済みの最新値とgeneration内の受信回数を保持します。

- `remote_line_status`、`line_status_events`。
- `remote_modem_status`、`modem_status_events`。受信modem値にはwireのEA bitも残します。
- `local_port`、`local_port_events`: 相手からのローカルポート設定更新。
- `remote_port`、`remote_port_events`: 相手側ポートの応答・更新。

有限のhistoryには保持期間中の各正規化イベントを残します。これらは観測値であり、個々の要求に対応付けたACKではありません。route generationと現在ACL epochで識別します。CHANNEL_OPENED前の初期MSC/RPNも、接続をOpenと偽らず保持します。古いACL、別CID、短いパケット、不正なポート値で現在接続を更新しません。キュー超過は明示的runtime faultです。無限の通知バッファではありません。

## 固定SDKへの対応

BTstack `501e6d2b86e6c92bfb9c390bcf55709938e25ac1` は `RPN_DATA_BITS_5`、`_6`、`_7`、`_8` をすべて0と定義しています。一方、実serializerはwire値0/1/2/3を必要とします。`spp::DataBits` は明示的に正しい値を持ちます。`SdkPortConfiguration.c` はCで整数互換enumに変換し、欠陥enumに対してC++で範囲外変換することを避けます。受信側も同じ理由で検証済み2bitを直接読みます。SDK checkout自体は変更しません。[CMake helper](../cmake/picow_spp.cmake) がこのC bridgeを組み込みます。

## 検証と残事項

host testsは、発信・着信双方、既存data routeを切断しないサービス解除・再登録、値の転送、4種類のdata-bit wire値、SDK拒否の分離、不正長、接続準備中の状態通知、古いACL/route/tokenを検査します。固定SDKの実ヘッダと通信呼出のmockを使い、ASan/UBSanで実行します。既存のFIFO/credit/SDP/reconnect決定的回帰試験も必要です。Pico SDK 2.1.1の実ARMビルド・リンクで `PicoSppSdkProbe` への統合を確認します。

同梱検査は[ビルドガイド](build_guide_jp.md#compile-only)を参照してください。

限定した Pico/Linux 試験ではローカル制御受付と byte stream を確認しました。5/6/7 data bits の相手への適用、incoming line status、任意相手・長時間は未確認です。status 0 はローカル受付です。詳細は [検証範囲](validation_jp.md) を参照してください。

証跡: 試験結果とSHA256。

制御試験は固定SDKのRPN serializer本体も抽出して変更せずCでコンパイルし、4種類のdata bitsすべてについてwire flagsとparameter maskを検査します。channel検索と送信スケジュールだけをmockし、抽出した関数のSHA256をログに出力します。

[オフライン既定の制御・peer切替利用例](spp_examples_jp.md)
