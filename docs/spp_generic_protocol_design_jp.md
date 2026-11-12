# 素のSPP・複数接続ルート：設計レビュー案

[English](spp_generic_protocol_design.md)

- 作成: 2026-11-08 UTC / 2026-11-09 JST
- 改訂: **r5（2026-11-09）。承認済み要件を維持し、段階3の共有runtime・SPP facade・SDP実機検証を反映。**
- 状態: **段階2のホスト検証、段階3の実SDK統合、UUID接続・複数候補・2 routeのAPI経由実機試験を完了。長時間・同時音声の検証は未完。**
- 文書パスは既存参照を維持するため据置き。前版のライブラリ内汎用プロトコル提案は現行方針ではない。

## 現在の進捗（2026-11-10追記）

上記の状態と次節は段階3時点の記録である。後続では二路SPPの負荷・再接続、A2DPとの同時通信、AVRCP基本target、三profileの約182秒共存、7種類の開発者サンプルまで実装・検証を進めた。
現在の結果と未確認範囲は[検証範囲](validation_jp.md)、利用方法は[開発者サンプル](developer_samples_jp.md)を参照する。
有限時間・固定peerでの成功を、任意の長時間運用、全相互接続、遅延・転送量の保証にはしない。過去の失敗試行や段階ごとの未確認項目は履歴として残す。

## 段階3の実装状況（2026-11-09追記）

実SDK統合とpublic `spp::Client` を追加した。現行契約・SDP event形式・取消drain・
MTU/RAM更新は [SDK統合記録](spp_sdk_integration_jp.md) を参照。
以下の段階2のMTU256とEngine15304 byteは当時の構成による履歴値である。
現在はMaxFrame1024、outgoing MTU1019、ARM Engine28616 byte。
標準UUID/custom128の未知channel解決と各1537-byte exact echoは実機PASS。
同一UUIDの2候補列挙・選択前1秒接続なし・route0選択後echoも確認した。
初回の追加route1はL2CAP pool3不足status86で終端停止。診断pool6で再試験し、
両routeの明示選択・Open・各1537-byte exact echoを含む全3 trialがPASS（約12秒）。
core errorとtrace dropは0、postflightで最終ACLなし・永続bondなしを確認。
A2DP/SPP同一targetの実SDK linkも成功したが、実機同時audioは未確認。
共用configはHCI1028へ明示統一し、A2DP-onlyの1695は変更しない。
詳細・保存した原traceへのリンクは上記SDK統合記録を参照。

## 1. 決まった方向性とr2からの差分

### ユーザーの最新方針

**ライブラリは素のSPPとして使える形を提供する。まず制御用とデータ用の2ルートをアプリが使い分け、データ路の中ではアプリのID/長さ等で複数種類を多重化できる。フレーミング、データ識別、間引き、優先、音声形式、制御の意味はアプリが決める。**

- 初版の相手は1台。接続開始・受入の両対応という承認済み方針を維持する。
- 同じ相手との**複数SPPルート**を設計対象にする。最新方針では2本の負荷・双方向実測を優先し、4本は必要性が出た場合のオプション。別のBluetooth機器を増やす意味ではない。
- ユーザーの最新要件目標は**同じ相手に少なくとも2本のSPP路を同時に使えること**。相手も2受付口/接続を扱えることが前提。4本は必須にしない。保証するAPI範囲・資源条件・試験範囲は実測後に要件案として示し、制御遅延や無制限な転送量まで含む保証とはしない。
- 共有runtime、倉庫番方式、SPP→A2DP回帰→共存の段階検証を維持する。
- BluePicoは利用例。ライブラリに「音声ルート」「制御ルート」という固定の意味や番号を持たせない。

### 前案から外すもの

- ライブラリ独自の長さprefix、magic、version、CRC、type、sequence、Hello交換。
- ライブラリによるSBC/PCM/ボタンの識別やメッセージ単位の扱い。
- ライブラリ内のExpireBeforeSend / ReplacePending、媒体期限、アプリ優先度の判断。
- 音声・制御を必ず一つのRFCOMMで多重化する前提。

アプリは必要なら「長さ＋中身」等の最小箱を自分で設けてよい。その箱をライブラリが付加・検出・二重化しない。アプリ用の見本を将来用意する場合も、SPP本体の契約とは分ける。

## 2. SPP・RFCOMM・別ルートの意味

SPP（Serial Port Profile）はRFCOMMを使うシリアル接続のprofileであり、音声やボタンのアプリプロトコルを規定しない。**アプリケーションが通信内容を決めてよい。**

「別RFCOMM」は、この設計では**同じ相手へもう一本、独立したSPPのバイトストリームを作る**という意味。異なるRFCOMM server channelへ接続して、それぞれの接続cidを持つ。アプリは例えば一方を音声用、もう一方を制御用にできる。

| 用語 | 意味 |
|---|---|
| 接続先アドレス | 同じBluetooth相手を指定する |
| RFCOMM server channel | 相手の受付口。複数ルートには異なる受付口を用意する |
| RFCOMM cid | 接続後にBTstackが割り当てるローカル識別子。server channel番号とは別 |
| ライブラリのroute/session handle（名称未確定） | アプリがルートを指定するための識別子。cidの再利用に備え世代を含める案 |
| central / peripheral | 無線上の役割。接続開始側/受入側やルートの用途とは別 |

議論では「制御」「再生音声」「マイク音声」「ボタン」の4ルート案も出たが、最新方針は**制御を別SPPにし、データ用SPPへ複数種類をアプリの形式で載せる2ルートを先に検証する**こと。各SPPは双方向byte streamなので、同じデータ路で母艦→Picoの再生相当とPico→母艦のマイク相当を扱える。ただし成立・性能の実測は必要である。

IDは内容の識別、length等は境界復元に使えるが、それだけで順序待ちや優先度は解決しない。データ路に載せたボタンはその方向の先行データを追い越せない。遅延が重要なボタンを制御路へ載せるか、別ルートにするかもアプリの判断となる。制御用ルートを別確保することを、共有ACLを含む遅延保証と同一視しない。

SPP経路は接続中に順序付き・信頼性のある転送を提供し、無線エラーへの再送はBluetoothの下位層が扱う。一方で、write/readの境界一致、相手アプリの処理完了、切断を跨ぐ配達、一定時間以内の到達は保証しない。

**2ルートでも2本の独立した無線回線ではない。** 同一相手のRFCOMMチャネルはmultiplexer/L2CAP/ACL、controller、電波の時間を共有する。片方のアプリストリーム内の順序待ちとは分離できても、共有層の混雑・再送・帯域不足は残る。低遅延保証は実測前にしない。

## 3. 既存設計と根拠

- [検証範囲](validation_jp.md) §2〜5: State / Event / Rule / Action / Engineを分離する。判断は純粋な規則、副作用は実行層、観察は変更しない。
- [A2DP遷移表](connection_state_transitions_jp.md): callback正規化、再入の直列化、有界履歴、未実測と実測の区別。
- [検証範囲](validation_jp.md): 相手1台・RFCOMM1本で、接続開始/受入、無線central/peripheral、両方向36byte一致は実測済み。追加した2ルート・限定負荷の結果は§11。飽和、長時間、共存の性能は未実測。
- [現行A2DP](../examples/A2DPSourcePlayer/README_jp.md)はCYW43/BTstack/L2CAP/SDP/電源を専有する。別のSPPクラスを独立beginして共存させない。
- 音声パイプライン構造を今回変更しない。SPPは音声専用ITransportへ無理に押し込まず、独立した双方向byte APIを持つ。
- [HFP非対応](HFP_UNSUPPORTED_REASON.md)とFastStream保留を維持する。SPP追加はLBSマイク対応の証明ではない。

## 4. 責務分担

### ライブラリが担当するもの

1. **共有Bluetooth runtime**: 初期化、poll、GAP/security、L2CAP、SDP、共通イベント、電源の一元所有。
2. **複数SPPルートの接続管理**: サービス登録、接続開始/受入、相手・受付口・cid・世代・MTU、ルート別切断、エラー。
3. **素のbyte I/O**: ルートを指定したread/write、送信許可、MTU分割、有界buffer、受信credit、満杯/未接続/失敗の明示。
4. **観察口**: 各ルートの状態、キュー水位、転送数、エラー、無線役割、遷移履歴。payloadの意味は見ない。

### アプリが担当するもの

- 各ルートを音声・制御・その他のどれに使うか。
- ルート内のフレーミング、type、version、形式交渉、SBC/PCM、ボタンの意味。
- 送るデータの選択、優先、送信前の間引き、品質/生成量の調整、再生待ちの管理。
- 受信/実行確認が必要な場合のACK、request ID、再試行、冪等性。

ライブラリは接続とbyte転送の上限を守る。アプリのデータを「古い音声だろう」と推定して捨てない。アプリに戻す判断を隠れたbuffer policyとして実装しない。

## 5. 倉庫番の状態・遷移案

### 状態

詳細な正本は [SPP 遷移表](spp_state_transitions_jp.md)。runtime の begin / 初期化失敗、共有 SDP の要求 / 候補 / 完了 / 失敗 / timeout / 取消、route の選択待ちを含む。

共通runtime状態の下に、容量固定のルート状態配列を持つ。各ルートの接続意図、対象アドレス/受付口、cid、世代、MTU、送信許可、TX/RX占有量、offset、最後の結果を正本に集める。音声/制御の分類・メッセージ期限は持たない。

payloadは容量固定のストレージに置き、State/履歴には所有権と位置・長さを記録する。巨大な音声byte列を履歴へ毎回複製せず、callbackの一時ポインターも保持しない。

### 正規化と副作用

callbackはEvent化と有界領域への搬送まで。RuleはState/Eventから次StateとAction列を返し、BTstackや外部時計を直接読まない。Actionの同期callbackは現遷移の確定後へキューする。SDKのCAN_SEND_NOWは要求関数の呼出し中に発火し得るため、この再入規則は必須。

| 条件 / Event | 次状態 / Action案 | 検証範囲 |
|---|---|---|
| Begin / InitResult / 初期化 timeout | runtime を Stopped → Starting → Ready または Failed。snapshot の error/status と履歴で観察 | 段階 2 ホスト検証、実 SDK 未結合 |
| Discover / ConnectUuid(peer, UUID) | 共有 SDP を Querying。候補列挙後、唯一候補なら Connect、複数ならアプリの選択待ち | 標準 0x1101 / 128bit のホスト検証、無線検索は未実測 |
| SDP 失敗 / timeout / CancelDiscovery | 明示結果。取消後は client 静止確認まで再利用しない | 段階 2 ホスト検証 |
| ConnectRequested(route, peer, channel) | 指定routeをConnecting、RFCOMM接続要求 | 独立probeで固定channelの2本開始を実測 |
| ListenRequested(endpoint) | サービス登録・受付開始 | 独立probeで2口の受付を実測 |
| Incoming、許可対象かつ空きあり | routeを割当て、Accept | 独立probeで相手固定2本を実測 |
| Incoming、対象外/上限超過 | Reject、理由を記録 | 保護案 |
| Open(status=0) | 該当routeのcid/MTUを保存、Open | 独立probeで2本同時Openを実測 |
| Open失敗 | 該当routeの結果を保存、他routeを変更しない | 1本のtimeout/security失敗は実測 |
| WriteAccepted、許可未要求 | 該当routeの送信許可要求 | 2ルートの限定echo bufferは実測、汎用API/満杯は未実測 |
| CanSend、route世代/cid一致 | MTU以内のbyte片をSend | 独立probeで2ルートの小payloadと限定負荷を実測 |
| SendResult | offset/数/失敗を更新、後続byteは順序維持 | ローカル成功と相手の処理を区別 |
| RxBytes、領域あり | 該当routeの受信領域へ保存 | 分割/連結/満杯は要検証 |
| ReadConsumed | 実際に空いた領域に対応するcredit付与 | manual creditは要検証 |
| DisconnectRequested(route) / DLC Closed | 該当routeだけ失効、未完byte数/理由を通知 | 他routeを維持する動作は未実測 |
| ACL切断 / runtime異常 | 影響する全routeを失効、世代更新 | 複数routeで要検証 |
| TX 空き 0 → 正数 | writable_epoch を更新、履歴の writable_mask に通知。snapshot は現在の空き byte 数 | 段階 2 ホスト検証 |
| 古い世代・無関係cid | 状態を進めず拒否/観測 | cid再利用の保護案 |

接続開始/受入と無線役割は別項目にする。SPP1本の切断でHCI電源OFF、全GAP接続切断、A2DP停止を行わない。重要イベントのqueue溢れは明示的な異常であり、無言で正常運転を続けない。

## 6. API候補（名前・signatureはレビュー後に確定）

見本は設定→runtime begin→接続開始/受付→main loopのread/write→状態照会の順に読める形にする。

- runtime: 一度のbegin/poll。初期化・電源の所有者を一つにする。
- endpoint登録: peer方針、server channel、容量を指定。用途名はアプリのローカル名に過ぎない。
- connect / listen / disconnect: 明示操作。自動再接続は初版では行わない案。
- **tryWrite(route, bytes)**: 部分受理ありで確定。受理 byte 数と結果を返す非ブロッキング API。先頭の受理範囲は呼出し中にコピーし、送信上の所有権がライブラリへ移る。元メモリは直ちに再利用できる。未受理の末尾はアプリが保持し、順序を守って再投入する。
- **read(route, buffer)**: 得られたbyte数を返す。送信時のwrite境界との一致を約束しない。
- snapshot / history: route ごとの接続状態、書込可能容量、受信 byte 数、未完送信、失敗を変更なしで読む。`txFree()` は現在の書込可能 byte 数。0 → 正数の変化は `writable_epoch` と履歴の `writable_mask` で観察でき、履歴が上書きされても epoch の比較で送信ペーシングを再開できる。

tryWriteのAcceptedは「ライブラリの有界送信領域へ受理」の意味。Bluetoothへの送信完了でも相手の処理成功でもない。結果候補はInvalidHandle、NotConnected、WouldBlock、Accepted、Failed。部分受理契約は確定済み。切断時の未完 byte は件数・理由として残し、ローカル送信成功と区別する。

ライブラリのbyte queueはFIFO。フレーム単位の破棄/置換APIは今回設けない。アプリがメッセージを複数writeで送る場合、途中まで受理された残りを順番どおり送る責任がある。disconnectでは未完byteを失効させて件数/理由を通知するが、接続を跨いで自動再送しない。

**peer の SDP 検索は必須機能。** 標準 UUID 0x1101 と 128bit カスタム UUID の両方から RFCOMM server channel を解決する。接続 API は `connect(peer, channel)` と `connect(peer, serviceUuid)`（解決 → 接続）の両方を提供する。検索だけの `discover(peer, serviceUuid)` と token 指定の `cancelDiscovery(query)` も非同期操作として観察する。ここでの名前は製品 facade の候補であり、段階 2 core は同義の Event を受け取る。

同一 UUID が複数 service に一致したら全候補を返し、ライブラリは一つを勝手に選ばない。アプリが候補を指定して接続を進める。初期固定容量は8候補で、超過は CandidateOverflow として明示的に失敗し、切り詰めた結果を唯一候補と誤認しない。

複数SPPサービスのUUIDはいずれも標準SPPになり得るため、UUID1101を見つけただけで用途を推定しない。識別方法はendpoint設定やアプリ契約に置く。汎用端末ソフトが複数口を自動的に使える保証もしない。

ペアリング/認証/暗号化/鍵保存の要件は共有runtimeの明示設定。実験probeの自動確認を製品へ無条件に持ち込まない。全操作は同一main loopから行う案で、IRQ/別コア利用は別仕様にする。

## 7. バックプレッシャーと遅延の境界

### 素のSPPでライブラリが守ること

- TX/RXは有界。満杯ならWouldBlock/受信credit抑制を使い、成功を返してbyteを黙って捨てない。
- RFCOMMのCAN_SENDとMTUに従う。routeごとの許可を別routeへ流用しない。
- manual creditはRFCOMM frame単位。既に許可したframe数×最大MTUと処理待ち領域を含めて受信容量を確保する。空きbyte数だけを見て過剰にgrantしない。
- ルート別のbufferと制御イベント用領域を分ける案。一方が受信を消費しないときに、もう一方の受信領域まで使い尽くさない。
- poll/実行層は一つのrouteが無制限に占有しない有界処理にする。これは低層の処理量制限であり、payloadの意味を判断したアプリ優先制御ではない。

### アプリが行う間引きと優先

アプリは未送信の音声/映像を適切な単位で間引き、制御ルートへの書込みを先に試せる。別SPPルートに分けることで、媒体ストリームの途中に制御フレームを挿入する必要はなくなる。

ただし、ライブラリやBTstack/controllerへ既に受理されたbyteを、アプリが安全に撤回できるとは約束しない。SPP内で部分送信中のアプリフレームの残りを捨てて次へ進むと、相手のparserを壊す。アプリは**渡す前に**破棄を判断し、小さい単位、短い送信待ち、過剰な先行投入を避ける設計にする。

同じACLの再送/HOLは残るため、2ルートでも「制御は必ず先に届く」「タイムラグなし」とは言えない。相手アプリの受信完了/実行を保証したいときは、そのアプリがACK等を用意する。受信後の再生待ちを捨てる処理は無線の使用量や既発生の遅延を取り戻さない。

## 8. 複数SPPのSDK対応と資源

### ソースで確認したこと

- BTstackのRFCOMMはserver channel/cidを区別するAPIを持ち、サービス登録・送信許可・credit・disconnectを個別に扱う。
- SDKの `rfcomm_channel_create_internal` は相手アドレスで既存multiplexerを探し、別DLCを生成する構造。同じ相手/同じDLCの二重作成は拒否される。
- memory poolに `MAX_NR_RFCOMM_CHANNELS`、`MAX_NR_RFCOMM_MULTIPLEXERS`、`MAX_NR_RFCOMM_SERVICES` がある。
- `rfcomm.c` のcan-send配送は共通L2CAPに属するチャネルをリスト順に走査し、待機中かつcreditのあるチャネルへtokenを渡す。ここだけから公平性や制御優先の保証は得られない。負荷試験では再要求タイミング・作成順・先行投入量を明記する。
- 原単一probeの PicoSppProbe設定 は全て1。その原本は維持し、別の PicoDualSppProbe と負荷probeで2チャネル/サービスを確保して実測した。4本は試していない。

### 段階 2 の静的 RAM 予算（数値）

段階2当時の設定を2 route、各 TX/RX=1,024 byte、MTU 上限256 byte、受信 credit 上限4 frame / route、
queue16、履歴16、SDP 最大8候補とする。`arm-none-eabi-g++ 13.2.1 -mcpu=cortex-m0plus -mthumb`
の `sizeof` を object の symbol size から確認した。動的割当ては使用しない。

| 項目 | byte | 範囲 |
|---|---:|---|
| TX FIFO | 1,024 | route ごと |
| RX FIFO | 1,024 | route ごと |
| Route 状態・カウンター・8 SDP 候補 | 192 | route ごと |
| **route 小計** | **2,240** | **2 route=4,480** |
| State の共通部分（runtime、endpoint、共有 SDP、ACL identity） | 272 | 共有 |
| event queue（metadata72 + RX 一時コピー256）×16 | 5,248 | 共有 |
| payload を含まない遷移履歴312×16 | 4,992 | 共有 |
| SDK 送信用の一時連続 buffer | 256 | 共有 |
| queue 指標、カウンター、executor 参照、alignment | 56 | 共有 |
| **Engine 全体** | **15,304** | **2 route と共有領域の合計** |

これは core object の静的 RAM で、BTstack の RFCOMM service/channel/multiplexer、
L2CAP/ACL/SDP client、CYW43、adapter、アプリ buffer、stack は含まない。
製品全体の RAM 予算は段階 3 の設定と linker map / stack 検証で上積みする。
現時点で「2ルートの全 Bluetooth 実装が15,304 byteで収まる」とは言わない。
query / 履歴 / queue の容量変更は共有費用を変える。route 状態には64 byteの候補表が含まれる。

受信 credit は `min(4, floor((RX容量−未読byte)/MTU))` と既発行分の差だけ付与する。
各 route の4 frame windowと control 用8件を含む queue 下限をコンパイル時に検査する。
この4 frameは初期実装の容量方針であり、転送性能の保証やアプリ優先順位ではない。

### 本実装前にさらに確認すること

- 同じ相手へ2 DLCを開く設定・状態・SDPレコード寿命。2受付口を公開するならservice数も必要になる。
- RFCOMM channel pool、L2CAP/ACL資源、受信credit、TX/RX領域、履歴の合計RAM。設定値を上げるだけで性能が出るとは仮定しない。
- SDKのmultiplexer送信選択と共有キュー。一方の送受信停止がもう一方へどう影響するか。
- 相手側も二つのRFCOMM受付口/接続をアプリで扱えること。SPPを1本しか公開しない既存機器にライブラリが2本目を勝手に作ることはできない。

現在の評価は相手1台・2ルートを中心にする。4ルートを試す場合は、受付側の4 service/channel、各ルートのbuffer、共通multiplexer等の容量調整が必要となる。Nルートに拡張可能な容量設定を検討するが、4本を必須要件にせず、未測定の接続数を対応保証にしない。

## 9. A2DP・AVRCPとの共存

- 現行A2dpSourcePlayerのInitialize、static owner/claimed、生存期間を共有runtimeへ分離する案。独立したbeginを並べて所有者保護を外すだけにしない。
- profileの登録は共有初期化へ集約。l2cap_init/sdp_initや電源操作の二重実行を避ける。
- A2DPと各RFCOMMの送信許可・キューを分け、既存A2DPを回帰検証する。
- AudioPipelineはsingletonでconfigureが既存経路を差し替える。SPP媒体の追加で既存A2DP経路を上書きしない。音声bridgeはアプリ/別adapterの仕事で、今回無断でパイプラインを再設計しない。
- 旧BTStackAVRCPPluginはopcode解析がplaceholder。完成済みAVRCPとの同時動作を前提にしない。
- 同じ相手で複数profile、SPP母艦と別A2DPスピーカー、Wi-Fi併用は別の試験。現在のA2DP helperはCYW43_LWIP=0なのでWi-Fi共存も別途検討が必要。

## 10. 承認済みの要件と段階別検証

### 確定した境界と検証範囲

- 素のSPPを複数route handleで扱い、byte I/Oと状態観察だけを提供する境界。
- 相手1台・2ルートを中心に負荷・双方向動作を実測し、追加ルートが必要か結果から判断する範囲。
- 非ブロッキングtryWriteの部分受理、buffer容量/所有権、明示切断・未完了通知のAPI契約。
- 共有 runtime への変更を含む段階順序。段階 2 は承認済み。実 SDK adapter と共有 runtime の結合は段階 3 に分離する。

### 段階

1. **複数SPPの最小実測を追加（実行承認済み）**: 既存1本のPhase 0に、同一相手2本同時の証拠を足す。合成データで試し、まだ観測していない遷移を実証済みと扱わない。結果をユーザーへ示して要件を固める。
2. **遷移表とpure core（承認済み、ホスト実装済み）**: 実測イベントをState/Event/Ruleへ写す。API境界、世代、credit、queue、失敗と再入をホストで検証する。
3. **共有runtime・SPP API**: 単独/2ルートの見本を上から読める形にし、実SDKビルドとA2DP回帰を行う。
4. **API経由の実機比較**: 下表の同時性・分離性・負荷を確認。次にA2DP/AVRCPとの共存へ進む。

| 試験 | 合格の観点 |
|---|---|
| Pico開始で同一母艦2ルート | 両方Open、別cid、各routeの双方向byte列が一致。混線なし |
| 母艦開始でPicoの2受付口 | 両方受入。同様の双方向一致。開始/受入と無線役割を別記録 |
| channel 未知・UUID のみの接続 | peer SDP を検索し、標準 0x1101 / 128bit UUID の各々で解決 → 接続が成立する |
| 同じ UUID の SPP 系 service を複数公開 | 全候補がアプリへ返り、明示選択前には接続しない。候補上限超過も明示する |
| runtime 初期化失敗 / SDP 失敗・timeout | route Open 失敗と区別した状態・原因・履歴が残る。取消後の client 再利用は静止確認後だけ |
| 分割・連結・MTU跨ぎ | 任意のwrite/read分割でもbyte列の順序と内容が維持される |
| 一方の受信消費を止める | 該当routeのcredit/上限が働く。別routeへの影響と共有層の待ちを測定 |
| 片方だけdisconnect/reconnect | 他routeが意図せず閉じない。旧cid/世代の通知が新routeへ混ざらない |
| ACL切断/途中送信/queue満杯 | 全影響routeの状態・未完byte・エラーが一致し、無言の欠落なし |
| 合成媒体と制御の同時負荷 | アプリ側の確認返信で制御往復時間の分位点/最大値を測る。queue水位・転送量・CPU/RAMも記録 |
| 複数route＋A2DP | 独立試験結果から推測せず、音欠け・制御遅延・送受信数を組合せ実測 |

現在の2ルート負荷試験は、負荷なしとデータ路の合成媒体負荷ありを同じharnessで比較する。方向分離の追加比較では再生相当の母艦→Picoのみ、マイク相当のPico→母艦のみ、可能なら双方同時の順に流し、別制御路の確認返信往復と欠落/timeoutを測る。媒体をechoした試験は両方向負荷なので片方向試験とは呼ばない。未完了packet数を制限した条件と飽和負荷も区別する。4ルート試験は結果に応じたオプションであり、現行の必須試験ではない。

試験用の識別子や確認返信は**検証アプリ**のもの。ライブラリのwire仕様へ入れない。時刻同期を仮定せず同じ時計による往復測定を使う。フレーム長/生成量/先行投入量を変えて比較し、短時間の成功から長時間性能や動画実用性を保証しない。

ホスト試験にはroute別queue、容量境界、部分受理、無加工byte列、CAN_SENDの重複/遅延・同期再入、API失敗、cid再利用、世代違い、片route/全ACL切断を含める。実録音・再生を伴わず合成データで開始し、必要な録音は従来の事前合図に従う。

## 11. 現在地と参照実装

**方針と要件 1〜4 は承認済み。段階2のpure coreに続き、段階3のfacade・共有runtimeと限定実機試験を完了した。** 既存A2DPとHFP/FastStream保留を維持する。実測結果・保持ファーム・終了状態を別の証拠ファイルに保存した。

### 複数ルートの初回実測

[検証範囲](validation_jp.md)を保存した。Pico受入側ではchannel1/cid1・channel2/cid2、Pico開始側ではcid3/4の両方がOpen status=0となり、途中のClosedなしで共存した。両試験とも各ルートの異なる36byteを双方向へ送り、全方向完全一致、trace欠落0。両試験Picoの無線役割はperipheralであり、開始側だからcentralとは表示しない。

### 限定負荷の実測

[検証範囲](validation_jp.md)と生の比較結果を保存した。同じ接続で無負荷→負荷→無負荷を各約6秒、制御を各115往復して照合した。

| 条件 | 制御RTT中央値 | 95%点 | 観測最大 |
|---|---|---|---|
| 無負荷 | 8.155ms | 14.116ms | 26.116ms |
| データ路echo負荷 | 9.927ms | 17.412ms | 35.754ms |
| 負荷後の無負荷 | 7.724ms | 14.927ms | 31.906ms |

媒体相当は128byte×617往復、各方向78,976byteが完全一致（約13.14kB/s各方向）。制御は16byteを約50msごとに投入。各routeの未完了要求は1件に制限しており、**両方向echoによる限定負荷であって飽和試験・独立した片方向試験ではない**。timeout、buffer overflow、送信エラー、trace欠落はいずれも0。FW queue最大は128/16bytes。ELF text=357020、data=0、bss=54804bytesは静的section量であり、実行時peak RAMではない。

Pico無線OFF、母艦のdiscoverable/pairable/discovery OFF、ACLなし、USB/socket解放を確認した。保持イメージと復元先を記録している。

### 実測を踏まえた確定要件（1〜4 は文言どおり承認済み）

1. 同一相手に**少なくとも2本のSPP byte stream**を同時に扱う。接続開始/受入の両方を提供し、相手側にも2受付口/接続を扱えることを要求する。
2. 各ルートの送受信・状態・エラーを識別し、ライブラリがbyte列へ独自headerを挿入しない。データ多重化・音声・ボタン・間引き・優先はアプリ側。
3. 有界bufferと非ブロッキングI/O、満杯の明示、世代/cid分離、片route切断と全ACL切断の区別を正式APIの検証条件にする。
4. 今回の短時間値を遅延上限・最大転送量・リアルタイム保証にしない。fake-stack系列、MTU跨ぎ、片route切断/再接続、受信停止、長時間、A2DP共存は本実装の合格条件として別に検証する。

5. 部分受理 byte 数・コピーによる所有権移転・未受理末尾のアプリ保持を tryWrite の契約にする。TX の書込可能状態は snapshot と変更通知で観察できる。
6. SDP 検索は必須。標準 0x1101 と 128bit カスタム UUID に対応し、UUID 解決から接続できる。同一 UUID の複数候補はアプリへ返して明示選択を待つ。共有 runtime が SDP client を所有する。
7. runtime の初期化失敗と route の Open 失敗を区別し、SDP を含め要求・結果・失敗・timeout を遷移と観察へ正規化する。
8. 初期設定の route 静的 RAM は TX 1,024 + RX 1,024 + 状態 192 = **2,240 byte / route**。2 route 分は4,480 byte。共有 queue / 履歴 / SDP と SDK 側費用は別枠とし、詳細は §8。これは初期設定の予算であり、変更不能の容量保証ではない。

これらは承認済みの製品要件であり、未完成の製品 API を既に保証済みとは表示しない。短時間 probe、pure core ホスト試験、製品 API 実機検証の証拠を分けて扱う。

### 段階 2 の成果物

- [SPP 遷移表と adapter 契約](spp_state_transitions_jp.md)
- [SPP pure core](../spp-core/SppCore.h)
- ホスト試験と実行方法
- 段階 2 の検証記録

- [A2dpConnection.h](../audio-core/connection/A2dpConnection.h): pure規則、有界Engine、観察口。
- [A2dpSourcePlayer.cpp](../audio-core/A2dpSourcePlayer.cpp): 共通初期化・所有者分離の実際の境界。
- [AudioPipeline.cpp](../audio-core/AudioPipeline.cpp): singletonの共有経路。
- [ITransport.h](../audio-core/ITransport.h): 音声送信用interface。双方向SPPとは別。
- [BTStackAVRCPPlugin.cpp](../audio-core/plugins/BTStack/BTStackAVRCPPlugin.cpp): 未完成の旧AVRCP部分。
- [picow_a2dp_source.cmake](../cmake/picow_a2dp_source.cmake): poll方式・SDK依存・LWIP設定。
- PicoSppProbe: 既存の1ルート実証用。製品APIとは区別。
