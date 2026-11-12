# Sink START の明示的な許可・拒否

## 有効化と既定動作

`A2dpSinkReceiver` は相手から届く AVDTP START をアプリが許可・拒否できます。二段階の opt-in です。

1. `picow_target_a2dp_sink(target config_directory)` の後に `picow_enable_sink_start_decisions(target)` を呼びます。
2. `begin()` の前に `Settings::require_start_decision = true` を指定します。

`startDecisionsAvailable()` はビルド時の対応有無を返します。未対応のまま明示判断を要求すると `begin()` は runtime を取得する前に `FeatureUnavailable` を返します。設定の既定値は自動 START のままです。機能をコンパイルしても、設定が false の Sink と Source は従来の自動処理を維持します。同一実行ファイル内でも Source の START を保留しません。ただし、双方向・共有プロファイル全体や実機動作の保証ではありません。

SDK の `ENABLE_AVDTP_ACCEPTOR_EXPLICIT_START_STREAM_CONFIRMATION` だけを全体に定義しないでください。Source の動作も変わるため、生成 acceptor はこの未対応の組み合わせをコンパイルエラーにします。専用 CMake 関数でライブラリ側のマクロと一緒に設定します。既存の同一 peer 実験 overlay との同一 configure は非対応です。インストール済み SDK は変更しません。

## 利用契約

`poll()` の後、`pendingStart()` を読みます。`request_id != 0` なら、その `generation` と `request_id` の組をそのまま `decideStart(request, true)`（許可）または `decideStart(request, false)`（拒否）へ渡します。保留は一件で、無制限キューや SDK 内のアプリコールバックはありません。

単一メインループ専用です。`poll()`、PCM `write()`、`reset()` のコールバックから呼ばないでください。poll を続け、入力待ちで停止させないでください。自動許可、自動拒否、ローカル timeout、自動 retry はありません。アプリが速やかに判断するか disconnect する責任を持ちます。相手が先に timeout する可能性があり、ローカル受付だけでは相手がまだ待っていると証明できません。

- `Accepted`: SDK が送信予定として受け付けた状態です。送信完了・相手の受信・streaming の証明ではありません。許可後は通常の `STREAM_STARTED` による `snapshot().streaming` を確認します。
- `FeatureUnavailable`: ビルド時に機能がありません。
- `NotReady`: 未開始、再入、または未処理の状態変更があります。poll 後にまだ有効な要求か確認します。
- `StaleRequest`: 現在の START を表さない識別子です。古い判断に現在の CID を入れ直さないでください。
- `SdkRejected`: `sdk_status` に SDK のエラーがあります。「アプリが拒否した」とは別です。SDK エラーの場合も試行済みの要求は消費されます。disconnect して処理を取り消し、同じ token を retry しないでください。

許可・拒否どちらも SDK に受け付けられれば `Accepted` です。Boolean 引数がどちらの判断かを示します。`sdk_status` は `Accepted` / `SdkRejected` のみ有効です。判断 API は streaming を直接変更しません。

同じ can-send 通知の再通知で ID は更新しません。一方、同じ signaling session 内の suspend/restart や transaction label の再使用でも、新しい START には新しい ID が付きます。実 ACL epoch、session generation、SDK connection、所有 Sink endpoint、START transaction label、local SEID、SDK 待機状態を照合します。disconnect、stop/close/abort/release、設定変更、runtime 終了・失敗、SDK 待機状態の変更で要求は利用不能になります。イベントキューの取りこぼしやカウンター枯渇は fail-closed です。公開 snapshot が SDK callback より遅れても、古い判断を許可しません。

拒否は [AVDTP 1.3 §8.13.3/Table 8.30](https://www.bluetooth.org/DocMan/handlers/DownloadDoc.ashx?doc_id=260860) に従い failed ACP SEID と BAD_STATE を含めます。SDK の短い拒否形式を Sink 限定で補正します。既存の raw SBC/設定 guard とアプリ設定 veto 後の復帰処理は維持します。

## 利用例

`examples/A2dpSinkStartDecisionSample` は PCM を破棄するサンプルで、音声出力装置ではありません。既定は radio OFF・peer 未指定です。`SINK_START_DECISIONS=ON` で機能をコンパイルします。radio 有効ビルドには別途 `SINK_START_ENABLE_RADIO=ON` と明示的な `SINK_START_PEER` が必要です。ビルドは実行・flash の許可を意味しません。

相手の要求より前に `a`（以降の START を許可）または `r`（拒否、既定値）を選択します。要求到着後はその方針ですぐ判断し、相手の timeout 中に人の入力を待ちません。`b` は radio 有効ビルドだけ初期化、`l` は listen、`c` は connect 要求、`x` は disconnect、`s` は状態表示です。自動 listen/connect/reconnect はありません。一ループ一文字の入力と非ブロッキング USB 出力で処理を制限していますが、リアルタイム保証ではありません。

## 検証範囲

任意START判断はhost試験とARMコンパイルで確認しています。実相手の応答期限、手動判断の無線実行、Source相互接続、共有profile全体の動作は未検証です。pollを継続して速やかに判断してください。ローカルtimeoutや自動retryはありません。

[検証範囲](validation_jp.md)
