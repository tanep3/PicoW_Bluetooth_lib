# AVRCP Cover Art 利用ガイド

[English](avrcp_cover_art.md)

固定BTstackが提供するのは**Cover Art Client**で、接続、thumbnail/image/properties取得、切断のAPIがあります。画像を提供するCover Art serverはありません。この段階はClient機能に対応し、画像提供Targetや無線受入完了を主張しません。

## 準備とAPI

`AvrcpCoverArt.h`の`picow::avrcp::AvrcpCoverArt`を使います。CMakeは`cmake/picow_avrcp_cover_art.cmake`をincludeし、`picow_target_avrcp_cover_art(target, config_directory)`を呼びます。`ENABLE_AVRCP_COVER_ART`、`ENABLE_GOEP_L2CAP`、`ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE`が必要です。2 peer用にoutgoing L2CAP channelを2個追加します。helperは対象consumerだけにCover Art/GOEPとL2CAP overlayを選択し、installed SDK原本は変更しません。

共有AVRCP signalling ownerを先に開始します。`begin(config, max_object_bytes)`は許可peer 1〜2件と正のbyte上限（既定はobjectごと16MiB）を受け付けます。ownerと初期化はprocess lifetimeで一度だけ使います。`connect(peer)`には許可peerの有効ACLとSDK内の既存signalling状態が必要で、共有SDP所有権を確認します。ローカル成功は相手との接続成功ではありません。Connectedを観測して`bindingForPeer(peer)`を取得します。

- `thumbnail(binding, handle)`、`image(binding, handle, descriptor)`、`properties(binding, handle)`でSDKの全3 GET操作に対応します。handleはASCII数字7桁＋NUL。descriptorは省略可能で、NULLならheaderなしで元画像を要求します。非NULLは最大256byte＋NULで、XMLの意味はpeerが判断します。長さ上限以内でも、交渉済みOBEX容量に収まらなければ拒否します。空文字はSDK同様に空headerとして扱います。
- handle/descriptorは非同期送信前にpeerごとの所有領域へコピーします。API復帰後は呼出し元領域を変更・解放できます。同peerで処理中の追加操作は拒否し、他peerは独立して利用できます。
- GET前に`setDataHandler`で同期sinkを登録します。Dataはlive binding、request番号、operation、offset、借用bytesとsizeを含みます。callback中に消費またはコピーし、SDK pointerを保存しません。facadeは全画像bufferを確保しません。
- sinkがfalseを返すと遅延cancelを要求します。次chunkがobject上限を超える場合も同様です。以後のdataは無視し、`poll()`でtransportをcancelします。その前にOBEX成功が届いても、Completeはsink/上限errorを返し、成功と誤報しません。受理byte数は拒否chunkを含みません。
- Completeはrequest番号、operation、受理byte数、statusを含みます。0はOBEX操作成功であり、**画像/XML内容の妥当性検証ではありません**。途中dataの後にerrorになる場合があるため、部分出力の破棄・保存はアプリが判断します。ローカルerrorはSizeLimit（-1001）、SinkRejected（-1002）、Cancelled（-1003）、Timeout（-1004）です。
- Bindingはpeer、ACL epoch、generation、Cover Art CID、signalling CIDを含み、古いbindingは拒否します。`poll()`はACLまたは対応signalling接続が消えた場合もcancelします。
- `disconnect(binding)`はidle時のみOBEX正常切断を行います。`cancel(binding)`は処理中のCover Art transportを閉じ、`cancelPeer(peer)`は接続待機中にも使えます。AVRCP signalling、browsing、A2DP/SPPを意図的には切断しません。明示cancelがローカル失敗した場合は以前の状態を維持し、遅延cancelはpollから再試行できます。
- `stop()`は終端で、全体GOEP/Cover Art deinitやradio OFFをせずcancelを要求します。ERTM/client/request領域はowner破棄後も非同期closeまで存在します。stop後はアプリcallbackを外します。古いSDK生callbackを保存しCID再利用後に再投入しません。

全APIは単一main loopで使います。callback内の変更操作は拒否し、sinkのbool戻り値がcancelの合図です。SDKにはこのClientの公開pause/resume/backpressure APIがありません。sinkは受信速度に追従するか、有限queueが満杯になった場合などにfalseを返します。

## main loopから明示的に有効化するtimeout

`setTimeouts(connection_ms, object_ms)`で、新規接続とGET操作の制限時間を個別に設定します。0は無効で、既定値は両方0です。`0x7fffffff`msを超える値は両設定を変更せず拒否します。callback内やstop後の変更も拒否します。受付済み操作の制限時間は、後から設定を変更しても変わりません。

`poll(now_ms)`へ呼出し元の単調増加millisecond値をuint32_t（2^32で折返し）として渡します。connect/GET後の最初の時刻付きpollを起点とするため、操作受付直後に呼び、以後も時刻付きpollの間隔を2^31ms未満に保って定期実行してください。unsigned差分で時計の折返しに対応します。**起点からの総経過時間**を制限し、無通信時間ではありません。data受信で期限は延長されず、接続済みidle状態にはtimeoutを適用しません。従来の`poll()`はruntime/cancel処理を続けますが、timerの開始・時刻更新はしません。wall clockやSDK timer callbackは使いません。

例えば接続前に`setTimeouts(15000, 60000)`を呼び、main loopで`poll(monotonic_ms)`を実行します。接続待機中の期限切れはTimeoutのErrorを1回通知します。GETの期限切れはrequestにTimeoutを記録し、SDKの操作完了またはtransport release時に受理byte数とともにCompleteで通知します。遅れたOBEX成功でも成功に上書きしません。以後のdata/追加操作を拒否し、SDKがcancelを一時拒否した場合はどちらのpollからも再試行します。`status().last_error`は期限切れ時にTimeoutとなりますが、その後cancel errorに変わる場合があります。requestの最終statusはTimeoutを保持します。

timeoutはcancel要求であり、**期限までの解放保証ではありません**。SDKがrelease（または接続失敗cleanup）を確定するまでclient、ERTM buffer、handle、descriptorの貸出状態を保持します。その間の再接続は拒否し、遅れた接続成功もConnectedとして公開しません。releaseが届かない場合はslotを保持し続け、不正なmemory再利用を避けます。`stop()`後もcallbackを外してstatic領域を維持します。この検証はfacade実装testであり、実無線での障害復帰の証拠ではありません。

## 所有権とSDK修正

static SDK client 2個とalignment付き10,000byte ERTM buffer 2組をprocess lifetimeで保持します。使用量はSDKのalignment/RX/TX状態配置を考慮し、MPSをHCI payload minus 8byte以下へ抑えます。mandatory ERTM、MTU512、RX/TX各4枠、FCS、timer2000/12000msです。画像を自動的にfileへ保存・uploadすることはありません。

hash固定のbuild directory overlayは原ライセンスを保持し、未知SDK bytesを拒否します。接続list解除順序、消去済callbackを使う失敗event、GOEP同期接続失敗cleanup、不正OBEX/parser停止、GET header容量検査、相手OBEX packet size、送信失敗、SDP/接続待ち/取得中cancelを修正します。GET事前検査errorは転送を始めずローカルreturnします。後のtransport失敗はfailure/completionを通知し、終端releaseまで所有権を保持します。[overlayライセンス説明](avrcp_sdk_overlay_jp.md)とmanifestに原noticeを維持し、SDKライセンスの置換や新たな商用権利の許諾は行いません。

`picow_avrcp_cover_art_client_cancel`はrepository独自拡張でupstream APIではありません。共有SDP処理中はCover Art要求だけ外し、他のSDP所有権は変更しません。L2CAP open待ちでは結果確定までmemoryを解放せず、遅れて成功した接続は直ちに閉じます。接続済み転送はGOEP経由で切断し、releaseまでbufferを保持します。

## 検証の範囲

この機能の検証はhostモデルとARMコンパイル・リンクです。実peer相互接続、ERTM無線転送、他profileとの同時負荷、実機障害復帰は未検証です。Cover Artの画像serverは提供しません。

[検証範囲](validation_jp.md)
