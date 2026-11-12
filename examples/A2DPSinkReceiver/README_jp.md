# 公開 A2DP Sink / PCM 受け渡しサンプル

[English](README.md)

SBC 音声の受信と PCM の受け渡しを学ぶ例です。音声出力ドライバは含みません。対応範囲は[SDK 対応表](../../docs/a2dp_sdk_coverage_jp.md)を参照してください。

`A2dpSinkReceiver` がSBCを受信し、メインループの `poll()` からアプリの `IAudioSink` を呼びます。付属 `Counter` はframe数だけを消費します。DAC/I2S出力、マイク、録音はなく、実際の発音を証明する例ではありません。アプリの有界出力buffer/driverへ差し替えてください。

## ビルドと操作

Pico W、Pico SDK、CMake、ARM GCC を準備し、リポジトリのルートで実行します。[ビルドガイド](../../docs/build_guide_jp.md)も参照してください。

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S examples/A2DPSinkReceiver -B build/a2dp-sink \
  -DSINK_PEER_ADDRESS=00:11:22:33:44:55
cmake --build build/a2dp-sink --parallel 2
```

仮アドレスを接続対象のSourceへ置き換えます。既定の空peerでは無線を初期化しません。buildは書き込みを行いません。別途許可された書き込み後：

1. `b`：一度だけbegin。`?` で `working=1` を待つ。
2. `l`：指定peerからの接続を受け付ける。共有無線をconnectableにするがdiscoverableにはしない。代わりに `c` でそのSourceへ一度だけ接続開始。自動リトライなし。
3. 相手側で再生開始。`?` でsignaling/stream、交渉rate、復号/受付/破棄frame数を確認。
4. `d`：A2DP signalingを切断し、着信受付を止める。閉じるまで次のl/cを待つ。無線OFFやSPP/AVRCP停止はしない。

この例は指定peerのSSP confirmationを明示的に許可します。鍵はruntimeのメモリのみ。IO capability応答はSDK既定経路を使います。アプリが `ENABLE_EXPLICIT_IO_CAPABILITIES_REPLY` を定義する場合は対応する応答handlerが別途必要で、`allow_pairing_confirmation` だけではそのcallbackを実装しません。USB stdoutは待ち時間0・有界bufferとし、無線pollを止めるよりログ欠落を許容します。

## アプリとの契約

`A2dpSinkReceiver.h` をincludeし、`pico_sdk_init()` 後に `cmake/picow_a2dp_sink.cmake` の `picow_target_a2dp_sink(app config_directory)` で組み込みます。Settingsは0以外の6バイトpeerを1台指定。begin結果は `Ok`, `InvalidSettings`, `RuntimeAlreadyOwned`, `InitializationFailed`。Okは接続完了ではありません。

receiverと出力objectは一つずつ作り、プログラム終了まで保持します。すべて同じmain loopで呼び、IRQ/別coreでは使いません。pollは共有runtimeの処理後、コピー済み制御/mediaをcallback順で処理し、1回最大2media packetを復号します。BTstack media callbackから直接アプリcallbackを呼びません。出力callbackからreceiver変更APIへ再入しないでください。

`IAudioSink::write(const int16_t*, size_t frames, uint32_t rate, uint8_t channels)` は借用のinterleaved signed-16 PCMを渡します。frameは各channelのsample1個ずつで、現実装は有効SBC frameごとにblocks×subbands frames（16〜128）、1または2channelsです。戻る前に受付分をコピーし、0〜framesの受付frame数を返します。部分/0受付なら残りを破棄数として計上し、再試行しません。A2DPで相手Sourceへのbackpressure保証はできません。提示数を超える戻り値は不正として全frame拒否に数えます。

`IAudioSink::reset()` はstream/config/start/stop境界でアプリの未出力PCMを捨て、旧session音声が再接続へ混ざるのを防ぎます。ライブラリは録音せず、出力driverやresamplerも提供しません。

snapshot/diagnosticsは読み取り専用コピー。受信/処理packet、queue overflow/oversize/discard、sequence欠落/逆順、timestamp/SSRC変化、parse/decode失敗、復号SBC frame、PCM frame、受付/破棄PCM、不正なconsumer戻り値を区別します。parse_errorsの順序は None, Short, Rtp, Fragmented, Count, Codec, Bounds, Trailing。欠落や破棄を成功に読み替えません。

## 対応形式と制限

- SBCのみ：必須44.1/48kHz形式、全4modes、4/8/12/16 blocks、4/8 subbands、loudness/SNR、bitpool範囲2〜86と形式ごとのbitrate制限。完全frameと制限付きfragmentを受理します。[形式の契約](../../docs/sink_sbc_configurations_contract_jp.md)と[生設定の検証](../../docs/sink_raw_configuration_contract.md)を参照。
- mediaは1024バイト×8、controlは64バイト×16。media overflowは計数。control overflowはSinkを終端失敗させ、A2DPだけ切断し他profileの無線電源は切らない。
- 固定peer1台、着信または一度の発信。自動再接続・発信timeout方針は未実装。対象外A2DPは拒否するが他profileのACLは切断しない。
- 別peerのSource/Sink同時登録とraw AVDTP role gateは検証中。同peerはSDKが接続当たり一つの設定済みメディアstreamを持つため登録を暫定ガード中。破棄・再作成やrole切替による復旧もまだ非対応。
- CMake helperはインストール済みRP2040 linker配置で別領域32 KiB stackと、その下に制限したheapを確保する。配置が変われば停止する。普遍的な最大stackの実測保証ではないためconsumer callbackは有界にする。
- 旧Sink probeの実機結果を、この新facadeの動作保証へ自動的に読み替えない。新facadeのhost試験/cross-buildと、今後必要な実機受入を分ける。

## Host検査

[所有コピーとpartial受理を示す上限付きPCM consumer例](../A2dpSinkPcmConsumer/README_jp.md)。既定無線OFFで、再生driverではありません。

## 困ったとき

`working=1` にならない場合は peer 設定と begin 結果を確認します。接続しても frame が増えない場合は Source 側の再生と SBC 交渉状態を確認してください。Counter は音を出しません。音が必要なら出力 driver を実装し、部分受付・破棄数を監視してください。
