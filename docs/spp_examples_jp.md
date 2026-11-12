# SPP制御・peer切替の利用例

[English](spp_examples.md) | [Serial制御](spp_control_jp.md) | [Peer選択とERTM](spp_lifecycle_jp.md)

[SppControlMainLoopSample](../examples/SppControlMainLoopSample/README_jp.md) は公開 `spp::Client` APIを使うアプリの処理順序を示します。既定実行形式はBluetoothを開始しません。有効pathは意図して設定するアプリ向けで、compileはflash/実機操作の許可ではありません。

queue受付とローカル状態遷移の完了を区別し、service登録、明示peer/channelへの接続、serial制御の逐次送信、コピー済みstatus counterと実transportの観測、service撤去、route/transport資源の解放待ち、次peerの選択を行います。相手のmedia/playerアプリを実装したり、control statusをpeer ACKと見なしたりする例ではありません。

- 現在の資源契約は選択peerが1台、routeは最大2本です。peer選択は逐次再設定であり、複数peerとの同時通信ではありません。
- `control_status == 0` はSDKのローカル受付です。受信RPN/MSC/line観測はtransactionと対応づけたACKではありません。
- optional ERTMはBasicへfallbackし得ます。有効なOpen handleで `transport(handle)` を読み、buffer割当だけでERTM成立と判断しないでください。
- ERTM backing bufferとclientはプログラム寿命で保持します。disconnect要求・route close・アプリtimeoutだけではmultiplexer leaseを再利用できません。
- 例はoptional ERTMを使います。現実装ではrequired-modeの資源不足や不適切な既存transport再利用がshared radioを失敗させ得るため、mandatory ERTMにはアプリ全体の明示的な方針が必要です。
- 上限付きhistoryは古いentryを上書きします。診断eventの欠落を検出し、entryがないことから完了を推定しないでください。

利用例のhost順序試験、既存core/adapter/pool回帰、Pico SDK compile/linkは別々の確認です。無線peer切替・ERTM再送・相手のcontrol処理・A2DP/AVRCP併用を保証しません。実機検証済みとする前にlifecycle guideの実機表を実施してください。

## ローカル再検証

同梱検査は[ビルドガイド](build_guide_jp.md#compile-only)を参照してください。

利用例host試験は同じworkflow headerと実coreを使用し、transport結果だけを模擬します。2台目への接続は行わず、選択結果の確認までを示します。

## アプリ側で区切りを付けた二路の双方向通信

[SppFramedDuplexSample](../examples/SppFramedDuplexSample/README_jp.md)は、制御用の1路と、温度・ベクトルという2種類の合成データを運ぶ1路を示します。version/type/lengthはサンプルのアプリ形式で、ライブラリはraw byte streamのままです。発信・待受のどちらかをプロセス内で選び、到着順で割り当てられるslotではなくchannelから役割を決めます。

固定容量の部分書込み、分割／結合受信、型付き値の取得、世代変更時の途中データ破棄を含みます。切断が拒否・遅延しても同世代を再開せず、二路×送受信の先頭機会を4pollで交代させます。これは無線の公平性や優先度保証ではありません。ACK・自動echo・自動再接続・再送プロトコルはなく、既定では無線OFFです。

host test／build の成功は公開サンプルの実機動作を保証しません。異なる試験アプリでの結果を転用せず、[検証範囲](validation_jp.md) の範囲を確認してください。
