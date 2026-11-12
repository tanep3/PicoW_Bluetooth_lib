# 固定SDK向けAVRCP overlay

[English](avrcp_sdk_overlay.md) / [Target機能](avrcp_target_features_jp.md) / [Controller](avrcp_controller_jp.md)

AVRCPのCMake helperは、確認したTarget/Controllerの変更版ソースをビルドディレクトリへ生成します。インストール済みPico SDKは変更しません。対象executableだけ `PICOW_AVRCP_SDK_OVERLAY` target propertyとcompile definitionで選択し、他のexecutableは元SDKソースを使用します。Python 3が必要です。生成物をSDKへ上書きしないでください。

基準はPico SDK `9a4113fbbae65ee82d8cd6537963bc3d3b14bcca`、BTstack `501e6d2b86e6c92bfb9c390bcf55709938e25ac1`。両生成器は入力SHA-256が未知の場合や置換位置が欠落/重複する場合に停止します。SDK更新時は検査を無効化せず、上流差分の確認・patch修正・再試験を行ってください。[manifest](../licenses/avrcp-overlay-manifest.json)に入力hashを記録しています。

## 上流からの変更

- Target: Binding付きSetAddressedPlayer、標準player settingsの取得/text/一括SET、追加6通知とコピーしたchanged応答。item応答のrole/CID引数逆転とCompanyID byte orderを修正し、vendor長・通知ID・metadata属性の境界を検査します。
- Controller: settings応答を長さ制限付きraw chunkで通知。最大4096 byteの所有bufferでcontinuationを再構成し、明示continuation/abort・相手拒否・release時cleanupへ対応。transaction/PDUと長さを照合し、charsetの個数/network order/長さ、setterのpair count、value-text要求形式を修正。引数付きsettings helperを追加します。
- repository内の変更であり、上流リリースやAVRCP全機能適合の宣言ではありません。browsing database、cover art、vendor独自設定、自動アプリ動作、実機相互接続は範囲外です。

## 通知取消しの境界

Controller overlayはqueue中または送信済み未確認の登録解除を`ERROR_CODE_COMMAND_DISALLOWED`で明確に拒否します。既存queue/通知追跡を使い、通常の終端通知応答でも追跡を退役させ、登録済みの遅延解除と完全未登録のno-opを維持します。新API、即時wire取消し、自動再試行やtimeoutは追加していません。生成Controller overlayを使う場合の修正であり、元SDKを手動linkするだけでは選択されません。SDK原本・common/Target生成内容とライセンスは未変更です。

## ライセンスと再配布

生成ファイルは元BTstackのcopyright/license headerを保持します。patch内の上流引用と生成ソースにはBTstackの条件が適用され、本repositoryのMITへ変更されません。[BTstack条件](../licenses/BTstack.txt)、[Raspberry Pi向け許諾](../licenses/BTstack-Raspberry-Pi.txt)、[第三者通知](../THIRD_PARTY_NOTICES.md)を確認してください。Raspberry Pi許諾は定義されたProducts/Customer Productsに限定され、一般的な無制限MIT許諾ではありません。変更ソースやfirmwareの配布時も該当する通知を保持してください。

## 検証と限界

parser/serializerはmock transportを使ったhost試験とARMリンクで検証しています。未知SDK hashは構成失敗になります。mock framingは実transport分割の確認ではなく、追加API全体の実機相互接続を保証しません。対応範囲は[検証ガイド](validation_jp.md)を参照してください。

Browsing consumerにはhash固定の`avrcp_browsing.c`（空SDU検査・service登録結果の公開）と`avrcp_browsing_controller.c`（属性なし要求count/length修正）も適用します。元ライセンスは維持します。[Browsing Controller](avrcp_browsing_jp.md)参照。

Cover Art consumerは`avrcp_cover_art_client.c`と`goep_client.c`の生成版も選択し、所有権を保ったcleanup、失敗/cancel、OBEX要求容量検査を適用します。原notice/ライセンスは維持します。[Cover Art Client](avrcp_cover_art_jp.md)に範囲と制限を記載しています。

Controller overlayはowned custom要求と識別子照合付きraw応答、上限付きAVCTP再構成を追加。生成moduleはtools/avrcp_controller_custom.py。Target overlayはpass-through応答方針を追加し、元SDKの余分な2byte paddingを除去。原本/ライセンスは維持。詳細は各role資料とdeadline/custom証拠を参照。

共通transportはcustom AVCTP送信、vendor continuation、追跡済み通知併用を追加。tools/patch_avrcp_common.pyはavrcp.cのpending取消しと共通frame容量を修正。CT/TGの二重header差引き、TG metadataの属性数byteのoffsetも修正し、実共通helper＋両serializerで検証。元SDK・licenseは維持。

Target metadata continuationではvendor応答offsetとAVCTP frame offsetを分離し、各vendor応答長を制限、属性byteを保持し、通知を完了/abortまで延期します。`tools/avrcp_target_metadata_patch.py` から生成し、installed SDK原本は変更しません。

[検証範囲](validation_jp.md)
