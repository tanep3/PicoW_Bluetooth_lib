# A2DP Sink: 受信を止めずにPCMを消費する

[English](a2dp_sink_pcm.md) | [公開receiver例](../examples/A2DPSinkReceiver/README_jp.md)

既存receiver例は受理frame数を数えます。[上限付きPCM consumer例](../examples/A2dpSinkPcmConsumer/README_jp.md)ではアプリ所有領域へコピーし、同じmain loopで後から取り出す方法を示します。DAC/I2S出力や実時間playerは実装せず、既定binaryはBluetoothを開始しません。別の `SINK_PCM_USB_CAPTURE=ON` で明示的な5秒USB PCM録音を追加でき、既定はOFFです。[serial collector](../examples/A2dpSinkPcmConsumer/capture_usb.py)で `CAPTURE` を入力するまで録音しません。

## 借用入力とframe単位

`IAudioSink::write` はreceiverの `poll` 内で同期的に呼ばれ、BTstack media callbackや割込み内ではありません。`const int16_t*` の借用期間はその呼出しだけです。1frameは各channelの符号付き16bit sampleを1つずつinterleaved順に含みます。stereoのN frameは2N sampleであり、frame数はbyte数でも要素数でもありません。sourceの形式を固定と推定せず、callbackのsample rate/channel数を扱います。

保存できる先頭部分だけをコピーし、受理したframe数を `0..frames` で返します。未受理suffixはreceiverがdropとして数え、再試行しません。相手sourceへのbackpressure契約はありません。空き待ちでblockしたり、借用pointerを保持したり、一部しかコピーしていないのに全frame受理と報告したりしないでください。例はcallback外で上限付きchunkを取り出し、診断処理します。実時間audio output driverではありません。

## Stream境界と寿命

`reset()` はsession/format/start/stop境界でqueue済みPCMを破棄します。次streamの形式が同じでも前のsampleを無効として扱います。receiver・consumer・storageはプログラム寿命で保持し、操作は1つのloopだけで行います。IRQ/別coreから操作せず、`write`/`reset` 内でreceiver API、待機、無制限allocation、file/device I/Oを行いません。

実出力機器にはコピー/DMA所有権と完了の別契約が必要です。一時drain bufferを非同期hardwareへ渡すなら、相手がコピーするか十分な寿命のstorageを保持する必要があります。FIFOだけではresampler・遅延補正・underrun補完・clock同期は実装されません。

## 受信手順と現在の制限

`begin` が `Ok` を返した後もpollを続け、`snapshot().working` を待ちます。その後、設定peerからの着信を `listen` で許可するか、`connect` で1回の発信接続を要求します。ローカル受付はsignaling/configuration/streaming成功ではないためsnapshotとdiagnosticsを確認します。listenはshared connectabilityを有効化しますが全体discoverabilityではありません。pairing確認を暗黙に許可しないでください。disconnect/resetだけで非同期transportが直ちに閉じるわけではなく、ownerを破棄できるという意味でもありません。

公開受信実装は[拡張SBC設定](sink_sbc_configurations_contract_jp.md)の44.1/48kHz、mono/dual/stereo/joint、4種類のblock長、4/8 subbands、loudness/SNRに対応します。callbackのframe数は各channelにつき16–128、channel数は1または2です。consumerが広いPCM形式を受理しても無線codec対応は広がりません。全codec網羅・広いfacade相互接続・実スピーカー再生は未検証です。[SDK対応範囲](a2dp_sdk_coverage_jp.md)を参照してください。

## 検証範囲

公開 PCM consumer の統合版は実機未検証です。別アプリの短時間受信試験では、他形式・相手、長時間動作、元波形との一致や物理出力タイミングまでは確認していません。[検証範囲](validation_jp.md) に当時の結果を保存しています。

## 明示的なUSB録音例

公開例は8,192-frame FIFOを同じmain loopから512-frame単位で全drainします。`SINK_PCM_USB_CAPTURE=ON` ではmain-loop所有のTinyUSBとEP512/RX512/TX4096の搬送bufferを使い、full-speed USB packet自体は64 byteを維持します。固定packet領域とCRC／sequence／frame集計は `IAudioSink` の上にある試験・アプリ側処理で、decoderの変更やlibrary全体の無欠落保証ではありません。

`capture_usb.py --port <明示device> --output-dir <新規directory>` で利用者が指定した1つのserial portを使います。`l`／`c`／`?`／`d` は明示入力し、Sourceは本人が操作、次の5秒を録音してよい時だけ `CAPTURE` を入力します。機器探索・flash・Source制御はありません。完全な妥当記録は `received-5s.wav`、欠落を含む妥当記録は `received-INCOMPLETE.wav` と区別し、framing／CRC不正やEND欠落を正常録音にしません。serial portを閉じてもBluetoothは切断されません。準備・終了操作・依存・制限は[例の説明](../examples/A2dpSinkPcmConsumer/README_jp.md)を参照してください。

著作権のある録音・raw音声はローカル試聴用で、Git・公開・配布から除外します。共有証拠には統計とhashだけを含めます。

## 明示的な遅延報告（ホスト検証済み、実機受入は未完了）

`reportDelay100us(delay_100us, expected_generation)` は、固定相手の現在の
Sink セッションに AVDTP 遅延報告を一度だけ要求します。値は **100 µs 単位の
0..65535（0..6553.5 ms）**。範囲外は切り詰めず拒否します。0 はそのまま送る値で、
「不明」の特別値ではありません。アプリが受信から出力までの遅延を測定してください。
PCM 数からの自動推定、周期報告、自動再試行はありません。

測定時の `snapshot().generation` を測定値と一緒に保存し、同じ値を渡します。
切断前の測定値に再接続後の generation を付け直さないでください。
[コンパイル検証した使用例](../examples/A2DPSinkReceiver/DelayReportExample.h) は
µs 値を 100 µs 単位に切り上げ、範囲外を拒否します。`poll()` の後のメインループで
呼び、`write()` / `reset()` 内、IRQ、別コアからは呼びません。

- `Accepted`: SDK が要求を受け付けて送信待ちにしただけです。実送信、相手の承認、
  遅延補正の実施は保証しません。
- `InvalidDelay`: 値が範囲外。
- `StaleSession`: generation または現在の接続識別情報が不一致。
- `NotReady`: 未開始、失敗、切断中、未処理イベント、所有権不一致、遷移中など。
  この receiver 自身の poll / output callback 内からの再入と同一相手の共有
  シグナリングも拒否します。全呼び出しをメインループの callback 外で行うのは
  呼び出し側の責務で、他 profile の callback まで動的検出する保証はありません。
- `NotNegotiated`: SET_CONFIGURATION で遅延報告が選択されていません。
  capability の広告だけでは使用できません。
- `SdkRejected`: `sdk_status` に busy を含む SDK の非ゼロ値をそのまま返します。
  `sdk_status` が意味を持つのは `Accepted` / `SdkRejected` の場合だけです。

対象は安定した CONFIGURED / OPENED / STREAMING のみです。OPEN/CLOSE 等の遷移中や
endpoint 制御処理中は拒否し、シグナリング initiator の busy 判定は SDK に従います。
結果は戻り値を確認してください。この呼び出しは `last_operation_status` を含む
receiver の snapshot を変更しません。報告ごとの実送信／相手承認通知はありません。
実験ビルドでも同一相手の Source+Sink 経路は許可しません。実機互換性・遅延補正の
受入条件や既存 backlog は完了扱いにしません。
