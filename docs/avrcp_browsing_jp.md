# AVRCP Browsing Controller・Target 利用ガイド

[English](avrcp_browsing.md)

これは上限を定めた共通transportとController実装で、AVRCP全機能や無線受入の完了ではありません。Browsing Targetの要求/応答APIも同じownerに追加しました。Cover Art Clientは[別の利用ガイド](avrcp_cover_art_jp.md)で対応し、画像serverは固定SDKにありません。既存signalling両roleや他profileを再初期化・停止しません。

## 準備とAPI

`AvrcpBrowsing.h`の`picow::avrcp::AvrcpBrowsing`を使います。従来名`AvrcpBrowsingController`はsource互換を維持し、同じ共有ownerを指します。CMakeの`picow_target_avrcp_browsing(target, config_directory)`はsignalling Controller/Targetと検証済みAVRCP/L2CAP overlayを含みます。`ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE`と2 peer用`MAX_NR_AVRCP_BROWSING_CONNECTIONS=4`、追加L2CAP service 1個・channel 2個を用意します。`examples/AvrcpBrowsingCompileProbe`参照（既定ではradio OFF）。

最初に`AvrcpController::begin(config, true)`でController browsingを広告し、browserの`begin(config)`でControllerのみ、`begin(config, true)`でTarget要求も有効にします。Target serviceを広告する場合はsignalling Targetにも`AvrcpTarget::begin(config, true)`を使います。両ownerはprocess lifetimeで一度のみ使用します。incomingは許可peer・有効ACLが必要で、重複/未知peerは拒否します。`connect(peer)`は既存signalling接続上でoutgoing browsingを開始し、共有SDP所有権を確認します。戻り値はローカル要求結果であり相手との接続成功ではありません。

- `bindingForPeer`はpeer、ACL epoch、browsing CID、generationを返します。古いbinding、closing、busy時の操作は拒否します。
- `folderItems(binding, scope, first, last, attributes)`はmedia player、virtual filesystem、search、now-playingの各scopeを扱います。attribute maskはbit 1〜8、0はALL、`AVRCP_MEDIA_ATTR_NONE`はこの操作で属性なしです。
- `setBrowsedPlayer`、`totalItems`、`changePath`（8byte UIDで上下移動）、`itemAttributes`（UID/counter/mask/scope）、`search`で他のController操作を公開します。itemAttributesのNONEはALLに黙って置換せず拒否します。searchはSDK所有領域に合わせ1〜19byteとし、長い入力は切詰めず拒否します。
- callbackはConnected、Disconnected、Response、Error。Response bytesの先頭は**相手のbrowsing status**（成功4）、以降はnetwork byte orderの元payloadです。`Event::status == 0`はローカル処理成功で相手の受理ではありません。参照はcallback中のみ有効なので非同期利用前にコピーします。callback内の変更操作は拒否します。
- `disconnect`は同CIDの両roleで共有するbrowsing channelを切断し、signallingや他profileは切断しません。`stop`は終端で非同期切断を要求し、全体deinitやradio OFFは行いません。`poll`は共有runtimeとACL生存確認を進めます。

## 所有権と応答解析

2組のstatic 10,000byte ERTM bufferはowner破棄後もtransport closeまで存在します。SDK alignment/state配置を考慮し、使用量を調整してMPSを`HCI_ACL_PAYLOAD_SIZE - 8`以下に抑えます。設定はmandatory ERTM、MTU512、RX/TX各4枠、FCS、再送/監視timer 2000/12000msです。L2CAP negotiation overlayが必要です。SDK CIDが残る失敗/待機接続はbufferを保持し、早期再利用しません。

固定SDK Browsing Controller parserは、切詰めitem領域より長いcallback sizeを報告し得ます。このfacadeではそのparserを公開せず、SDK要求serializerを使い、応答はpeerごと4096byteの所有領域で検証・再構成します。AVCTP SINGLE/START/CONTINUE/ENDのtransaction label、PID、packet数、最終長、要求PDUを確認します。応答envelope、folder record、text/folder長、attribute長をcallback前に確認します。media item内の意味は生application dataとして渡し、未検査iteratorは公開しません。大きいrecordも総量上限内なら切詰めません。不正/超過応答はErrorとし成功通知せず、正常応答または明示disconnectまでcommand待機を維持します。応答期限は既定で無効です。必要なら後述の `setTimeouts` と時刻付き `poll(now_ms)` を使ってください。

SDK修正はhash固定原本から生成し、installed SDKは変更しません。空の共通transport packetはheader参照前に拒否し、service登録失敗をbeginへ伝え、folder属性なし要求のcount/parameter lengthを修正します。生成SDKには元BlueKitchen/Raspberry Piライセンスが適用されます。[overlayライセンス説明](avrcp_sdk_overlay_jp.md)参照。応答adapterはrepository独自コードです。

## Target要求と応答

単一のbrowsing ownerが両方向handlerを登録します。peerごとにController outgoingとTarget incomingのtransaction状態を分け、serviceやtransportを二重作成しません。signalling初期化後のTarget専用利用も可能です。`setRequestCallback`には借用request payload（network byte orderのPDU parameters）と、live Binding・sequence・transaction label・PDUを含む`RequestToken`を渡します。token/dataをコピーしmain loopから後で応答します。callback中の変更操作は拒否します。

`respondSetBrowsedPlayer`、`respondFolderItems`、`respondTotalItems`、`respondChangePath`、`respondItemAttributes`、`respondSearch`、`reject`を使います。汎用`respond(token, status, payload, length)`のpayloadはstatus byteを含みません。成功応答はPDU構造と一致する必要があり、error応答には追加payloadを付けません。入力bytesは呼出し中に所有領域へコピーします。古い/変更済み/再使用token、別peerのtoken、二重応答、callback内からの変更は拒否します。

- SetBrowsedPlayerのfolder recordはBE16 name length＋name bytesをdepth回並べます。counter、item count、charsetはmethodで付けます。
- Folder item recordはitem type（1〜3）、BE16 body length、符号化済みbodyです。record境界を検査しますが、内部itemの意味的整合性はアプリの責任です。
- Item attribute recordはBE32 attribute ID、BE16 charset、BE16 text length、text bytesをcount回並べます。
- incomingは全6 PDUのlayout、scope/direction、attribute ID、count、範囲、search長をcallback前に確認します。分割requestには独立4096byte領域を使います。peerごとに未完incomingは1件で、応答待ち/送信中の追加要求で上書きしません。Controller操作は並行可能です。
- 応答領域はPDU/length/status header込み4096byte（typed methodのprefixを除く前のapplication payloadは最大4092byte）。元transaction labelを維持し、remote MTUとlocal上限512byteに合わせAVCTP SINGLEまたはSTART/CONTINUE/ENDで送ります。APIが成功して戻った後は呼出し元bufferを変更・解放できます。
- can-send時に`l2cap_send`が受理したframeだけoffsetを進めます。ローカル送信失敗は同一frameを保持して`poll`から再試行し、明示disconnectも可能です。Target最終fragment後に待機Controller送信を再予約します。`TargetResponseSent`は全frameのローカル受理で、**相手アプリからのACKではありません**。
- stop/releaseはtokenを無効化し遅れたアプリ応答を拒否します。static transport bufferは非同期closeまで保持します。SDK close eventはepochを含まないため、古い生callbackを保存して再投入しません。任意の期限設定は後述します。アプリからrejectや無応答peerのdisconnectも実行できます。Target無効またはcallback未設定なら、完全な未対応requestにGeneral Rejectを返します。

成功したController応答のUID counterはSDK状態にも同期し、次のChangePathがSetBrowsedPlayer/GetFolderItems/GetTotalItems/Searchで返ったcounterを使用します。

## 任意の期限設定

`setTimeouts(connection_ms, response_ms, target_ms, assembly_ms = 0)` は、新しい送受信接続、Controller の送信コマンド、受信が完了した Target 要求（自動 General Reject 応答も含む）の期限を設定します。既定値はすべて 0（無効）です。`0x7fffffff` を超える値およびコールバック中の変更は拒否されます。処理開始時の設定を保持するため、変更は既存の処理には影響しません。Target の期限はアプリの応答待ちと全応答フラグメントの送信・再試行を合わせた時間です。任意の第 4 引数は受信要求の組み立てに別の期限を設けます。Target が無効でも、有効な先頭フラグメントを受理した時点で設定を保持します。継続フラグメントの進行、不正なフラグメント、割り込み要求でこの期限を更新・解除することはありません。有効な要求が完成すると組み立て期限を解除し、独立した Target 期限を開始します。切断完了時は両方を解除します。従来の 3 引数の呼び出しも利用でき、組み立て期限は無効となります。組み立て期限切れの PDU は受理した先頭フラグメントの値です。

`poll(now_ms)` には 32 ビットで周回する単調増加ミリ秒を渡し、呼び出し間隔を 2^31 ms 未満にしてください。処理開始後の最初の時刻付き poll から計測するため、開始後は速やかに呼び出します。経過時間が期限以上になると期限切れとなり、途中の進捗で延長しません。従来の `poll()` は時刻を進めず、送信や切断再試行を処理します。

期限切れは Binding と `Timeout`（-1004）を持つ `Error` を一度通知します（接続時の PDU は 0）。その相手の共有 Browsing チャネル全体を使用不可にし、切断します。CT 処理と Target トークンの両方が無効となりますが、signalling ともう一方の相手には影響しません。遅延応答が残る間の transaction label 再利用を防ぎます。切断に失敗した場合は次の poll で再試行し、期限切れ通知は繰り返しません。遅延応答や can-send イベントも処理しません。SDK が保持する ERTM バッファは接続失敗または解放通知まで維持し、擬似的な解放や強制再利用は行いません。期限後の接続成功もアプリへ公開せず切断します。

## 検証の範囲

この機能の検証はhostモデルとARMコンパイル・リンクです。実peer相互接続、ERTM無線転送、他profileとの同時負荷、実機障害復帰は未検証です。Cover Artの画像serverは提供しません。

[検証範囲](validation_jp.md)
