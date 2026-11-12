# Sink のアプリ validator 拒否後の状態復元

## 対象

`tools/patch_sink_raw_configuration.py` が対象ターゲット向けに生成する、ハッシュ固定の acceptor に対する修正です。**正規化 media validator が Sink の SET_CONFIGURATION を拒否した場合**だけ、途中の状態変更を復元します。Pico SDK 2.1.1 / BTstack `501e6d2b86e6c92bfb9c390bcf55709938e25ac1` のインストール内容は変更しません。raw 入力検査とは独立したライフサイクル修正です。

## コールバックと所有権

1. 既存の raw guard が unpack 前に動作します。この修正でその判定は変えません。
2. SDK は従来通り、endpoint を一時的に CONFIGURATION_SUBSTATEMACHINE とし、要求元 connection に結び付けてから正規化 validator を呼びます。実際の `a2dp_sink_register_media_config_validator` の転送処理が subevent を変換し、呼出し後に元に戻します。
3. Sink の validator が非ゼロを返した場合だけ、endpoint の直前の state と connection pointer を復元します。内部 helper の返却値で、呼出し元の REMOTE_INITIATED への遷移を抑止します。公開 API の追加はありません。
4. 拒否時には codec 保存領域、local/remote configuration、in-use や他 endpoint を更新しません。応答待ちの acceptor substate は従来の reject-pending のままとし、通常の送信可能イベントを待ちます。この待機中まで完全に idle に戻すわけではありません。
5. 応答の transaction label、SET_CONFIGURATION、MEDIA_CODEC、アプリの error byte を保持します。Sink の SET category 拒否は、復元した旧所有者ではなく**要求元 connection**の CID/transaction label に返します。従来の endpoint reject 経路を残すため、送信後の拒否通知にも要求元 AVDTP CID、local SEID、非 initiator を保持します。既存の Source/Sink 両方への通知も維持します。
6. 拒否応答後、同一 session の次の有効な設定をアプリが許可すれば、通常の設定完了に進めます。自動再試行や validator の二重呼出しは追加しません。

Source の従来遷移と他の応答 routing は維持し、AVRCP は変更しません。RECONFIGURE の validator は既存の修正を維持するだけで、新しい状態遷移を追加しません。

## 公開 receiver と制限

`SinkProfile::validate` は正規化 codec とコールバック側の認証済み session/CID を検査し、endpoint 引数には依存しません。ただし本修正は既存 validator の互換性のため、コールバック時点の一時的な owner/state も従来通り維持します。コールバックは同期的に判定し、SDK session の破棄・変更や dispatch への再入を行わないことが前提です。任意のアプリ副作用を巻き戻すものではありません。

SEP-in-use、remote-SEP 衝突、全 AVDTP 状態、signaling 分割、同時・再入要求、送信失敗、Source のライフサイクル全般を修正するものではありません。他 connection の旧所有権を拒否時に復元しても、使用中 endpoint の乗っ取りを許可する意味ではありません。この復元試験は non-in-use endpoint に限ります。

## 検証

専用の検証では、固定した有効 SBC 設定 2 個とアプリの拒否/許可だけを使い、実 SDK の parser・正規化・A2DP validator 転送と、スタブの transport/session を組み合わせます。過去の raw 入力試験は実行しません。

このvalidator拒否後の回復経路は固定入力のhost試験とARM compile/linkで確認しています。この経路を対象とした無線相互接続試験ではありません。別途確認された共有コードの限定的なSBC/Sink実機結果を否定するものではありません。
[検証範囲](validation_jp.md)

同梱サンプルのビルドと実行可能な検査手順は[ビルドガイド](build_guide_jp.md#compile-only)を参照してください。上記の機能別検証範囲をすべて再実行する手順ではありません。
