# 同じ相手へのA2DP送受信：実験機構と制約

[English](a2dp_same_peer_experiment.md) · [通常の二つのロール](a2dp_dual_role_jp.md)

**実験機構の説明であり、通常アプリへの導入手順ではありません。** 通常のSource/Sink helperは同じ相手への登録を拒否します。2本の反対方向の音声streamを実機で確認した機能ではありません。

## 公開helperとの組合せ

`cmake/picow_a2dp_same_peer_experiment.cmake`と`picow_enable_a2dp_same_peer_experiment(target)`は実験用の構成要素です。現在の公開Sink helperもSDKの`avdtp_acceptor.c`をraw SBC guardへ置換するため、**同じCMake構成では併用できません。** 呼出し順の変更や別targetへの適用だけでは解消しません。通常のSinkアプリは[ビルドガイド](build_guide_jp.md)に従い、実験helperを追加しないでください。

生成器は5本の固定されたSDK入力を対象にし、異なるhashの入力は推測でpatchせず拒否します。インストール済みSDKは変更しません。構成要素の同梱は、通常helperとの合成や実機動作の保証ではありません。

## 実験が扱う境界

- 接続全体のconfiguration状態と各SEPの所有を区別します。別SEPの未完了処理を消して設定を通すことはしません。
- roleごとのSEID、media CID、現在のACLを照合して、制御・mediaイベントの誤配信を防ぎます。
- `detachSharedRole()`は安定したmediaのCLOSEだけが対象です。STREAM_RELEASEDとSDK endpoint解放後のpollで退役します。`false`は受付なしで、busy CLOSEでは既存streamを保ちます。
- `cancelSharedDiscovery()`は実験Sinkの探索段階だけです。成功は受付であり、SDK応答のdrainまで完了しません。10秒の期限超過でも共有transactionを強制的に空けず、他roleのmediaを保ちながら隔離します。

## 未対応・未検証

Source facadeの同じ探索取消、SET_CONFIGURATION・OPENの取消、media CIDがない設定済みSEPのABORT、codec交渉から2本のstream確立まで、反復detach/rejoin、同時負荷・資源上限・実機相互接続は完成・検証されていません。`disconnect()`で別roleの共有signallingを壊してよいわけではありません。

ソフトウェアfixtureやARM linkの結果は、無線交渉、音声転送、再接続の結果とは別です。実験用runnerはこの配布物には含めていません。通常用途では[対応範囲](supported_features_jp.md)を使ってください。

## ライセンス

BTstack由来の生成器部分と生成物には上流条件が残ります。[出典manifest](../licenses/a2dp-same-peer-manifest.json)、[BTstack標準条件](../licenses/BTstack.txt)、[Raspberry Pi追加許諾](../licenses/BTstack-Raspberry-Pi.txt)を保持してください。
