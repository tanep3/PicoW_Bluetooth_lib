# A2DP Source の上限付き SBC 分割送信

既存の stereo・16 blocks・8 subbands・loudness・bitpool 2–53、44.1/48 kHz を対象にします。完全なフレームが収まる場合、従来の1–4フレームまとめ送りは変更しません。収まらない場合だけ、1フレームを2–15個に分割します。payload上限はSBCヘッダーを含み、RTPヘッダーを除きます。1断片の符号化データは最大payload−1バイト。16個以上が必要な設定やデータが1バイトも入らない設定はPCM読出し前に拒否します。小さい合成payloadの試験は、その値が実機のL2CAP MTUとして交渉可能という意味ではありません。

## 一次資料と送信方式

Bluetooth SIG [A2DP 1.4.1 §§4.3.3–4.3.4](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/a2dp_v1-4-1_1752513648/A2DP_v1.4.1/out/en/index-en.html) に従い、各断片は共通timestamp、最終以外は同じpacketサイズです。Fは分割、S/Lは先頭/末尾、下位4bitは現在の断片を含めた残数です。M/Xと予約bitは0、PTはdynamic値。[RFC 3550 §5.1](https://www.rfc-editor.org/rfc/rfc3550.html#section-5.1) に従いsequenceはpacketごとに増えます。

使用SDKのBTstackは `501e6d2b86e6c92bfb9c390bcf55709938e25ac1`。raw media packet APIからbasic classicの`l2cap_send`へ進み、実credit確認後にbufferを確保する既存経路を使います。SDKのpayload-RTP helperは送信前にsequenceを進めるため使いません。保証範囲はCYW43 polling/basic AVDTPで、ERTMや任意の非同期portへ一般化しません。

## 所有権・進展・診断

- Source内部transportだけ明示的に分割を有効化します。他の利用者は既定の無効状態を維持。bool `send()`は有効化後でも途中prefixを送りません。分割を扱う利用者は`sendResult()`を使います。
- `Progress`を両result enum末尾へ追加し、既存値を保持します。1物理packetの受理を示しますが、media全体の完了ではありません。`Deferred`は今回の呼出しで未受理。どちらも同じ符号化batchを保持し、再エンコードやPCM再読出しはしません。
- transportは固定118バイトのframeコピーと長さ/offsetを保持します。途中でretry bytesが変わった場合、SDKを呼ぶ前に失敗します。pump中の追加allocationはありません。既存1024バイトstorageと1036バイトRTP vectorは維持。分割時pipelineは256 interleaved samples、符号化データ最大118バイトで、PCMには既存の余分な確保係数があります。完全frame時は最大4フレーム。設定変更時には旧bufferと新bufferが一時的に共存します。
- `MediaProgress`はsnapshotの物理packet数だけ増やします。最終`MediaResult`で完了batch数と受理sample数を増やし、分割frameなら128 samples分だけ有理数pacingを進めます。RTP sequenceは各成功packet、timestampは最後だけ更新。Busyでは更新せず、各送信に次のpollと新creditが必要です。
- `sent_packets`とsnapshot `packets`は物理packet数。末尾追加の`completed_media`は完了batch数で、codec frame数とは限りません。`media_deferrals`へ成功途中進展を含めません。`abandoned_partial_frames`は成功した非最終断片が1個以上ある未完groupの破棄数です。全く送れていないBusy batchは含めず、多重resetでも二重加算しません。
- pause、pipeline reset、受理済みcodec再設定、disconnectで未完データを破棄します。同一sessionのpause/reset/codec再設定は受理済みRTP sequence/timestampを保持します。新sessionのtransport `configure()`は従来どおり両方を0へ戻します。未完破棄へsample/pacingの完了creditは付けません。
- `abandonPending()`自身がgenerationを変えます。送信中に同期resetされた古い結果はFailedとなり、新状態のcounter/sequence/offsetへ反映せず、古い断片を復活させません。合成SDK SUCCESSでも同様です。対象basic経路には送信内callback/yieldがなく、この合成obsolete completionや任意の非同期portで物理counterの帰属は保証しません。捕捉したSUCCESSを「物理送信なし」と解釈しません。
- pipelineの置換時には符号化batchがpendingの場合だけ旧transportの破棄hookを呼びます。この場合は旧transportを置換完了まで生存させてください。pendingがない場合は旧transportを参照せず、完了後に破棄したstack transportからの置換も可能です。Failed時はpendingを0にする前に破棄hookを実行します。reset/loopでは現在の借用objectの生存が必要です。既存transport用hookの既定実装は何もしません。pump中のconfigureは禁止のままです。

新enum/State/Diagnostics fieldは末尾追加ですが、再コンパイルが必要です。binary ABI互換は保証しません。Sink、same-peer policy、security/admission、codec対応範囲や広範なlifecycleは変更しません。

host試験とoffline ARM linkは実機・無線・peer相互運用のacceptanceではありません。

[検証範囲](validation_jp.md)

同梱サンプルのビルドと実行可能な検査手順は[ビルドガイド](build_guide_jp.md#compile-only)を参照してください。上記の機能別検証範囲をすべて再実行する手順ではありません。
