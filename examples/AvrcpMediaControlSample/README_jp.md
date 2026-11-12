# AVRCPメディア操作コンソール

[English](README.md)

Pico Wから、Bluetoothで音声を送っているPCやスマートフォンのプレーヤーを**一時停止・再生する**サンプルです。PicoはA2DP Sinkとして音声を受け、AVRCP Controllerとして同じ送信元に命令を送ります。

受信した音声は捨てるため、**Picoから音は出ません**。録音もしません。送信元のプレーヤー画面で停止・再開を確認します。

<a id="安全なビルド"></a>
<a id="実機で使う設定へ切り替える"></a>
## ビルドして書き込む

用意するものはPico W、USBデータケーブル、USBシリアル端末、Bluetooth Classicの音声送信とAVRCP操作に対応するPCまたはスマートフォンです。[ビルド環境の準備](../../docs/build_guide_jp.md#prerequisites)を済ませ、リポジトリ全体を手元に置いてください。このディレクトリだけではビルドできません。

[送信元のClassic Bluetoothアドレス](../../docs/build_guide_jp.md#peer-address)を調べ、Bash変数`SOURCE_BT_ADDRESS`に設定します。**PicoのアドレスやWi-FiのMACアドレスではありません。** 以下はリポジトリのルートで実行します。`PICO_SDK_PATH`には用意したSDKの場所を設定してください。

```sh
: "${PICO_SDK_PATH:?Set this to your Pico SDK directory}"
: "${SOURCE_BT_ADDRESS:?Set this to the verified source Bluetooth address}"
cmake -S examples/AvrcpMediaControlSample -B build-avrcp-media-enabled \
  -DAVRCP_MEDIA_ENABLE_RADIO=ON \
  -DAVRCP_MEDIA_ALLOW_PAIRING=ON \
  -DAVRCP_MEDIA_PEER="$SOURCE_BT_ADDRESS"
cmake --build build-avrcp-media-enabled -j2
```

`build-avrcp-media-enabled/avrcp_media_control_sample.uf2`を[BOOTSELで書き込み、USB端末を開きます](../../docs/build_guide_jp.md#flash)。今のファームウェアを残したい場合は、先にバックアップと戻し方を確認してください。起動後は`INERT`と表示され、`START`を送るまで無線は動きません。

<a id="operator-workflow"></a>
## 操作手順

命令は**大文字で1行ずつ**送り、末尾をLFまたはCRLFにします。前後に空白を付けず、各手順の結果を見てから次を送ってください。

1. `START`を送り、`READY`と`LOCAL_ADDRESS`を待ちます。表示されたアドレスを確認してください。Bluetooth名とUSB名は`AVRCP Media Control`です。
2. 送信元のBluetooth設定を開き、Picoに`PAIR`を送ります。5分以内に、送信元から対象のPicoを選んでペアリングします。接続を許可するのはビルド時に指定した送信元だけです。
3. 送信元の音声出力をPicoに切り替え、プレーヤーで曲を再生します。`STATUS`で`streaming=1`を確認します。A2DP接続後も`ct_cid=0`なら、`CONNECT_AVRCP`を1回送り、`STATUS`で`ct_cid`が0以外になるのを待ちます。戻り値0だけでは接続完了ではありません。
4. `PAUSE`を1回送ります。`CLICK_COMPLETE remote_accepted=1`を待ってから`QUERY_STATUS`を送り、`PLAY_STATUS`の`interpreted=1`と`playback=2`を確認します。数秒待ち、プレーヤー画面でも一時停止を確認してください。
5. 確認できたら`PLAY`を1回送ります。同様に`CLICK_COMPLETE remote_accepted=1`を待って状態を照会し、`interpreted=1`・`playback=1`と画面上の再生を確認します。
6. 終わったら`SHUTDOWN`を送ります。`STATUS`の`initialized=1 hci=0 running=0`を確認してから、必要なら元のファームウェアに戻します。送信元の音声出力も戻してください。

## うまく動かないとき

- **USB端末が出ない：** `AVRCP_MEDIA_ENABLE_RADIO=ON`で作ったUF2を書き込んだか確認します。OFF版はUSBシリアルも起動しません。
- **接続できない：** 指定した送信元アドレスと、選んだPicoのアドレスを確認します。鍵はRAMにしか残らないため、Picoを再起動した後は送信元のPicoの登録だけを削除し、ペアリングし直す必要があります。
- **`DENIED`が出る：** `STATUS`で接続と処理中の命令を確認し、応答を待ってから新しい行で送ります。行編集はできません。命令をまとめて貼り付けないでください。
- **接続だけ切りたい：** `DISCONNECT`でAVRCPとSinkの切断を要求し、Sinkの待受けとペアリングを閉じます。無線はOFFにならず、固定の送信元からAVRCPが再接続する場合があります。無線を終了するには`SHUTDOWN`を使います。
- **終了後に再開したい：** `SHUTDOWN`、致命的エラー、動作期限による終了後はリセットが必要です。全体の動作時間は`START`から15分です。

`LISTEN`はSinkの接続待受けだけを開き、発見可能状態やペアリング許可を変えません。`AVRCP_MEDIA_ALLOW_PAIRING=OFF`のビルドでは`PAIR`は使えません。開始前の`STATUS`に出る`initialized=0 hci=255`は未初期化を表し、255は無線状態が不明という意味です。

<a id="成功を4段階に分ける"></a>
## 結果の見方と制限

- `PRESS_LOCAL rc=0`はローカルで要求を受け付けたという意味です。
- `CLICK_COMPLETE remote_accepted=1`は、対応する押下・解放の**両方**に相手がAccepted(9)を返したという意味です。Rejected(10)、Not Implemented(8)は成功に含めません。
- `PLAY_STATUS ... interpreted=1`は、現在の照会にImplemented/Stable(12)が返った場合です。`playback`は0=停止、1=再生、2=一時停止。長さ・位置は不明値`UINT32_MAX`や遅れを含むことがあります。`interpreted=0`では状態を確認できていません。
- 最後に、意図したプレーヤーの画面を確認します。相手が命令を受け付けても、OSやアプリの対応によって実際の動作は異なります。

メディア命令は`PLAY`(0x44)と`PAUSE`(0x46)だけです。曲送り・音量操作やAVRCP STOP(0x45)はありません。`SHUTDOWN`はPico側の終了命令です。

**この公開サンプルそのものはホスト試験・ARMビルドまで確認済みで、実機試験は未実施（NOT_RUN）です。** 別アプリの試験結果とは区別しています。[検証範囲](../../docs/validation_jp.md)を参照してください。

<details>
<summary>アプリへ組み込む場合の注意点</summary>

## 実装上のポイント

- `Application.h`はSinkとControllerを各1つ、終了処理中も含めて保持します。終了後に所有オブジェクトを作り直したり、旧AVRCPプラグインと混在させたりしません。
- コールバックはイベントと借用データを32件の固定キューへコピーするだけです。表示、USB待ち、Bluetooth APIの呼び出し、借用ポインターの保持はしません。押下応答後、メインループから解放を送ります。既に解放中なら重ねて送りません。
- v6 SDK overlayがtransaction label、operation、押下/解放bitを照合します。アプリ側のbindingだけでは無線応答を照合できません。
- 入力行は最初のbyteを読んだ時点のbindingを保持します。接続の変更、未接続、命令・照会の処理中は操作を拒否します。ただし、ホストのUSBバッファに入力された時刻は分かりません。1周で最大16byteを読み、不正な行は改行まで捨てます。
- 期限はSDK poll前に確認します。起動15秒、Controller応答700ms、アプリ命令・照会1500ms、ペアリング5分、全体15分。pollが止まると5秒のwatchdogで`INERT`へ再起動します。実測の時間保証ではなく、応答が遅い機器には調整が必要です。エラー時の自動再送や長押しの繰り返しはありません。
- USB出力の待機は0ですが、すべての処理時間を保証するものではありません。表示欠落、イベント連番、キュー損失を確認し、不完全なログを全応答の証拠にしないでください。HCI dumpと永続的な鍵保存は無効です。
- `SHUTDOWN`は共有無線全体を終了します。無関係なプロファイルが動く製品には、そのまま適用できません。

## 既存例との使い分け

- この例：Pico Controllerから送信元プレーヤーへPLAY/PAUSEを送ります。音声は捨てます。
- `examples/DeveloperSamples/avrcp_target.cpp`：相手ControllerからPico Targetへ届く操作を確認します。プレーヤー機能やControllerへの転送はありません。
- `examples/AvrcpMainLoopSample`：Controller/Target、browsing、artworkとキューの実装例です。Targetは仮の拒否処理で、相手にbrowsing対応を求める構成も含みます。

音声のSource/Sinkと、AVRCPのController/Targetは別の役割です。組み合わせる際は各APIと共有runtimeの契約を守り、BTstackを二重に初期化しないでください。

</details>

<a id="compile-only"></a>
## オフライン検証

開発用の任意の手順です。**上の実機用ビルドの前に行う必要はありません。** 次は同じサンプルを無線OFFでコンパイルする設定で、別のサンプルではありません。USBシリアルも起動しないため、操作確認には使えません。

```sh
cmake -S examples/AvrcpMediaControlSample -B build-avrcp-media-offline \
  -DAVRCP_MEDIA_ENABLE_RADIO=OFF -DAVRCP_MEDIA_ALLOW_PAIRING=OFF \
  -DAVRCP_MEDIA_PEER=00:00:00:00:00:00
cmake --build build-avrcp-media-offline -j2
bash tests/host/run_avrcp_media_control_sample_tests.sh
```

無線とペアリングの既定値はOFFです。無線ONには非ゼロの6オクテットの相手アドレスが必要で、無線OFF・ペアリングONの組み合わせは拒否されます。OFF版でもコードの型検査は行われますが、無線ON版のリンクや動作を確認するものではありません。ホスト試験も模擬APIを使っており、実機・ペアリング・実SDK serializer・プレーヤー画面は検証しません。
