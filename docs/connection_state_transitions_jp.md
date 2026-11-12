# 接続状態・イベント・副作用の対応（Phase 1）

**過去のSource状態観測記録です。当時の範囲とログを以下に保持します。**

[English](connection_state_transitions.md)

根拠は 2026-11-07 LBS実機記録、
特に device-03 と device-04（sequence 1815以降）の連続した USB 記録。これは A2DP Source 1台の観測範囲である。
Bluetooth全体、AVRCP、HFPの一般的な状態機械を先に作らない。

## 状態と観察

接続管理の正本は1つの State。

- 電源: off / starting / working。
- inquiry: idle / active / stopping。見つけた事実は接続済みを意味しない。
- 対象: アプリから渡した Bluetooth address。別アドレスは接続対象にしない。
- pairing: 未観測 / 要求応答済み / 完了（statusを保存）。
- A2DP signaling: idle / connecting / connected、cid。
- codec: 未設定 / 受諾済みの48kHzまたは44.1kHz、stereo、16blocks、8bands、loudness、bitpool。
- stream: idle / configuring / open / start_requested / playing。
- 送信許可: 未要求 / 要求中 / 1 packet分あり。playingとは別の事実。
- 時刻: Tickから渡す単調時刻、開始待ち・次の送信・短時間試験の終了期限。
- 接続意図: 明示connectまたは設定したauto_connect。disconnectで撤回し、遅れて成立した接続を再生へ進めない。
- 最終アクションの結果、送信累計、エラー。ログの欠落は観測系の状態として別に示す。

TransportのreadyはStateからの投影。接続済みと送信可能を同じboolにしない。
QueryはStateと履歴をコピーするだけで、Bluetooth APIや状態変更を行わない。

## 観測した遷移

| 前状態・条件 | 正規化Event | 次状態 | Action（データとして返す） | 根拠 |
|---|---|---|---|---|
| 電源off | PowerRequested | starting | PowerOn | seq2〜6 |
| starting | PowerWorking | working | なし | seq51〜52 |
| working、接続なし、inquiry idle | DiscoveryRequested | inquiry active、found=false | StartInquiry | seq54 |
| inquiry active、別アドレス | DeviceSeen | 変更なし | なし | seq57〜58 |
| inquiry active、指定アドレス | DeviceSeen | found=true、inquiry stopping | StopInquiry | seq61〜64 |
| inquiry停止完了、found=true | InquiryFinished | inquiry idle、signaling connecting | EstablishA2dp(target) | seq65〜68 |
| 接続途中、指定相手 | ConfirmationRequested | pairing要求応答済み | ConfirmPairing(target) | seq114〜115 |
| 接続途中 | PairingFinished(status) | pairing結果を保存 | なし | HCI pairing complete |
| signaling connecting、status=0 | SignalingEstablished | signaling connected、cidを保存 | なし | seq144〜145 |
| 相手能力受信 | CapabilitiesObserved | 観察記録のみ | なし（選択は現状BTstack A2DP層） | seq154〜166/WIRE18〜24 |
| SET_CONFIGURATION相手受諾 | CodecConfigured | codec設定を保存 | ConfigureAudio(codec) | WIRE25/seq170〜171 |
| media接続成功 | StreamOpened | stream configuring | ConfigureTransport | WIRE33/seq190〜192 |
| transport設定成功 | TransportConfigured | stream open、開始期限=now+設定値、payload limit保存 | なし | 設定API結果をExecutorから明示 |
| stream open、開始期限到達 | Tick | start_requested | StartStream(cid,seid) | seq208〜210 |
| START相手受諾 | StreamStarted | playing、次送信期限=now、終了期限=now+3000ms | SetTransportStreaming(true) | WIRE39/seq212〜213 |
| playing、期限到達、許可未要求 | Tick | 許可要求中 | RequestCanSend | 最初のmedia送信直前 |
| playing、要求に対応する通知 | SendCreditGranted | 1 packet分許可あり | GrantTransportCredit | A2DP CAN_SEND |
| playing、許可あり | Tick | 許可消費、送信結果待ち | PumpAudioOnce | WIRE40〜42 |
| media送信APIの結果 | MediaResult | 成功数・samplesを更新し次期限を512/rate秒進める、またはエラーを記録 | 失敗時は送信停止（自動再試行なし） | STATE累計／失敗枝は未実機検証 |
| playing、短時間試験期限到達 | Tick | playing終了、許可クリア | SetTransportStreaming(false), DisconnectA2dp | seq932〜936 |
| stream切断通知 | StreamReleased | stream idle、許可クリア | SetTransportStreaming(false) | seq939〜940 |
| signaling切断通知 | SignalingReleased | signaling idle、cid=0、codec未設定 | ClearConnection | seq943〜944 |

正常な signaling 解放後に明示的な DiscoveryRequested を再度受け、同じ手順を繰り返す経路は
device-04 sequence 1815〜2699 で確認した。接続固有の codec / credit / 期限 / pairing 応答フラグを
クリアする。自動再接続は行わず、明示した接続意図のない着信は再生へ進めない。

開始待ち4秒・終了3秒は**観測用アプリの方針**。エンジン内部の普遍的Bluetooth規則として固定しない。
アプリが待機時間・試験長をState/設定データで指定する。音源と出力先の設定も見本側で読めるようにする。

## 生イベントの説明範囲

- Inquiry、pairing/security、A2DP codec/open/start/releaseは上表へ正規化する。
- HCI初期化のCommandComplete/CommandStatus、transport packet sent、completed packets、
  feature/version/role/security通知は、低層BTstackの事実として履歴へ残す。
  これだけで接続管理のStateを進めず、独自の接続副作用を追加しない。
- SDP照会・L2CAP設定・AVDTP Discover/GetCapabilities/SetConfiguration/Open/Startの生往復は
  BTstackが処理している。Ruleはその処理を再実装せず、受諾通知とraw traceを対応づける。
- WIRE36〜37のAVRCP PSM0017要求/未対応応答は観測のみ。AVRCP成功のStateは作らない。
- 未知のイベントは Observed として履歴へ残し、状態・副作用を変えない。黙って成功扱いしない。
- 送信payload全件は採取せず最初の3packetだけ。累計と媒体内容の観測範囲を区別する。

## 未観測の入力・安全境界

pause/resume、失敗status、途中切断の完全な規則は、この成功ログだけから「検証済み」にしない。
正常解放後の明示再接続のみ追加実測済みであり、失敗からの自動再試行は含めない。
実装では未対応・不正な要求を明示的に拒否し、エラー結果を記録して自動再試行しない。
必要なイベントを追加観測したら本表とホスト試験を更新する。
HFPは追加しない。AVRCPは実測済みの「未対応要求」以上の成功動作を発明しない。

## 実装境界（Phase 2）

- Ruleは(State,Event)からTransitionを生成するだけ。外部時刻もBTstackも読まない。
- ActionExecutorだけがBluetooth API、Transport/AudioPipelineへの操作を行う。
- Engineはイベントキュー、規則適用、アクション実行、状態確定、履歴保存を担当する。
- BTstackがAction実行中に同期コールバックした場合、再入したEventはキューへ入れ、
  現在のTransitionが確定してから処理する。未確定Stateへ再入しない。
- アクションはTransitionの次Stateを明示的に受け取り、実行結果をEventとして戻す。
- Phase 0例は比較対象として保持する。旧plugin経路の全体修復とは区別する。

## 保護用の規則（ホスト試験、実機未再現）

次は成功ログから相手の正常動作を推測した規則ではなく、未対応の入力で発音を続けない保護境界。
実機検証済みとは扱わず、ホスト試験で入力列と結果を固定する。

- disconnect は電源開始中・探索中・接続成立待ちでも接続意図を撤回する。
  接続開始の API からまだ cid が届いていない場合、遅れて成立した cid を明示的に切断する。
- 指定外の address、接続意図のない着信、既存接続と異なる cid の成立は拒否・切断する。
- StreamSuspended は未対応として送信を停止する。自動 resume は実装しない。
- PIN pairing、相手による command reject、API の失敗は明示エラーと送信停止にする。
  SDK の A2DP COMMAND_REJECTED は AVDTP の個別エラー byte を公開しないため、
  command / cid / seid とローカル診断statusを残し、相手エラー値を捏造しない。
- 不明通知には raw event type / subevent / command の識別子を残す。鍵や認証情報は残さない。
- 再入イベントは先に実行中の遷移を確定してから処理する。キュー溢れは送信停止と欠落数で示す。
- 履歴は前後State、Event、生成Action列、sequenceを保存。単に時刻だけ進むTickで履歴を追い出さない。
  高速の送信関連履歴は有限容量で上書きされ得るため、上書き数と送信累計を別に観測する。
