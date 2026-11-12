# SPP: 状態遷移と pure core（段階2〜4）

**SPP段階2〜4の実装・受入記録です。初回利用の手順書ではありません。**

[English](spp_state_transitions.md)

本表は `spp-core/SppCore.h` の規則に対応する。段階3で追加した実SDK adapter、
共有runtime、製品APIと限定実機試験は末尾およびSDK統合記録に示す。
段階4の送信延期・失敗status保持とAPI経由の実機結果も反映した。
本coreのホスト成功を無線上の動作保証に読み替えない。

## 正本・相関・副作用

- `State` は runtime、2 route、2 endpoint、共有 SDP query の正本。payload は別の固定領域。
- runtime は `Stopped / Starting / Ready / Failed`。`begin` の失敗は route の Open 失敗とは別。
- route は `Closed / Resolving / Selecting / Connecting / Open / Closing`。
- SDP は `Idle / Querying / Complete / Failed / TimedOut / Cancelled`。
  `busy` は SDP client の所有を示し、timeout/cancel 後も adapter の `SdpQuiesced` まで真。
- `(slot, generation)` が route handle。callback はさらに cid と操作 token を照合する。
  ACL は共有 runtime が付ける単調増加 link token と HCI connection handle で相関し、同じ相手の再接続後に旧 ACL 切断を適用しない。
  adapter は要求時の token を保持して正規化する。通知時点の現行世代を古い cid に貼らない。
- Rule は `(State, Event) -> Transition` のみ。時計は `Tick` の単調時刻で受け取る。
- Engine は次 State を確定し、payload コピーを完了した後に Action を実行する。
  同期 callback は固定 queue に入り、現在の Action 列を抜けてから処理される。
- `poll(budget)` は有限件数で戻る。副作用中の `tryWrite/read` と未処理 event を追い越す
  I/O は `Busy`。main loop 所有であり、IRQ / 別コアから直接使用しない。

## 遷移表

| 前状態・条件 | Event | 次状態・Action | 確認区分 |
|---|---|---|---|
| Stopped | Begin | Starting、Initialize(runtime token) | ホスト保護仕様 |
| Starting、token 一致 | InitResult(0) | Ready | ホスト保護仕様 |
| Starting、token 一致 | InitResult(失敗) / 初期化期限到達 | Failed、原因/status、QuiesceRuntime | ホスト保護仕様 |
| Ready、空き route | ConnectChannel(peer, channel) | Connecting、世代更新、Connect | 2本成立は独立 probe 実測、core はホスト |
| Ready、空き route、SDP 空き | ConnectUuid(peer, uuid) | Resolving、共有 query=Querying、StartSdp | ホスト / 段階3実機 |
| Ready、SDP 空き | Discover(peer, uuid) | query=Querying、StartSdp | ホスト、無線は未実測 |
| Querying、query token 一致 | SdpCandidate(record, channel) | 上限8候補へ保存。同じ record/channel の重複は追加しない | ホスト |
| Querying、query token 一致 | SdpComplete(0) | Complete、所有解放。0候補=NotFound、1候補=Connect、複数=Selecting | ホスト / 1候補・複数候補は段階3実機 |
| Selecting、handle/query token/index 一致 | SelectCandidate | アプリ指定候補で Connecting、Connect | ホスト / 段階3実機 |
| Querying | SdpComplete(失敗) | Failed、route の解決失敗。ほかの route は維持 | ホスト |
| Querying、期限到達 / 候補上限超過 / 取消 | Tick / SdpCandidate / CancelDiscovery(token) または Disconnect | TimedOut / Failed / Cancelled、CancelSdp、busy 維持 | ホスト |
| SDP cancel の後、token 一致 | SdpQuiesced | busy=false。結果は保持。ここから次 query を許可 | ホスト |
| endpoint 未登録、channel 重複なし | Listen | Registering、RegisterService | ホスト / 段階4実機 |
| Registering、token 一致 | ListenResult | Listening または Failed | ホスト / Listeningは段階4実機 |
| Listening、指定 peer、空き route、重複接続なし | Incoming | Connecting、世代更新、Accept | ホスト / 公開APIで段階4実機 |
| 対象外 / 満杯、新しい有効 cid | Incoming | Reject、理由記録 | ホスト |
| 既に所有する cid / cid=0 / token=0 / 許可 peer の古い ACL token | Incoming | 拒否数だけ更新。既存接続を Reject しない | ホスト |
| Connecting、handle/peer/channel 一致 | Opened(0, cid, mtu) | Open、受信予約 credit、GrantCredits | ホスト / 段階3・4実機 |
| Connecting | Opened(失敗) | Closed、status 保存。他 route 不変 | 独立 probe / ホスト |
| Open、TX 空きあり | WriteRequested(n) | min(n, 空き) を受理。未要求なら RequestCanSend | ホスト |
| Open、handle/cid/token 一致、許可要求中 | CanSend | min(TX, MTU) の Send。readiness通知でありbuffer予約ではない。重複は拒否 | ホスト / 段階3・4実機 |
| Send 待ち、handle/cid/token 一致 | SendResult(0) | 送信分だけ FIFO を進める。必要なら次の RequestCanSend | ホスト / 段階3・4実機 |
| Send 待ち、handle/cid/token 一致 | SendDeferred | FIFO・所有権・tx_sentを保持しsending解除。新tokenでRequestCanSend。結果WouldBlock | ホスト / 段階4実機 |
| Open、受信予約あり | RxBytes | credit 1個消費、byte を RX FIFO へ保存 | ホスト / 段階4のRX停止実機 |
| Open | ReadRequested(n) | min(n, RX) を返す。最大 MTU 分を確保できる frame 数だけ再 grant | ホスト / 段階4のRX再開実機 |
| credit grant 待ち、token 一致 | CreditResult | 成功なら予約維持。失敗なら route 終了処理 | ホスト |
| Open / Connecting | Disconnect | Closing、未完 TX / 未読 RX を件数付きで破棄、Disconnect / CancelConnect | ホスト / Openからの片route切断は段階4実機 |
| Closing、接続成功が遅着 | Opened | late cid を Disconnect。書込みは再開しない | ホスト |
| handle/cid 一致 | Closed | Closed、未完件数と原因保持。status0で先行非0 statusを消さない。他routeは維持 | ホスト / 片route分離は段階4実機 |
| 指定 peer、新しい link token | AclOpened | ACL handle と link token を保存。active 中の重複は拒否 | ホスト / 段階4再接続実機 |
| 指定 peer、ACL handle / link token 一致 | AclClosed | ACL inactive。未終了の対象routeをClosed、関連SDPをcancel。既にClosedなら保持 | ホスト / SDPなしの切断は段階4実機 |
| SDK送信の非一時的失敗 / credit / 許可APIの失敗 | 対応する Result | 該当routeをClosing、未完件数、Disconnect。一時的送信不可はSendDeferred | ホスト |
| runtime 障害 / queue overflow / clock regression | RuntimeFault / QueueOverflow / Tick | runtime Failed、全 route を無効化、QuiesceRuntime | ホスト |
| 古い世代 / token / cid、状態違い | 各通知 | 拒否数と理由を記録、接続や byte 数を進めない | ホスト |

SDK adapter は `Initialize`, `StartSdp`, `RegisterService`, `RequestCanSend`, `Send`,
`GrantCredits` の結果を必ずイベントへ戻す。`RequestCanSend` 成功は `CanSend` が結果で、
失敗だけ `CanSendFailed`。Send実行時の一時的な送信不可は `SendDeferred` に分ける。
`SendDeferred` は末尾に追加したKind ID29で、既存のID0〜28を変えない。
SDP 完了は callback がもう来ない状態を意味する。
取消では SDK の終了を確認した `SdpQuiesced` が必須であり、timeout だけで client を使い回さない。
`AclOpened` は shared runtime が HCI Connection Complete から正規化し、前回より大きい非0の
link token を付ける。`Incoming` と成功 `Opened`、`AclClosed` はその ACL の token を保持する。
HCI connection handle が再利用されても旧 token は再利用しない。カウンター枯渇時は静止して
runtime を再構築し、wrap させない。ACL handle は0も有効。生成時に対応づけた token を使い、
古い callback に現時点の token を後付けしない。
対象外 peer の Incoming にも shared runtime がその ACL 固有の非0 token を付ける。
core はその peer を追跡対象に加えず、未所有の有効 cid に `Reject(token, cid)` を返す。
adapter は Reject 実行前に token/cid が同じ live ACL/DLC を指していることを確認し、
失効済みなら decline しない。許可 peer の ACL がまだなくても対象外の接続を拒否できる。
peer に関係なく、既に所有する cid の重複通知に Reject は出さない。
`CancelConnect` の terminal `Closed` は SDK の接続要求が静止してから返す。
遅い `Opened` はそれまで Closing のまま切断する。closing 中は slot を再利用しない。

## byte と観察の契約

`tryWrite(handle, bytes, n)` は非ブロッキングで `IoResult{bytes, error}` を返す。
戻り値のbytesが受理byte数で、部分受理がある。先頭の受理済みbyte は呼出し中にコピーされ、送信責任・所有権はライブラリへ移る。
呼出し元のポインターは保持しないので、その範囲の元メモリは直ちに再利用できる。
未受理末尾はアプリが保持し、先行 byte に続けて再投入する。Accepted は相手への配達や実行の ACK ではない。
`WouldBlock`（TX空き不足）と `Busy`（未処理event/dispatch等）は未受理末尾を保持する一時待ち。
Engineは未処理eventやdispatch中には `{0,Busy}` を返す。一つのmain loopでも、route0へのwriteが
eventを生成し、直後のroute1へのwriteがBusyとなる場合がある。snapshotのTX空きだけでは受理を保証しない。
受理された `bytes` だけ送信位置を進め、通常の `poll()` 後に残りを再試行する。Busyをfatalとして
残りを捨てたり、受理済みbyteを再送したり、pollせずspinしたりしない。
未受理データはhandleの世代に紐付け、切断・世代変更・fatal errorで破棄し、新routeに持ち越さない。
Busyにはoverflow処理待ちも含まれるため、poll後のruntime/route障害を確認し、無期限再試行の根拠にしない。
`read` もコピー API で、write 境界は復元しない。FIFO の byte を加工しない。

`snapshot().routes[i].txFree()` は Open 時の書込可能 byte 数。それ以外では0。
0から正数への変化（Open、満杯からの回復）で `writable_epoch` を増やし、
履歴 entry の `writable_mask` に route の bit を立てる。epoch を比較すれば履歴の上書き後も
「書けるようになった」を観察できる。毎回の空き数は snapshot が正本。
`tx_accepted / tx_sent / tx_discarded`、`rx_received / rx_read / rx_discarded` を区別する。
`tx_accepted` はライブラリへのコピー、`tx_sent` は SDK のローカル送信成功であり、
どちらも相手への到達・処理完了ではない。相手の受信量やecho/アプリACKを別途確認する。
`rx_received` はこのPico側の受信量であり、相手側の受信量ではない。

`CanSend` とSend実行の間に共有bufferやcreditの状態が変わり得る。adapterは実行時に再確認し、
一時的に送れなければ `SendDeferred` を返す。coreは受理済みbyteを所有したままFIFOを進めず、
新tokenで次のreadinessを要求する。アプリによる同じbyteの再投入は不要。
BTstack 1.6.2の `rfcomm_send` は内部送信の戻り値を隠すため、
`spp-sdk/SdkSend.h` はreserve/copy/`rfcomm_send_prepared` の結果を使用する。
非0結果時にはbufferが呼出側所有のままという前提でreleaseする。
対象は確認済みのCYW43 polling/basic RFCOMMで、任意transportには一般化しない。
ERTMの `ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE_FOR_RFCOMM` はcompile時に拒否する。

## SDP の UUID と選択

標準 0x1101 は `Uuid::from16(0x1101)`、カスタム UUID は canonical network byte order の
128bit 配列で指定する。adapter は実際の peer SDP を検索し、record handle / RFCOMM channel
を Candidate へ正規化する。core は UUID の意味を解釈しない。
候補上限8を超えたら `CandidateOverflow` と失敗を返す。切り詰めて唯一候補扱いにしない。
候補は `query.candidates`、UUID connect では `route.candidates` にも保持されるので、別 query
が始まってもアプリ選択を失わない。複数 service が同じ channel を示しても record が違えば別候補。
同一 peer/channel への同時二重接続は拒否する。

## 段階 3 の必須確認

1. channel 未知、標準 UUID とカスタム UUID の各々で SDP 解決から接続へ進む。
2. 同じ UUID の service を複数公開した peer から全候補が返り、選択前は接続しない。
3. SDK の SDP callback / cancel / RFCOMM late event を要求 token に正しく対応づける。
4. 小さい MTU、RX 消費停止、API 失敗、片 route 再接続、長時間、A2DP 回帰と共存を実測する。

## 固定容量と RAM

段階2当時の設定は2 route、TX/RX 各1,024 byte / route、最大 RFCOMM payload 256 byte、
同時受信 credit は最大4 frame / route。byte 空き量に加え frame window でも上限を設ける。
小さい MTU でも許可済み2 route ×4 frame を queue に収容できる。Engine の queue 下限は
この8 data slot +8 control slot の16件で、増量はできる。制御 event の無制限な burst は
許容せず、容量超過は runtime の明示異常。SDK の8bit credit 引数にも収まる。
段階2ではadapterを最大256 byteに構成する想定だったが、実SDK outgoing APIでは指定できなかった。
段階3はMaxFrame1024へ拡張した。HCI payload1028の現設定で1019は計算上の上限であり、
実際のMTUは相手との交渉値を使用する。段階4のincoming実測は1016だった。

Cortex-M0+ ABI の静的サイズは Route 192 + payload 2,048 = **2,240 byte / route**。
段階2当時のデフォルト Engine（2 route、queue16、履歴16）は **15,304 byte**。内訳と除外項目は
[設計書 §8](spp_generic_protocol_design_jp.md#8-複数sppのsdk対応と資源) を参照。

## 段階3への追記

[実SDK統合記録](spp_sdk_integration_jp.md) に現行APIとcallback正規化を記載した。
上の15,304 byteは段階2の履歴値。現行ARM Engineは28,616 byte、hostは28,632 byte。
Route+Payloadの2,240 byteは不変。QueuedEvent1104、State656、HistoryEntry320 byte。
取消SDPは自然なQUERY_COMPLETEまでbusyを保持する。接続完了前のSDK disconnect拒否は
Closingを保持し、late Openedで再切断する。
実機で標準UUID/custom128の未知channel解決と1537-byte exact echo、
複数候補の明示選択待ちとroute0 echoを確認した。追加route1はL2CAP pool3不足
status86からruntimeFailed終端となった。pool6へ拡張後の全3 trialはPASS。
両routeをそれぞれ明示選択し、cid3/channel19とcid4/channel23でOpen、各1537 byte
のexact echoを確認。core error=0、dropped_trace=0。session全体約12秒。
StartSdp非busy失敗後にSDK busyが残る場合もruntime failed/powerOff終端とし再試行しない。
A2DP/SPP同一targetの実SDK linkは成功。実機同時audioは未確認。
共用HCI payloadは1028へ明示統一し、MaxFrame+9以下のcompile-time guardを設ける。
A2DP-only config1695は変更しない。最終ACLなし・device消滅はpostflightで再確認。
保存したsession/console/postflight証拠は[SDK統合記録](spp_sdk_integration_jp.md#保存した証拠)を参照。


## 段階4への追記

[API経由の実機検証](spp_hardware_validation_jp.md) と
段階4証拠 に合格範囲を記録した。
`attempt3` は約23.06秒で全選択項目に合格。公開listenによるchannel30/29の2受付口、
片routeのdisconnect/reconnect、旧handle拒否、両roleのRX停止・再開、
209384-byte完全一致bulkと並行する制御echo100件、ACL切断と再接続を確認した。
各routeのRX停止時はMTU1016、未読1016 byte、credits0で停滞し、他routeは制御echoを継続した。

ACL切断ではRFCOMM `Closed` 2件が `AclClosed` より先に届いた。
両routeは既にClosedのためerror/status0を保持し、ACLだけinactiveになった。
その順序を異常とせず、全Closed・旧socket終端・新ACL token/route generation・再接続echoで判定する。
`Closed(status=0)` が先行する非0の失敗statusを保持する規則は、この通知順とは別の保護である。
非0のClosed statusが届いた場合はその値を優先する。

終了時はPico HCI OFF、対象ACLなし、母艦Paired/Bonded/Trusted/Connectedが全てno。
BlueZ deviceオブジェクトは残った。長時間、小MTU強制、SDP取消、A2DP同時実機、
任意transportやERTMの成功は今回の実測から推定しない。

別sessionのattempt4も全項目PASS（混在181216 byte完全一致、制御100件、SendDeferred24件）。
修正後のUUID回帰は標準→channel19、custom→23、複数候補19/23の明示選択と各1537-byte完全一致を再確認した。
最終host31 PASS、audio/connection回帰とcombined再linkも成功。数値・原本は
[段階4検証](spp_hardware_validation_jp.md) を参照。
