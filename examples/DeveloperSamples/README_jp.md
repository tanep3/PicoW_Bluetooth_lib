# 開発者向け: 小さなコードから使い始める

[English](README.md)

公開 API の使い方を学ぶ独立実行例です。各 `.cpp` が別々のファームウェアになります。A2DP は無音 PCM を使うため、音声出力装置なしで接続や操作の流れを確認できます。実機確認は限定条件の結果であり、音質や一般的な相互接続性の保証ではありません。

## 選び方

|実行 target / ファイル|学ぶこと|
|---|---|
|`sample_a2dp_source` / `a2dp_source.cpp`|`IAudioSource` → settings → begin → poll。手動接続、10 秒の無音 PCM、切断、状態確認|
|`sample_avrcp_target` / `avrcp_target.cpp`|受信専用 Target。操作 ID・押下/解放を観察、アプリ側で処理、terminal stop|
|`sample_spp_client` / `spp_client.cpp`|1 route の SPP。channel 指定 / UUID 検索、候補選択、送受信|
|`sample_spp_two_routes` / `spp_two_routes.cpp`|同じ peer の別 RFCOMM channel に 2 route。世代別の送信管理|
|`sample_a2dp_avrcp` / `a2dp_avrcp.cpp`|共有 runtime。AVRCP STOP をアプリで A2DP 切断に対応付け|
|`sample_a2dp_spp_avrcp` / `a2dp_spp_avrcp.cpp`|2 SPP 受付口 + 既存 ACL 上の A2DP + 受信 AVRCP。明示的に一度だけ audio 開始|
|`sample_spp_reconnect` / `spp_reconnect.cpp`|明示開始、最大 3 回、timeout enqueue後の約5秒待機、connect発行から10秒期限。無限リトライしない|

SPP の共通実装は `SppSample.h` にあります。各小さな入口で route 数と再試行ポリシーだけを切り替えます。まず 1 route を読み、その後同じコードの 2 route / retry 分岐を追ってください。プロトコル framing・route 優先度・echo はライブラリが決めません。

## ビルド

SDK の準備から始める場合は [ビルドガイド](../../docs/build_guide_jp.md) を参照してください。以下は環境を用意した後の短縮手順です。

Pico W、Pico SDK、CMake、ARM GCC が必要です。リポジトリのルートから:

```sh
export PICO_SDK_PATH="$HOME/pico/pico-sdk" # 自分の SDK の場所へ変更
cmake -S examples/DeveloperSamples -B /tmp/picow-samples \
  -DSAMPLE_PEER=12:34:56:78:9A:BC \
  -DSAMPLE_CHANNEL_0=1 -DSAMPLE_CHANNEL_1=2
cmake --build /tmp/picow-samples -j2
```

`12:34:56:78:9A:BC` は説明用です。必ず接続を許可した機器の実際の Bluetooth アドレスへ置き換えます。RFCOMM channel も相手のサービスの値へ変更してください。1 と 2 は予約済みサービスを意味しません。範囲は 1..30、2 route は異なる channel が必要です。同じ `spp::Client` から 2 台の相手へ接続する例ではありません。

`SAMPLE_PEER` の初期値は空文字です。未設定・不正・全ゼロ・全 FF のままでは begin しません。ビルドは可能ですが無線は開始しません。生成物は `/tmp/picow-samples/sample_*.uf2`。この手順・検査スクリプトは書き込みを行いません。実機で利用する場合は目的の UF2 **一つだけ**を書き込みます。

## コンソールと初回操作

標準は Pico の USB CDC シリアルです。接続した端末から `b` を送るまで begin / 無線 power-on を行いません。USB の起動表示は端末が間に合わないと失われるため、表示がなくても `b` を一度送り、以後 `?` で観察できます。USB 出力待ち時間は 0 に設定し、端末未接続・混雑時は診断を落とします。診断ログは完全記録ではありません。

UART を使う場合は `-DSAMPLE_USB_CONSOLE=OFF` で再構成します。UART0、GP0=TX、GP1=RX、GND 共通、115200 baud、3.3 V TTL。RS-232 電圧を直結しないでください。UART printf は送信待ちするので `?` を連打しないでください。どちらも 1 loop で 1 文字だけ読みます。リアルタイム製品では低頻度の status または bounded trace queue に置き換えてください。

- A2DP: `b` → `?` で power=2 (Working) → `c` → `?`。早すぎる `c` は起動待ちを表示し、後で手動で再入力します。`d` で切断。`c` は手動の 1 回で自動リトライしません。正常切断後は再入力できますが、A2DP error が None 以外なら同じ owner では回復せずリセットが必要です。
- AVRCP 単体: `SAMPLE_ALLOW_PAIRING=ON` でビルドし、`b` → Ready 後に `p` → 指定した Controller 側で機器検索・ペアリング・AVRCP 接続。`?` で最新 event の kind、operation、pressed、result。`x` は AVRCP の terminal stop。再開始にはリセットが必要です。
- SPP: `b` → `?` で Ready を確認 → `c` → `?` で Open を確認 → `w`。`w` は短い greeting を一度だけ送ります。相手側で受信を確認してください。
- SPP の `u` は route 0 に標準 SPP UUID `0x1101` で接続を要求します。複数候補で Selecting なら `?` で一覧を読み、candidate 0 が目的の channel の場合に限って `s` で選択します。他の index を使う製品では UI と `select(handle,index)` を実装してください。
- SPP の `l` は Pico 側に受付サービスを登録します。相手から設定した local channel へ接続します。`?` で endpoint の Listening/Failed と status、route の Open/error を別々に確認してください。listen の bool もキュー受理だけです。endpoint 番号と受信 route 番号は固定対応ではなく、着信は空いている route へ割り当てられます。snapshot の route.channel でサービスを識別してください。`d` は接続を切断しますが listener を削除しません。listener を止める公開 API はないため、この例ではリセットが必要です。発信と受付の両方を同時に要求すると衝突する可能性があるので、学習時はどちらか一方から始めます。
- `d` は SPP の接続/探索を切断・中止し、自動 retry モードを解除します。`r` は reconnect 例だけで有効です。

## ペアリングを明示的に許可する場合

SPP と AVRCP 単体はデフォルトで自動確認を許可しません。初回 SSP ペアリングをするには、指定相手が正しいことを確認して `-DSAMPLE_ALLOW_PAIRING=ON` でビルドし直します。固定した **その peer だけ**へ SSP 確認を許可します。SPP は `Client::Security{true,true}`、AVRCP 単体は `p` 操作と `PeerPairing.h` のアプリ側 HCI handler / runtime policy で実装しています。全例の `PeerPairing.h` が明示 IO capability request に応答し、許可した固定 peer 以外は拒否します。確認要求は共有 runtime の profile policy と照合します。認証要求は無効化しません。相手側も接続可能・必要ならペアリング可能にしてください。AVRCP は受信専用であり、この例自体が相手を検索したり、相手側に AVRCP 接続を開始させたりはしません。単体例は WORKING 後にアプリが connectable を有効にします。`b` だけでは discoverable/pairing は許可せず、明示コマンド `p` が最大 60 秒の window を開きます。期限到達・AVRCP 接続後・`x` で discoverable と自分の pairing 許可を閉じ、`x` では connectable も解除します。window 中の `p` 連打で期限は延長しません。固定アドレスで接続できる Controller は表示された Pico アドレスも利用できます。Controller 実装や接続性によって単体で接続できない機器があります。

**A2DP の既存 facade は別の仕様です**。`A2dpSourcePlayer::begin()` は設定 peer に対する SSP 確認許可を登録します。`SAMPLE_ALLOW_PAIRING=OFF` は A2DP のこの動作を無効化する設定ではありません。A2DP を含む例の `b` / `c` はこの動作も理解した上で実行してください。併用時は同じ peer の共有 policy に影響します。

Link key DB は RAM のみで再起動を越えて永続化しません。相手だけが古い bond を保持している場合、相手側で以前の登録を削除して再ペアリングする必要があります。旧式の PIN ペアリングは未対応。無差別な自動確認やセキュリティレベルを下げるフォールバックはありません。

## 状態・エラー・寿命

- 各 profile は process-lifetime owner を一つだけ作り、同じ main loop で `begin/poll` と API を呼びます。IRQ / 第 2 core / 別 thread から操作しません。PCM source と callback context は owner より長く生存させます。この例では static です。
- CYW43 / HCI / L2CAP / SDP の初期化は `BluetoothRuntime` に任せます。別の `cyw43_arch_init()`、独立 run loop、legacy plugin、background 型の旧ライブラリ target を混ぜません。各 facade の `poll()` が runtime を進めるため、併用例では両方を毎周呼びます。
- `begin()==true`、`connect()==true`、`tryWrite().bytes>0` は通信完了の証拠ではありません。SPP の bool はキュー受理です。Ready/Open、error/status、tx_accepted/tx_sent/tx_discarded、droppedEvents を分けて観察します。A2DP の begin 成功も接続・再生成功ではなく、Playing と packet count を確認します。
- SPP は `{slot,generation}` の handle を snapshot から取得します。再接続後に古い handle を再使用しません。未送信のアプリ message は切断や世代変更で破棄します。部分受理時は bytes だけ offset を進め、WouldBlock は次の poll 後に残りを試します。accepted は相手アプリの受領確認ではありません。
- SPP の read は最大 64 bytes / route / loop。この例は受信を消費して counters で観察するだけです。必要なデータ処理へ置換してください。RFCOMM は byte stream なので write 境界と read 境界は一致しません。長さ prefix / 区切り / ACK / 再送・優先順位はアプリ固有の要件に応じて実装します。
- AVRCP callback は intent と最新 event だけ保存します。`?` までに複数 event が来ると古いものは上書きされます。pressed=false は解放。`0x44` PLAY、`0x45` STOP、`0x46` PAUSE など。製品で押下保持を実装する場合は Disconnected/Reset で保持を解除し、存在しない解放イベントを捏造しません。
- 併用例は STOP の押下を A2DP disconnect に明示的に対応付けています。PLAY/PAUSE 等を自動的にプレイヤーへ転送しません。facade に存在しない pause/seek API は例示しません。playback status は Playing のときだけ playing とし、長さ/位置は未知値です。
- A2DP は signed int16、interleaved stereo、48000 Hz。`IAudioSource::read` の個数は int16 sample 数で stereo frame 数ではありません。例は無音のため音が聞こえなくても正常です。サンプルレートを変える際は PCM source と settings の双方を合わせます。resampler はありません。

### 主な状態の数値

診断は公開 enum の値です。A2DP power: 0=Off、1=Starting、2=Working。signaling: 0=Idle、1=Connecting、2=Connected。stream: 0=Idle、1=Configuring、2=Open、3=StartRequested、4=Playing。SPP runtime: 0=Stopped、1=Starting、2=Ready、3=Failed。route phase: 0=Closed、1=Resolving、2=Selecting、3=Connecting、4=Open、5=Closing。endpoint phase: 0=Disabled、1=Registering、2=Listening、3=Failed。error=0 は None。詳細は `audio-core/connection/A2dpConnection.h`、`spp-core/SppCore.h`、`avrcp-core/AvrcpTargetCore.h` の enum と、各 owner の `history()` を参照してください。

## 再接続の上限

reconnect 例の `r` は 1 boot で最大 3 回のキュー投入、各回connect発行から10秒の接続/セッション期限、timeout disconnect enqueue後の約5秒cooldownを持ちます。Open成立後10秒やsocket物理閉鎖後5秒ではありません。期限に切断を要求する**動作確認用ポリシー**です。無期限運用の推奨値ではありません。queue 拒否も回数を消費します。`r` 連打で budget は増えません。disconnect の非同期完了が得られず Closing に残れば次の接続を行いません。Failed runtime はリセットを要し、同じ owner の begin を繰り返しません。

相手の RFCOMM server と認証準備が必要です。この例には firmware watchdog がなく、`d` や試行回数の消費では無線電源を切りません。

## A2DP + SPP + AVRCP へ進む

単純な併用の土台は `a2dp_avrcp.cpp` です。SPP を加える場合も同じ runtime に `spp::Client` を一つだけ追加し、A2DP → SPP → AVRCP のように全 profile を毎 loop poll します。片方の待ちループで他の poll を止めないでください。複数 SPP route は `spp_two_routes` と同じ client の slot 0/1 です。

読みやすい三 profile の入口は `a2dp_spp_avrcp.cpp` です。`b` → SPP Ready 後に `l` → 両 endpoint が Listening になったら、初回発見が必要な場合は `p` → 相手から 2 channel へ接続 → `?` で Open を確認 → `c`。同じ peer の現 ACL の `{peer, epoch, handle}` を取得し、`connectOverExistingAcl` へ一度だけ渡します。受理 bool は接続/認証成功ではなく、Playing と error/status を観察します。ACL が消失/置換した場合や radio OFF 後はこの opt-in 経路は terminal でリセットが必要です。`c` を自動再送しません。`d` / AVRCP STOP は audio だけを切断し、SPP と radio は維持します。`w` は各 Open route に greeting を一度送ります。全 profile を処理する main loop を止めない設計を見てください。

この三 profile 例の `p` は発見可能にするだけの明示操作です。`SAMPLE_ALLOW_PAIRING=ON`、radio Working、両 listener Ready、固定 peer の ACL が未接続の場合だけ最大 60 秒開きます。連打では延長せず、固定 peer の live ACL、期限、radio/SPP 停止・失敗で閉じます。`?` の `discovery_window` で状態を確認できます。共有の connectable や A2DP/SPP の固定 peer ペアリング方針は変更しません。既存アドレスから直接接続できる相手は `p` 不要です。

三 profile の統合は [a2dp_spp_avrcp.cpp](a2dp_spp_avrcp.cpp) と [CMakeLists.txt](CMakeLists.txt) を参照してください。共有 SDP は単一のため、SPP の UUID 探索と A2DP の探索を同時投入しない設計が必要です。先に SPP が同じ ACL を持つ場合の A2DP 接続は通常の inquiry 接続と同じではありません。7 番目の例はそのための `connectOverExistingAcl` と `a2dp::AclBinding` を使います。handle 0 は有効で、active と非ゼロ epoch と peer の一致を確認します。単に `connect()` を繰り返す回避策は示しません。

PCM の受信には [A2DP Sink サンプル](../A2DPSinkReceiver/README_jp.md)を使います。音を出すには自分の I2S/DAC 出力処理が必要です。HFP と FastStream はこの学習用 API の対象外です。

## オフライン検査

```sh
PICO_SDK_PATH=/path/to/pico-sdk examples/DeveloperSamples/check.sh
```

peer parser の拒否、retry 回数/待機、SPP 世代変更、部分書き込みと WouldBlock 時の byte 重複防止、切断/世代変更時の残データ破棄をホストで検査し、続けて全 7 target を実 SDK で ARM ビルドします。無線、flash、ペアリング、マイク/スピーカーの実機操作は行いません。検査スクリプトの初期値も peer 未設定です。実通信のセキュリティ交渉・音質・到達性・再接続は別途確認が必要です。

## 診断で困ったとき

USB console は radio loop を待たせないよう stdout timeout=0 を維持し、CDC TX buffer を SDK 既定の 64 byte から固定 1024 byte に増やしています（各 USB target の buffer 増分 960 byte）。三 profile 例の `?` 一組を収めるためです。読み手が遅い場合は依然欠落し得るため、状態は `?` で再確認してください。UART build の buffer 設定は変更しません。

三 profile 例の route status は `accepted` がライブラリ内部TX queueへ受理された累計byte、`tx` がSDK送信成功の累計byteです。受理だけでは相手の受信を保証しません。greeting helper は `WouldBlock` と、未処理callbackがある場合の `Busy` で残りを保持し、次のmain-loop poll後に再試行します。世代変更・切断・fatal errorでは残りを捨てます。

この例の STOP はアプリから audio の切断だけを要求します。実機では、それに伴って AVRCP 接続も終了する場合があります。AVRCP が維持されるとは約束せず、SPP の接続・通信継続は別に確認してください。

各機能の確認状況と限界は[検証範囲](../../docs/validation_jp.md)を参照してください。
