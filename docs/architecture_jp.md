# ライブラリの構成

[English](architecture.md) · [APIリファレンス](api_reference_jp.md) · [組み込み手順](getting_started_jp.md)

公開profileのオブジェクト、共有Bluetoothライフサイクル、SDK adapter、容量を制限したデータ処理を分けています。通常はCMake helperと公開facadeを使い、profileごとに別のBTstackを初期化しません。

## 各機能の入口

- [A2dpSourcePlayer](../audio-core/A2dpSourcePlayer.h): アプリのPCMをencoder・transportへ渡し、SBCとして送信します。
- [A2dpSinkReceiver](../audio-core/A2dpSinkReceiver.h): SBCを受信・デコードし、[IAudioSink](../audio-core/IAudioSink.h)へ一時的なPCM参照を渡します。再生・出力はアプリが実装し、resetと所有権の境界を守ります。
- [AvrcpController](../avrcp-sdk/AvrcpController.h): 相手プレーヤーへ操作を送ります。[AvrcpTarget](../avrcp-sdk/AvrcpTarget.h)は操作を受け、アプリが所有する状態を提供します。Browsing・Cover Artには個別のadapterと容量上限があります。
- [SppClient](../spp-sdk/SppClient.h): 最大2ルートのバイトstreamと状態・診断を提供します。部分I/Oを扱い、メッセージの区切りと再送はアプリで決めます。

## 共有runtimeと実行方法

[BluetoothRuntime](../bluetooth-runtime/BluetoothRuntime.h)が共有無線と登録profileのライフサイクルを調整します。各状態機械とSDK adapterはbinding・世代・状態を区別します。切断・交換されたbindingで、古い処理を黙って復活させないでください。

有効な各profileの処理を一つのメインループから呼び、オブジェクトの寿命を守ります。PCM callbackからreceiverを変更したり、APIに許可されていない別core・IRQアクセスを加えたりしないでください。要求の受付と相手側の完了は別なので、成功戻り値だけでなく状態・イベントを確認します。

詳細は[接続状態](connection_state_transitions_jp.md)、[SPPの状態遷移](spp_state_transitions_jp.md)、[APIリファレンス](api_reference_jp.md)を参照してください。

## 音声データの所有

Sourceは[IAudioSource](../audio-core/IAudioSource.h)、[IAudioEncoder](../audio-core/IAudioEncoder.h)、[ITransport](../audio-core/ITransport.h)、[AudioPipeline](../audio-core/AudioPipeline.h)を使います。通常の送信延期では未送信データを保持し、同じPCMを読み直して再encodeしません。状態・形式の変更では古いqueueを無効化します。

Sink callbackのPCMは規定された呼出し期間だけ借用できます。処理を後回しにするなら容量を制限したアプリの保存領域へコピーし、reset・世代境界で古いデータを破棄します。出力処理のないreceiverはスピーカーを駆動しません。

## ビルドとSDK

`cmake/`のhelperが選択した実装、設定header、SDK依存、必要なtarget用overlayを組み込みます。一部は固定hashのBTstack sourceをbuild内へ加工しますが、インストール済みSDKを編集しません。未知hashは構成時に拒否します。

個別にbuildできても、すべてのhelperを組み合わせられるわけではありません。[同一peer A2DP実験](a2dp_same_peer_experiment_jp.md)は公開Sink helperと同じCMake構成で併用できません。通常の別peer両roleにも[制約](a2dp_dual_role_jp.md)があります。

永続ボンドとcommitted TLVは明示的に選ぶ機能で、[配置・回復の条件](bluetooth_committed_tlv_jp.md)があります。profileを使うだけでは自動で有効になりません。

[対応機能](supported_features_jp.md)、[検証の限界](validation_jp.md)、[第三者ライセンス](../THIRD_PARTY_NOTICES.md)も確認してください。
