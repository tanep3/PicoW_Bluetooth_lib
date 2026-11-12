<a id="a2dp-sink容量固定の-pcm-コンシューマーと任意-usb-録音"></a>
# A2DP Sink：受信したPCMを処理する

[English](README.md) / [Sink API概要](../A2DPSinkReceiver/README_jp.md)

PCやスマートフォンからPico WへBluetoothで音声を送り、**受信量とピーク振幅をUSB端末で確認する**サンプルです。公開APIの`A2dpSinkReceiver` / `IAudioSink`で受けたPCMを固定長のFIFOにコピーし、メインループで取り出して処理します。

**スピーカー出力はありません。Picoから音が出ないのは正常です。** 最初の手順では録音せずに受信を確認します。必要なら、後半の手順で最大5秒をPCのWAVファイルに保存できます。マイク入力やAVRCP操作は行いません。

<a id="first-use"></a>
## 初回操作：録音せず受信を確認する

Pico W、USBデータケーブル、USBシリアル端末、Bluetooth ClassicのSBC音声を送れるPCまたはスマートフォンを用意します。[ビルド環境の準備](../../docs/build_guide_jp.md#prerequisites)を済ませ、リポジトリ全体を使ってください。

### ビルドして書き込む

[送信元のClassic Bluetoothアドレス](../../docs/build_guide_jp.md#peer-address)を調べ、Bash変数`SOURCE_BT_ADDRESS`に設定します。**PicoのアドレスやWi-FiのMACアドレスではありません。** `PICO_SDK_PATH`には用意したSDKの場所を設定し、リポジトリのルートで次を実行します。

```sh
: "${PICO_SDK_PATH:?Set this to your Pico SDK directory}"
: "${SOURCE_BT_ADDRESS:?Set this to the verified source Bluetooth address}"
cmake -S examples/A2dpSinkPcmConsumer -B .work/builds/sink-pcm-first-use \
  -DSINK_PCM_ENABLE_RADIO=ON -DSINK_PCM_USB_CAPTURE=OFF \
  -DSINK_PCM_ALLOW_PAIRING=ON -DSINK_PCM_DISCOVERABLE_LISTEN=ON \
  -DSINK_PCM_PEER="$SOURCE_BT_ADDRESS"
cmake --build .work/builds/sink-pcm-first-use --target a2dp_sink_pcm_consumer --parallel 2
```

`.work/builds/sink-pcm-first-use/a2dp_sink_pcm_consumer.uf2`を[BOOTSELで書き込み、USB端末を開きます](../../docs/build_guide_jp.md#flash)。今のファームウェアを残したい場合は、先にバックアップと戻し方を確認してください。この設定では**起動時に無線の初期化を開始**します。`b`や`START`は使いません。

### 接続して受信を確認する

命令は小文字の1文字、状態確認だけ`?`です。1つずつ送り、結果を確認します。

1. `?`を送り、`working=1 failed=0`を確認します。起動時の`Ready`を見逃していても、この表示で確認できます。
2. `i`でPicoのBluetoothアドレスを確認します。Bluetooth名は`PicoW Bluetooth`で始まり、アドレスを含みます。
3. 送信元のBluetooth設定を開き、Picoに`l`を送ります。60秒以内に対象のPicoを選んでペアリングしてください。接続を許可するのはビルド時に指定した送信元だけです。
4. 送信元の音声出力をPicoに切り替え、曲を再生します。`?`を間隔を空けて2回送り、`streaming=1`と、`delivered`・`accepted`・`drained`の増加を確認します。`failed=0`で、`dropped`・`rejected`が増えないかも見てください。無音なら`peak=0`になることがあります。
5. 終わったら`d`を送り、`?`で`closing=0 signaling=0 connecting=0`になるのを待ちます。`d`は無線OFFではありません。電源を外すか元のファームウェアに戻し、送信元の音声出力も戻してください。

ここで確認できるのはPCMの受信と処理です。`accepted`はFIFOへコピーした数、`drained`は取り出した数で、スピーカーで再生した数ではありません。

## うまく動かないとき

- **USB端末が出ない：** `SINK_PCM_ENABLE_RADIO=ON`で作ったUF2か確認します。OFF版はUSBシリアルも起動しません。
- **Picoが一覧に出ない：** `working=1 failed=0`を確認してから`l`を送ります。60秒で待受けが閉じたら、`?`で待受けと接続の終了を確認して、改めて`l`を送ってください。連打しても期限は延びません。
- **再起動後に接続できない：** 鍵はRAMにしか残りません。送信元のPicoの登録だけを削除し、ペアリングし直してください。
- **接続したがカウンターが増えない：** 送信元の音声出力先と再生状態を確認します。`listen accepted=1`や`connect accepted=1`は接続・音声受信の完了ではありません。
- **`failed=1`：** 状態を確認してください。この例は初期化の再試行や自動再接続をしません。`c`は利用者が指定する1回の発信接続です。

**この公開サンプルそのものはホスト試験・ARMビルドまで確認済みで、実機試験は未実施（NOT_RUN）です。** 別ファームウェアの試験結果とは区別しています。[検証範囲](../../docs/validation_jp.md)を参照してください。

## 任意の5秒 USB WAV 録音

受信を確認した後、録音が必要な場合だけ行います。録音してよい音源を使い、関係者にも伝えてください。同じ送信元アドレスを指定し、別のビルド先で録音機能を有効にします。

```sh
: "${PICO_SDK_PATH:?Set this to your Pico SDK directory}"
: "${SOURCE_BT_ADDRESS:?Set this to the verified source Bluetooth address}"
cmake -S examples/A2dpSinkPcmConsumer -B .work/builds/sink-usb \
  -DSINK_PCM_ENABLE_RADIO=ON -DSINK_PCM_USB_CAPTURE=ON \
  -DSINK_PCM_ALLOW_PAIRING=ON -DSINK_PCM_DISCOVERABLE_LISTEN=ON \
  -DSINK_PCM_PEER="$SOURCE_BT_ADDRESS"
cmake --build .work/builds/sink-usb --parallel 2
```

`.work/builds/sink-usb/a2dp_sink_pcm_consumer.uf2`を書き込みます。Python環境に`pyserial`を用意し、同じポートを使うUSB端末は閉じてください。ポートは実際のPicoのものを確認し、保存先は**まだ存在しない専用ディレクトリ**にします。親ディレクトリは先に作成し、NTFS/ext4などハードリンク対応のローカルファイルシステムを使ってください。

Linuxでの例（`PICO_SERIAL_PORT`に確認済みのポートを設定）：

```sh
: "${PICO_SERIAL_PORT:?Set this to the verified Pico USB serial port}"
python3 examples/A2dpSinkPcmConsumer/capture_usb.py \
  --port "$PICO_SERIAL_PORT" --output-dir /tmp/sink-take-01
```

WindowsのPowerShellでの例（`COM7`と保存先は自分の環境に合わせて変更）：

```powershell
python examples/A2dpSinkPcmConsumer/capture_usb.py --port COM7 --output-dir 'C:\private\sink-take-01'
```

1. 収集スクリプトは指定ポートを115200 baudで開きます。機器によってはポートを開くとDTR/RTSに影響するため、対象を確認してください。Bluetooth命令は自動送信しません。
2. 収集スクリプトの入力欄で`i`、`l`を順に送り、送信元でPicoを選んで再生します。`?`で`streaming=1`、想定したrate/channels、`measured_frames`の増加を確認します。空のEnterで表示だけを更新できます。
3. 準備ができたら**`CAPTURE`**と入力します。これがPicoに`r`を送り、次の最大5秒を録音します。それ以前には録音しません。転送中は別の端末でポートを開かないでください。
4. 最大15秒で1回の収集を終えます。結果を下で確認してください。終了してもBluetoothは切断されないため、同じポートを開き直し、`d`と`?`で切断を確認します。

録音結果：

- 終了コード0：`received-5s.wav`と`capture-result.json`。全フレームが揃い、報告上の欠落なし。
- 終了コード2：`received-INCOMPLETE.wav`。ENDは検証できたものの、時間不足・欠落・異常あり。
- 終了コード1：エラー。完全な録音として扱わず、メッセージと保存先を確認してください。CRC、フレーム集計、END不在などの検証エラーではWAVを作りません。ファイル処理の失敗では`.partial`などが残る場合があります。既存ファイルは上書きしません。

**録音音声はGit、配布物、外部アップロードに含めないでください。** 収集スクリプトはraw dumpを作りません。検査はパケットとカウンターが対象で、音質の確認ではありません。不完全WAVは受信できた分を連結するため、欠落した時間の分だけ短くなります。

<details>
<summary>実装・データ形式の詳細</summary>

## ライフサイクルと FIFO

- receiverとconsumerは静的寿命です。`begin()`は一度だけ呼び、以後は単一メインループで`poll()`を続けます。IRQ・別core・別threadから共有できません。コールバックではメモリ確保、待機、I/O、receiver APIの呼び出しをしません。
- `BoundedPcmSink<8192>`は32 KiBのFIFOと2 KiBの取り出しバッファを使います。poll後に最大512フレームずつ空になるまで処理しますが、無欠落を保証するものではありません。
- `write()`の入力は借用の符号付き`int16_t`で、ステレオはL0,R0,L1,R1,…の順です。空きに入る先頭だけコピーし、残りは拒否します（drop-new）。receiverは拒否分をdroppedと数え、再送しません。
- `pull()`の容量は`int16_t`要素数です。完全なフレームだけコピーし、`{frames, sample_rate, channels, epoch}`を返します。借用ポインターは保持しません。
- `reset()`は保留PCMとformatを消し、epochを増やします。異なるformatの非空writeも旧FIFOを捨て、meterはepoch変更で集計をリセットします。epochはローカルの境界番号で、Bluetooth session IDではありません。
- 次のpollをまたいで古いPCMを渡さないでください。DMAなどのキューを加えるなら、そちらもreset時に取り消します。`flushed`は取り出す前に捨てたPCMで、`rejected`とは別です。

入力検査と累積カウンターの詳細は[英語版のFIFO仕様](README.md#pcm-and-fifo-contract)を参照してください。受信したrate/channelsに従って処理します。[対応SBC設定](../../docs/sink_sbc_configurations_contract_jp.md)以外のcodec、resample、チャンネル変換、出力clockは提供しません。

AVRCPを組み合わせるなら[AvrcpMediaControlSample](../AvrcpMediaControlSample/README_jp.md)を参照してください。PCMを捨てるPLAY/PAUSE例で、この録音例とは別のアプリです。共有runtime、単一メインループ、所有オブジェクトの寿命を守って組み込みます。

<a id="明示的な-discoverable-listen"></a>
## 発見可能な待受け

`SINK_PCM_DISCOVERABLE_LISTEN`は既定OFFです。ONでも、起動時や`i`でのアドレス表示だけでは発見可能になりません。受理された`l`で開き、接続・世代変更・異常・待受け終了・`d`・`c`で閉じます。60秒経過時は次のpollで閉じて切断を要求します。厳密な時間保証ではなく、自動で開き直しません。

通常の`listen()`は接続可能状態だけを変えます。`SINK_PCM_ALLOW_PAIRING`も既定OFFで、ONは指定peerのSSP確認を許可します。発見可能状態とペアリング許可は独立しています。

## 状態表示の framing v1

`?`の3行はそれぞれ`SINK_PCM v=1 report=N line=L/3 `で始まります。Lはstate=1、PCM=2、receiver/FIFO=3。同じ表示の3行は同じNを使います。Nは1〜4294967295で、表示が欠けても番号を消費し、上限後はこの形式の表示を出しません。

ID は再起動で戻るため、鮮度や USB 到達の保証には使えません。行の順序とフレーム集計を確認してください。[レポート形式の詳細](README.md#status-framing-v1)を参照してください。

### 転送・失敗の上限

録音版はメインループからTinyUSBを処理し、固定の32 packetキューでPCMを送ります。rate×5フレーム到達、format/epoch変更、stream停止、または7秒で録音を止めます。ホストが読めなくても開始から12秒で転送を打ち切ります。転送中の表示は止め、入力は捨てます。

ホストはCRC、sequence、format、時間、フレーム集計を検証し、END不在を成功にしません。[パケット形式とバッファの詳細](README.md#transport-and-failure-bounds)、[SDK由来コードの出典](PROVENANCE.md)を参照してください。

</details>

<a id="compile-only"></a>
<a id="実機を使わないビルド"></a>
## 任意：コンパイルだけ確認する

**初回操作の前に行う必要はありません。** 同じサンプルの無線OFF設定で、USBシリアルも起動しません。操作確認用に書き込むものではありません。

```sh
cmake -S examples/A2dpSinkPcmConsumer -B .work/builds/sink-pcm-offline \
  -DSINK_PCM_ENABLE_RADIO=OFF -DSINK_PCM_ALLOW_PAIRING=OFF \
  -DSINK_PCM_USB_CAPTURE=OFF -DSINK_PCM_DISCOVERABLE_LISTEN=OFF \
  -DSINK_PCM_PEER=00:00:00:00:00:00
cmake --build .work/builds/sink-pcm-offline --parallel 2
```

4つのオプションはすべて既定OFFです。OFF版でもコードの型検査は行われますが、無線ON版のリンクや動作は確認できません。ON版には非ゼロの6オクテットの送信元アドレスが必要です。

## 検証範囲と結果の区別

```sh
bash tests/host/run_sink_usb_capture_tests.sh
bash tests/host/run_sink_pcm_sample_tests.sh
bash tests/host/run_sink_discoverable_listen_tests.sh
```

これらは模擬データを使うホスト試験です。実機、USB、無線、書き込み、録音は使わず、USB転送性能やBluetooth音声の実機動作を確認するものではありません。[FIFO試験の説明](../../tests/host/sink_pcm_sample/README.md)も参照してください。
