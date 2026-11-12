# SPP 双方向・アプリ側フレーム例

[English / wire bytes and build detail](README.md)

ライブラリはRAWバイトストリームのままです。この例だけがversion/type/lengthを持つ
小さなアプリ形式を実装します。制御用ストリームとデータ用ストリームを分離し、
データ側には温度と3成分ベクトルの2種類を多重化します。どちらの端点からも送信でき、
自動echo、ACK、自動再接続、自動ペアリング承認、マイク、音声はありません。
RFCOMMのポート制御コマンドとは別のアプリ制御です。

## コンパイルのみ

リポジトリ直下から、導入済みSDK 2.1.1とpicotoolを使用します。

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S examples/SppFramedDuplexSample -B build/spp-duplex-off \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_UPDATES_DISCONNECTED=ON -DPICOTOOL_FORCE_FETCH_FROM_GIT=OFF
cmake --build build/spp-duplex-off -j4
```

既定はradio OFF・peer未指定です。stdio初期化、Client生成、pollより前に終了します。
有効パスのリンク確認には別buildディレクトリを使い、
`-DSPP_SAMPLE_ENABLE_RADIO=ON -DSPP_SAMPLE_PEER=02:00:00:00:00:01`を追加します。
これは架空の試験用アドレスで実機接続先ではありません。実機用には別途許可された
相手を設定してください。ビルドだけでは書き込み・実行・接続は行われません。
有効版も起動時はUSB stdioだけで、`b`まではbeginしません。

Basicのみ。`SPP_SAMPLE_CONTROL_CHANNEL=1`、`SPP_SAMPLE_DATA_CHANNEL=2`が既定です。
それぞれ1～30かつ異なる番号にします。相手も同じチャネル・形式を実装する必要が
あります。認証は必要で、自動確認承認は無効です。鍵管理やペアリング操作は実装しません。

## 書き込み後の手動コマンド

各1文字がコマンドで改行は無視します。すべて同じmain loopで処理します。
USB stdout・stdio lock待ち・接続待ちのtimeoutは明示的に0です。未接続/満杯時の表示は捨てられます。
文字列整形や送信可能分の書込みにも有限の処理時間がかかり、pollを遅らせます。
リアルタイム性やログの無欠落を保証する例ではありません。

- `b`: beginを1回queueに入れる。`s`でruntime=2（Ready）を確認。
- 発信モード: `c`で制御、`d`でデータに接続。`s`でphase=4（Open）を確認。
- 待受モード: 上記の代わりに`l`で制御、`k`でデータを登録。
  endpoint phase=2（Listening）を確認してから相手が接続。
- `t`: 開いている制御routeに固定のmarker command 7を1個積む。
- `v`: 開いているデータrouteに温度とベクトルを1組積む。
- `s`: runtime、error、channel、generation、route/endpoint状態、保留bytes、
  受信件数と最終値、形式エラー、破棄bytesを表示。
- `x`: アプリの保留bytesを破棄し、接続中/確立中routeの切断を要求。待受は残る。
- `u`: Listeningのendpointを撤去。登録中なら完了を待ち、もう一度入力。
  新規受入を止めて切断するには、`u`後Disabledを確認し、`x`を入力。

queue受理は完了でもpeer ACKでもありません。`s`で結果を確認します。拒否された操作は
後から手動で再入力します。Clientはstaticで保持し、失敗時もpollを続けます。
end/reset/recreateの代用として共有radioを切ることはしません。

最初の接続/待受コマンドでモードを固定し、切断後も切替えません。発信時のchannelは
相手側、待受時はローカル側を意味します。incomingは到着順に空きrouteを割り当てるため、
endpoint 0がroute 0とは限りません。本例は観測したchannelとgenerationから役割を対応
付け、slot 0を常に制御とは扱いません。混在origin、SDP選択、3本目、2台目には非対応です。

## アプリ形式

先頭4bytesはversion=1、type、payload長のLE16です。payloadも符号なしLE16で、
構造体のメモリ表現をそのまま送信しません。正確な16進bytesは英語版にあります。

- 制御type=1、長さ2: marker command ID=7。受信記録だけで外部動作はしない。
- データtype=2、長さ2: 温度2500（0.01℃単位、25.00℃）。
- データtype=3、長さ6: ベクトル(10,20,30)、物理単位なし。

相手は同形式の符号なし16bit値を送れます。固定値との一致は受信条件ではありません。
分割readも複数frameの結合readも処理します。version/役割/type/長さを検査し、不正なら
そのrouteのアプリI/Oを停止して手動切断・再接続を待ちます。CRC、圧縮、再同期推測、
ACK、request/reply相関はありません。自動応答ループもありません。

## 固定容量と世代

routeごとに64byteのTX/RXを所有し、現在の送信は最大16bytes、受信frameは最大10bytes。
満杯/未接続/不正状態ではenqueueを拒否し、heapの可変queueを増やしません。各poll後、
各routeにつき最大1回32bytes readと1回write。部分受理は受理数だけ進め、残りbytesを保持。
Busy/WouldBlock時は次の外側pollまで待ち、spinしません。先頭操作を4pollで
R0→R1→W0→W1と輪番し、read後のcredit callbackが後続をBusyにする場合でも、
常に同じreadだけが先行し続けないようにします。制御用の別streamは優先遅延を
保証するQoSではありません。

手動abortは切断要求が拒否/遅延/失敗しても同じgenerationの停止を保持し、
次generationまでアプリI/Oを再開しません。

非Openまたはgeneration変更で保留TX、受信prefix、値/件数を破棄します。観測の間に
close/reopenしても世代で検出します。古いデータの再送・新旧連結はしません。再接続後に
手動送信すると新しい固定サンプルを作ります。破棄計数はアプリ保有分のみで、core FIFOの
破棄とは別です。自動復旧やwatchdogはなく、エラー時は操作側が判断します。

## 限定検証

`bash tests/host/run_spp_framed_duplex_tests.sh`で、このheaderと実SppCoreを
ASan/UBSan付きで試験します。SDK/radio部分は模擬callbackです。両role/両方向の正確な
bytes、incoming逆順、分割/結合受信、型付き値、部分送信、Busy/WouldBlock、世代破棄を
固定caseで確認します。MTU1019の1-credit条件でも両route/両方向の進展を確認します。実ClientとのARMリンクは別確認であり、実機動作の証明ではありません。
広範な受入項目、相互接続、圧力下の公平性、A2DP/AVRCP同時運用は未検証のままです。
