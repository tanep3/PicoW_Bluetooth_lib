# AVRCPサンプルと再現可能なローカル確認

[English](avrcp_examples.md) | [検証範囲](validation_jp.md)

アプリの正しい実行順序とlink参照を分離したサンプルです。build結果は実機受入ではありません。SDK profileを二重初期化せず、これらのownerとlegacy AVRCP pluginを混用しません。

## 役割と検証範囲

A2DP Sink は音声を受信し、AVRCP Controller は相手のプレーヤーへ操作を送ります。Target は逆に相手から操作を受けます。公開 media-control サンプルの操作は `PLAY` / `PAUSE` です。公開サンプル自体の実機検証は未完了で、別の試験の成功をそのまま適用できません。試験の詳細は [検証範囲](validation_jp.md) を参照してください。

## 基本操作を送るController例

[AvrcpMediaControlSample](../examples/AvrcpMediaControlSample/README_jp.md)はA2DP SinkとAVRCP Controllerを組み合わせ、送信元プレーヤーへ明示的に再生・一時停止を送ります。ローカル受付、押下と解放の両応答、PlayStatus、本人の画面観測を分けます。radio/pairingは既定OFF、有効版もSTARTを待ちます。公開向けに組み直した版には専用host/build確認があり、Windows実機で成功した私有FWと同一とは扱いません。既存Target例は逆方向の受信で、自動転送や二重初期化は行いません。

## アプリの使用パターン

[AvrcpMainLoopSample](../examples/AvrcpMainLoopSample/README_jp.md)は単一main loop、process-lifetime共有owner、callbackからmain loopへコピーする上限付きwork、明示的なエラー処理と要求/応答順序を示します。配布時の既定ではbegin/無線開始をしません。実プログラム化前にREADMEを読み、configure/compileをflashや機器操作の許可と扱わないでください。

同期callback内で別Bluetooth commandを発行しません。token/dataをownedな上限付き領域へコピーして戻り、後でmain loopから操作します。queue満杯も明示処理し、借用pointerの保存で代用しません。停止/timeout時もtransport清掃完了までowner/領域を保持。ローカル戻り値0は受付で、相手ACKではありません。

## 最初の実アプリへ進む順序

1. READMEに従い、[main-loop project全体](../examples/AvrcpMainLoopSample/CMakeLists.txt)を既定の無線OFFでbuildします。[APIリファレンス](api_reference_jp.md)でrole所有権とメソッド契約を確認し、必要な[Controller](avrcp_controller_jp.md)、[Target](avrcp_target_features_jp.md)、[Browsing](avrcp_browsing_jp.md)、[Cover Art](avrcp_cover_art_jp.md)の説明へ進みます。
2. vendor配置ではライブラリのフォルダ構成を維持します。`main.cpp`やsample単体のコピーでは不足します。`OwnedRecordQueue.h`、生成される`sample_config.h`、`examples/AvrcpCoverArtCompileProbe/btstack_config.h`への相対includeに依存します。独立アプリには自身の設定headerとsettingsを用意し、sampleのCMake helper呼出しをその配置へ合わせます。wrapperの`btstack_config.h`を別の場所へそのままコピーしません。
3. 同期決定と後回しにできるeventを分けます。Targetのdecisionはcallback中に実態に合う結果を返す必要があり、main loopを待てません。Browsing request tokenやコピー済みeventはqueueで後処理できます。ただしtoken保存は借用request byteの保持にならず、structコピーもpointer先の所有権を作りません。sampleのqueue headerは上限付き格納部品で、アプリ全体のevent方針ではありません。
4. playerを名乗る前に、sampleの拒否するTarget/catalogue方針を実際の再生状態と容量を守ったwork受付へ置き換えます。ローカル戻り値0やchunk受信を成功とせず、相手の応答をdecode・照合します。pairing/bond方針、queue満杯処理、期限、終端処理も必要です。書き込む前に、実際の接続相手とpairing設定を確認してください。

[検証範囲](validation_jp.md)は実装・hostのみの証拠・限定的な実機観測を分けています。この入口の説明で受入状況は変わりません。

## Link確認専用

- [AvrcpBrowsingCompileProbe](../examples/AvrcpBrowsingCompileProbe/README_jp.md)：共通Browsingと両role APIの参照。
- [AvrcpCoverArtCompileProbe](../examples/AvrcpCoverArtCompileProbe/README_jp.md)：共有signalling両role、Browsing、Cover Art、取消し/timeout APIのlink。

両probeとも既定では無線を初期化せず終了。連続API呼び出しは接続済みworkflowではありません。connect発行直後にbindingが利用可能になるとは限らず、そのままGETを続けて成功例として扱えません。

## Host機能回帰

開発用回帰試験にはfull repository checkoutとtest sourceが必要です。選別source archiveではrunnerが省略される場合があり、不在をライブラリのcompile失敗と扱いません。archiveの収録一覧と検証範囲を確認してください。アプリsampleのbuild手順とは別の経路です。

Full repository rootから実行します。

同梱検査は[ビルドガイド](build_guide_jp.md#compile-only)を参照してください。

SDK pathは導入済みの対応SDKへ合わせます。runnerは13個の明示suiteを順番に実行し、最初の失敗で停止。SSH、flash、無線、録音は行いません。多くは一時build directory、core suiteは`.work/avrcp/host`を使用。各runnerでASan/UBSanを有効化。未知source hashは拒否し、SDK原本は変更しません。

- Core/属性/facade試験は上限付きアプリ契約を検証。実SDK headerを使っていてもfacadeのtransport効果はmockです。
- Controller/Target/Cover Art overlay試験は実生成SDK parser/serializerとモデル化したtransport境界を使用。
- Browsing wire試験は記載範囲のframingと実SDK command serializerを検証。
- Common overlay試験はpending取消しと実shared helper/role serializerの組合せを検証。MTU/長さ全組合せはheader compileより時間がかかります。

- 専用metadata-continuation試験は実Target/common overlayをL2CAP模擬で実行し、長文属性・byte単位の再構成・abort/再開・queue済み通知を確認します。

このrunnerは新しい独立監査を主張しません。以前中断した独立レビューは未完。各checkpointのsource hash/試験上限を参照し、ローカルPASSで実機受入行を完了にしないでください。

## 実機受入は別段階

build/link、host機能試験、相手実機で観測した動作を別結果として扱います。[検証範囲](validation_jp.md)でrole/接続方向、packet/アプリ結果、欠落/再接続、併用を確認。必要な機器/flash承認を取り、SSHは必ず毎回承認、録音前には合図します。サンプルが勝手にpairingや全体discoverableを有効化したり、録音/受信byteのuploadをすることはありません。

公開 `AvrcpMediaControlSample` のメディア命令は `PLAY` / `PAUSE` のみです。私有試験のNEXTや音量ボタンを公開consoleの実装済み機能とは扱いません。
