# 実測値を明示して送る A2DP Sink 遅延レポート

[English](README.md) | [公開 API の契約](../../docs/a2dp_sink_pcm_jp.md)

`A2dpSinkReceiver::reportDelay100us` を実際に呼ぶ**アプリケーション入力経路**の
サンプルです。遅延測定器ではありません。`receiver.poll()` が戻った後のメイン
ループで、長さを制限した入力行を解析し、測定時の情報を確認して一度だけ呼びます。
PCM 受信、接続イベント、定期処理による自動送信・自動再試行はありません。

## 利用するアプリケーション側で用意するもの

`UnconnectedOutput` は DAC / I2S / レンダラーを持ちません。受け入れフレーム数に
0 を返し、PCM が破棄されたことを正しく集計します。録音・再生・実際の描画までの
遅延測定は行いません。**実際の出力パイプラインへ置き換えてから、その出力系の
実測遅延を指定してください。** [PCM 出力の注意点](../../docs/a2dp_sink_pcm_jp.md)も参照。

対象パイプラインに応じてデコーダ、待機 PCM、出力装置・実際の出力までを含む
遅延を測ります。FIFO の残量、フレーム数、推定の定数、このサンプルの処理時間を
総遅延の代わりにしてはいけません。入力数値の物理的な正しさは検証できません。
別の出力系や前の接続・ストリームの実測値は使わないでください。

測定開始時に receiver の generation と `OutputBoundary::epoch()` を取得し、
実測値と一緒に保持します。測定終了時にも確認し、どちらかが変われば破棄します。
`reset()` は epoch を更新するため、同じ generation の停止・再開・形式変更でも
古い測定値を拒否できます。独自の出力タイミング・設定変更でも boundary を reset
してください。これらは経過時間の期限ではありません。境界を跨がなくても測定値が
古くなったかどうかはアプリケーションで判断します。

## ビルド設定と安全な初期状態

既定は **radio OFF、peer 空欄**。stdio、receiver の構築、Bluetooth 初期化より前に
終了します。有効経路も型検査はされます。有効ビルドでも明示的な `begin` 入力まで
無線を開始しません。ペアリング確認許可は常に false です。自動探索・接続・再接続、
新しい永続ボンド保存、自動レポートはありません。新規ペアリング確認が必要な相手には、
別途検討したアプリケーションのペアリング方針が必要です。

インストール済み Pico SDK / ツールチェーン / picotool の CMake 設定を用意し、
リポジトリ直下から次のようにビルドします。検証時の SDK は 2.1.1 です。

```sh
export PICO_SDK_PATH=/path/to/installed/pico-sdk
cmake -S examples/A2dpSinkDelayReportSample -B build/sink-delay-default \
  -DPICO_SDK_FETCH_FROM_GIT=OFF -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_UPDATES_DISCONNECTED=ON -DPICOTOOL_FORCE_FETCH_FROM_GIT=OFF \
  -Dpicotool_DIR=/path/to/installed/picotool/cmake
cmake --build build/sink-delay-default --parallel 2
```

有効経路を**コンパイル**する場合は別のビルド先を使い、
`-DSINK_DELAY_ENABLE_RADIO=ON -DSINK_DELAY_PEER="YOUR_SOURCE_ADDRESS"` を追加します。
peer は許可された送信元の実際の 6 オクテット・アドレスに置き換えます。有効時の
空欄・全ゼロ・ブロードキャスト・不正な書式は構成時に拒否します。
ビルドだけでは書き込みや実行は行われません。検証用の架空 peer を含むファームウェア
を実機で動かしてはいけません。

## Pico W への書き込み後の操作

各コマンドは LF または CRLF で終わる ASCII の一行です。前後の空白は不可、
大文字小文字を区別します。

1. `begin`: プロセス全体で初期化を一度だけ試行。失敗時は再初期化せず、receiver /
   出力の寿命を維持して poll を続けます。
2. `status`: `working=1`、失敗なしを確認。その後 `listen` で固定 peer からの接続を
   許可するか、`connect` で一度だけ発信します。ローカル受付は接続・配信完了ではありません。
3. `status`: generation、output_epoch、eligible、接続状態を表示します。このサンプル
   は配信中だけ測定値を受け付けます。公開 API の CONFIGURED / OPENED / STREAMING
   より厳しい条件です。送信時に新しい generation を当てはめず、必ず実測時の情報を保持します。
4. `report <id> <measurement_generation> <measurement_output_epoch> <actual_measured_us>`。
   山括弧部分は説明用で、実際には数値を入力します。4 フィールドは 0..UINT32_MAX の
   符号なし十進整数。ID / generation / epoch は非ゼロ、実測 µs は **0..6553500**。
   真の実測 0 は有効であり、「不明」の意味ではありません。上限確認後に `(us+99)/100`
   で 100 µs 単位へ切り上げます。オーバーフローや範囲内への丸め込みはしません。
5. `disconnect`: 切断要求と出力 epoch 無効化。切断完了まで poll を継続します。
   再接続は別の明示コマンドで行い、新しい generation / epoch の新しい測定が必要です。
   `stop` は切断要求に加え、このプロセスの begin / listen / connect / report を永久停止。
   `status` は利用できます。receiver の失敗でも処理を停止します。

入力は途中の一行だけ保持し、アプリケーション側のコマンド・レポート待ち行列は
ありません。LF を除き最大 64 バイト（末尾 CR を含む）、一ループ最大 32 回の入力と
完結一行まで。不正文字、制御文字、数値の桁溢れ、不足・余分な項目では送信しません。
長すぎる行や不正行は LF まで読み捨て、切れた先頭・末尾を別コマンドにしません。
未完行は LF まで保持するだけです。入力途中に generation / epoch / 利用可否が
変わると、その report を拒否します。通信側に残った完結行も読み出すたびに元の測定
情報を確認するため、切断・reset 後の新しい接続へ自動的に付け替えることはありません。

report の ID はプロセス全体で 1 以上から増加させます。構文が正しい新規非ゼロ ID は、
範囲・状態・API の成否確認より**先に消費**します。busy や拒否でも同じ ID とそれ以下は
再送不能。不正な構文と ID 0 は ID を消費しません。UINT32_MAX は一度だけ利用でき、
巻き戻し・リセットはありません。epoch 上限到達後の reset は永久無効化します。
再入呼び出しは入力も API も実行しません。制御コマンドには report ID がなく、ID を変更しません。

ID と epoch は**プロセス内限定**で、再起動を跨ぐ永続的な再送防止・実測値の鮮度保証
ではありません。再起動時は外部入力バッファを破棄し、保存した測定コマンドを流し直さないで
ください。送信元認証や、偶然別の有効行になる通信の欠落・破損の検出も行いません。
必要な用途では認証・フレーム整合性を備えた入力方式を別途検討します。

## 結果の読み方

ローカルの構文・状態拒否では API を呼びません。`attempted-once` は公開 API を一度
呼んだ意味であり、BTstack 呼び出しまで到達したとは限りません。

- `Accepted-local-SDK-admission-only`: ローカル SDK の受付・キュー投入のみ。
  実際の送信、相手の ACK、出力完了、遅延補償の証明ではありません。
- `InvalidDelay` / `StaleSession` / `NotReady` / `NotNegotiated`: 公開 API 側で拒否。
  所有権、実際の ACL / endpoint、未処理イベント、交渉済み機能の追加検査は API 側が行います。
- `SdkRejected-no-retry`: busy を含む SDK の正確な戻り値を表示。成功ではなく、自動再試行しません。

SDK status は Accepted / SdkRejected の場合だけ表示します。再試行が適切と判断した場合も、
状態と測定値の適切さを確認して新しい ID で明示してください。busy の定期再送や、境界を
跨いだ測定値の付け替えはしないでください。

出力コールバックでは出力の状態処理だけを行い、入力解析・ログ・receiver API 呼び出し・
遅延測定や送信を行いません。すべての receiver 操作は単一メインループで、コールバック・
IRQ・第 2 コアの外から実行します。USB 出力の待ち時間設定は 0 でログが欠落し得ます。
ログ欠落は再送許可ではありません。入力処理量が有限でも**実時間の poll 期限を保証しません**。
stdio、ライブラリ処理、置き換えた出力系の時間検証は別途必要です。

## 検証範囲

```sh
bash tests/host/run_sink_delay_use_flow_tests.sh
```

モデル reporter を使い、入力・単位変換・世代境界・重複拒否・無再試行をホストで検査します。実機実行は **NOT_RUN** です。実測精度、相手への到達、遅延補償、機器互換性はこの検査では確認できません。[検証範囲](../../docs/validation_jp.md)を参照してください。
