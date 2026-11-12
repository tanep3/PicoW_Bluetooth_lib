# SPP: RFCOMM byte stream を組み込む

[English](spp_sdk_integration.md)

`spp::Client` は固定 peer との最大二路の全二重 byte stream を提供します。C++17 と単一 main loop を使い、`cmake/picow_spp.cmake` の `picow_target_spp(target config_directory)` で組み込みます。[利用例](spp_examples_jp.md)から始め、以下の所有権・部分 I/O・SDP 契約を確認してください。

## 所有とAPI

共有 BluetoothRuntime が CYW43、L2CAP、SDP server/client、HCI/ACL観測、
電源とpairing policyを所有する。A2dpSourcePlayerもこのruntimeへ合流し、
独立した二重初期化をしない。A2DP音声pipeline自体をSPP向けに置き換えない。
SDP clientは単一所有であり、A2DP側の内部SDPも含め競合を避ける。
この統合はA2DPとの実機同時通信の合格を意味しない。

`spp::Client` はprocess lifetimeの固定インスタンスとして使用する。
callback登録やSDP recordの保存領域もresetまで存続させる。
全APIとcallbackは同じmain loopに属し、IRQ・別coreから直接呼ばない。
`begin()` と `poll()` が非同期初期化・処理を進める。bool戻り値は要求のqueue受理であり、
接続成功ではない。`snapshot()`、`historySize()`、`history()`、`droppedEvents()`で
初期化失敗、遷移、操作結果、欠落を観測する。

- `connect(slot, peer, channel)` はchannel明示。
- `connect(slot, peer, Uuid)` はSDP解決後の接続。`Uuid::from16(0x1101)`と128bit UUIDを扱う。
- `discover(peer, uuid)` は候補取得のみ。`cancelDiscovery(token)` は検索取消要求。
- 複数候補はSelectingで停止し、`select(handle,index)`でアプリが選ぶ。勝手に一つ選ばない。
- `listen(slot,channel,uuid)` は受付口とSDP recordを登録する。
- `disconnect(handle)` はそのrouteを切断する。
- `tryWrite(handle,p,n)` は受理範囲をライブラリへコピーし、受理byte数を返す。
  部分受理を許し、未受理分はアプリが保持する。呼出元ポインタは保持しない。
- `read(handle,p,n)` は受信byte列をコピーする。write/read境界一致やアプリ処理完了を保証しない。

### tryWrite の再試行と完了の区別

戻り値は `IoResult{bytes,error}`。アプリの送信位置は受理された `bytes` だけ進め、
受理済み範囲を再投入しない。`WouldBlock` はTX空き不足、`Busy` は未処理eventや
Engineのdispatch中などでI/Oが先行できない場合であり、いずれもそれだけでfatalと扱わない。
`Engine::tryWrite` は未処理event/dispatch時に `{0,Busy}` を返す。単一loopでも、
一方のrouteへのwriteがeventを生成すれば、直後の別routeへのwriteはBusyになり得る。

未受理末尾を同じ世代のhandleに紐付けて保持し、通常の `poll()` で処理を進めた後に再試行する。
Busy中に同じloopで空回りしたり、callbackから再入したりしない。有限budgetのpoll後もBusyが
続く場合があるため、snapshotのroute状態・世代・runtime障害を併せて確認する。
切断、世代変更、fatal errorではアプリが保持する未受理末尾を破棄し、新routeへ持ち越さない。
ライブラリがすでに受理したbyteはライブラリ側の所有で、切断時の破棄はcounterで観測する。

`tx_accepted` はライブラリへのコピー、`tx_sent` はSDKのローカル送信成功。
どちらも相手が受信・処理した証明ではない。相手の受信量やecho/アプリACKを別に照合する。
`rx_received` はこのPico側が受信したbyte数で、相手側の受信量ではない。

handleのslotとgeneration、操作token、ACL epochを正規化時に保持する。
旧cidの遅延callbackへ現在のgenerationを付け直さない。
接続完了前のdisconnectがSDKで拒否された場合はClosingを保持する。
遅れてOpenedが成功すれば再度disconnectし、取消要求だけでslotを再利用しない。

## SDPの実際のevent形式と取消

このSDKの `sdp_client.c` は `SDP_EVENT_QUERY_ATTRIBUTE_VALUE` を
**11 byteのlegacy BYTE形式**で発行する。record indexはoffset2、attribute IDは4、
attribute sizeは6、byte offsetは8、dataは10。生成VALUE getterの連続value解釈を使わない。
DE headerの通知時はsize=0で、payload通知時にsizeが確定する。
parserはこの遷移を扱い、offset順序、確定長、上限、不完全終端を検証する。
属性上限256 byteはSDP parserの制限であり、SPPのpayload上限ではない。

query-local record indexで属性を組み合わせ、0x0000から実record handle、
0x0004からRFCOMM channelを取得する。record/channel候補は最大8件。
便利なRFCOMM service callbackだけではrecord identityを保持できないため使わない。
不正・不足・容量超過を成功候補に読み替えない。

SDKには公開SDP cancel APIがない。取消・timeout時は結果を抑止するがSDK処理は
自然なQUERY_COMPLETEまでdrainする。その間共有client所有を保持する。
完了後にSdpQuiescedを通知し、取消要求直後にbusy=falseとはしない。
StartSdpがbusy以外のエラーを返したのにSDK内部busyが残った場合は、runtimeを
failed()終端としpowerOffする。A2DP/SPP双方がこの失敗を観察し、reset前の再試行をしない。

もう一つのSDK差分として、RFCOMM CHANNEL_OPENEDは宣言長18 byte（packet[1]=16）
に対し実callback長17 byteである。`SdkEventLength.h`ではexact17/decl16の
組合せだけを例外扱いし、使用する実フィールドが17 byte以内に収まることを確認する。
一般的な短いpacketや任意の長さ不一致を許容するものではない。

## MTU・メモリ・セキュリティ

`MaxFrame=1024`、TX/RX FIFO は各 route に1024 byte、route は最大2です。共有設定では `HCI_ACL_PAYLOAD_SIZE <= 1033` が必要です。サンプルの1028なら outgoing RFCOMM MTU の上限は1019で、交渉により下がります。frame credit は最大4ですが、実際の許可数は未読領域と MTU に依存します。

FIFO だけで総 RAM を見積もらないでください。event queue、history、runtime、BTstack、SDP record、アプリ buffer を含め、選択した構成の map/sizeof を確認します。ERTM は別途アプリ所有 buffer が必要です。

`Security` の既定は `authentication=true`、`allow_confirmation=false` です。試験用 probe の pairing 設定をそのまま製品の方針にしないでください。

<a id="保存した証拠"></a>
## 検証範囲

UUID 解決と二路 echo の短時間試験は、最大性能や同時音声の保証ではありません。試験条件と失敗履歴は [検証範囲](validation_jp.md) の既存証拠を参照してください。

## 送信待ちと buffer の所有権

CAN_SEND_NOWは排他的な送信予約ではない。callbackからqueueを経てActionを実行する間に、
別routeが共有HCI bufferを使うことがある。adapterは実行時にreadyを再確認する。
未ready、ACL buffer満杯、RFCOMM credit不足、aggregate flow停止は `SendDeferred` として
coreへ返し、TX FIFO・所有権・受理／送信counterを維持したまま新tokenで送信可能通知を要求する。
永久的な失敗は `SendResult` で失敗させ、後続 `Closed(status=0)` で元の失敗statusを消さない。

使用中BTstackの `rfcomm_send()` は下位の `rfcomm_send_prepared()` の戻り値を捨てるため、
`SdkSend.h` はreserve/copy/prepared-sendの経路で実際のstatusを扱う。
このhelperのbuffer契約は「成功時はSDKが消費／解放、失敗時は呼出側に所有権が残る」。
Pico SDK 2.1.1のCYW43 polling・basic RFCOMM経路を確認したもので、任意transportへの移植保証ではない。
他transportでは下位層が失敗時にすでにreleaseする場合があり、同期callbackで別profileが再予約する
可能性もあるため、単なるreservedフラグでは所有者を判別できない。移植時にはこの契約を再検証する。
ERTM は両方の feature macro、生成 SPP overlay、アプリ所有 buffer が必要です。[ERTM 組込み](spp_lifecycle_jp.md)を参照してください。

限定した実機再現結果と残課題は 段階4結果 を参照。
A2DP＋SPP二路の同時音声試験、長時間安定性、遅延・転送量保証はこの検証に含めない。


## 後続のシリアルポート制御拡張

listener解除、modem/line status、port configurationの追加API、受付と完了の区別、別途の検証状況は [SPPサービスと制御](spp_control_jp.md) を参照してください。上記のRAM値・実機結果は記録当時の版の値です。

後続のpeer再設定・ERTM統合は [SPP lifecycle](spp_lifecycle_jp.md) を参照してください。host/build結果を無線受入の代わりにはしません。
