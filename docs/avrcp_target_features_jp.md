# AVRCP Target のSDK機能対応

[English](avrcp_target_features.md) / [Target組込み](avrcp_target_jp.md) / [Controller](avrcp_controller_jp.md)

現在の実装範囲であり、AVRCP全機能適合の宣言ではありません。対象SDKはPico SDK `9a4113fbbae65ee82d8cd6537963bc3d3b14bcca`、BTstack `501e6d2b86e6c92bfb9c390bcf55709938e25ac1`。追加機能はホストadapterテストとARMビルドで確認し、**実機相互接続は未検証**です。既存ボタン試験を追加機能の証明へ転用しません。

## 実装したTarget facade

同じmain loopから呼びます。接続後に `core().bindingForPeer(peer)` で最新 `Binding` を取得してください。CID・peer・ACL epoch・generationを持ち、古いbindingはSDK呼出し前に拒否します。整数0はSDKのローカル受理であり、相手受信の証明ではありません。アプリcallbackは短時間で戻り、receiver/control APIへ再入しないでください。

| 機能 | API・動作 |
|---|---|
| 着信・発信signaling | `begin`、`connect(peer)`、`disconnect(binding)`、`poll`、終端 `stop`。Controller/TargetでSDK初期化を共有 |
| Pass-throughボタン | begin前の `setSupported`、押下/解放イベント、切断/resetで保持解除 |
| Play-status問合せ | `setPlayback(length_ms, position_ms, status)` で両設定peerの応答値を設定。自動プレイヤー操作なし |
| メタデータ | `setMetadata(binding, Metadata)` でtitle/artist/album/genre、track ID・番号・総数・長さをowner所有領域へコピー |
| 通知対応の広告 | `supportNotification(binding,event_id)` はrepository overlay使用時に1..13を許可（元SDKでは7種類） |
| 再生状態・音量 | `notifyPlayback`、`notifyVolume`。音量は0..127で、DAC/software gainは変更しない |
| 曲・一覧・player・UID | `notifyTrack`、`notifyContentChanged`、`notifyAddressedPlayer`、`notifyUids` |
| 電池状態 | `notifyBattery`、値0..4 |
| Capability | `setCompanies`（24bit IDを1..8件）、`setUnitInfo`（5bit type/24bit company）、`setSubunitInfo`（1..32 byteをコピー） |
| 相手のabsolute-volume要求 | `setRequestHandler`。同期handler後にSDKが応答し、返す音量値で要求値を量子化できる |
| 相手のPlayItem/AddToNowPlaying | 同handlerへscope・UID counter・8byte item ID・bindingを渡し、AVRCP statusをSDK応答として返す |

通知IDは1（playback）、2（track）、6（battery）、9（now-playing content）、11（addressed player）、12（UIDs）、13（volume）です。接続ごとに明示設定し、アプリ側の変更を対応メソッドで報告します。通知登録・transaction label・interim/changed応答・一度の変更後の再登録はSDKが管理します。アプリ状態の自動監視はありません。

`notifyPlayback` はそのpeerのplay-status問合せの再生値を、`setMetadata` は長さを更新します。`setPlayback` は両peerの長さ・位置・再生値を更新しますが、それだけで通知を送信しません。アプリで問合せ状態と通知を整合させます。

## メタデータとポインタ寿命

`Metadata` の文字列は読み取り可能なNUL終端で各255byte以内です（Unicode文字数ではありません）。nullまたは上限超過は、どの項目も変更する前に拒否します。4文字列・company ID・subunit byteはコピーするので、成功後は入力領域を解放できます。track IDは文字列ではなく8byteです。

BTstackはmetadata/capabilityのポインタを保持します。ownerは設定peerごとに別の固定領域を持ちます。SDK応答送信中、または分割metadata応答の続き待ち中の更新は `ERROR_CODE_COMMAND_DISALLOWED` を返します。通常pollを進め後で再試行し、空回りしないでください。検証したSDKのconnection state・metadata応答flag・未完GET_ELEMENT_ATTRIBUTES byte数に基づくguardなので、SDK更新時は再確認が必要です。相手がcontinuationをabortせず放棄すると、SDK transaction/channelが終わるまでbusyが続く場合があります。

Targetはプログラム寿命で使用します。begin試行後に破棄した場合、非同期SDK cleanupの参照が残り得るため、コピーしたcapability/metadata領域はresetまで意図的に保持します。繰返し生成・破棄するrestart可能serviceではありません。`stop()` は他profileのradioを落とさず、Controller ownerも使用するsignaling channelは切断しません。

## Request handler契約

```cpp
using Target = picow::avrcp::AvrcpTarget;
Target::Response decide(const Target::Request& request, void*) {
    if (request.kind == Target::Request::Kind::AbsoluteVolume) {
        // 実際の音量変更intentをアプリへ渡す。自動gain変更はない。
        return {0, request.volume};
    }
    // 0 = AVRCP INVALID_COMMAND。実際にitem操作を受理したときだけ
    // 4（SUCCESS）を返す。
    return {0, 0};
}
// target.setRequestHandler(decide, nullptr);
```

handler未登録ならitem操作はINVALID_COMMAND、absolute volumeは要求値を維持します。音量応答はAVRCP statusではなく音量値です。SDKはwire上で既に受理する仕様です。不正な返却音量は要求値へ戻してresponse errorを加算し、不正なitem statusはINVALID_PARAMETERへ置換します。要求受信だけで音声再生やqueue変更は起きず、アプリがintentを実行します。

## Pass-throughのwire判断（固定overlay）

begin前の `setSupported(operation, enabled)` がwire上の対応操作を制御します。既定9ボタンは変更しません。定義済みAV/C panel操作のみ配送し、予約IDは有効設定してもNOT_IMPLEMENTEDです。標準ボタンのoperandは0byteのみです。未対応操作はNOT_IMPLEMENTED、重複押下・押下のない解放・古いBinding・未対応の標準operandはREJECTEDを返し、保持状態やOperationイベントを変更しません。

`setPassThroughHandler(handler, context)` で `const PassThroughRequest&` を受け、`PassThroughDecision::Accepted` / `Rejected` / `NotImplemented` を返せます。要求は新しい `binding`、`operation`、`pressed`、`operand_count`、コピー済み5byte配列 `operands` を含みます。不正な戻り値はREJECTEDです。handlerなしでは対応済み・正しい順序の標準ボタンを受理し、既存core callbackへ一度だけ配送します。handlerありではAcceptedだけが保持状態を更新し、core callbackを呼びます。判断handlerはintentの受入判断、標準ボタンの実行はcore Operation callbackと役割分担し、同じ操作を二度実行しないでください。

SIG next/previous groupは `setSupported(0x7e, true)` とhandlerの両方が必要です。SDPのgroup-navigation対応bitを広告する場合は両方をbegin前に設定してください。operandは `00 19 58 00 00`（次group）または `00 19 58 00 01`（前group）の正確な5byteです。group操作はアプリが実装し、track forward/backボタンへ自動変換しません。groupごと・peerごと・Binding世代ごとに独立した押下/解放状態を持ち、再接続で初期化します。groupは判断handlerだけへ配送し、operandを持たないcore Operation callbackへは配送しません。handler未登録、未知company/group IDはNOT_IMPLEMENTED、不正なoperand長はREJECTEDです。任意vendor payloadには対応しません。

overlayが応答を一度だけqueueし、operation/state・transaction label・境界検証済みoperandをechoします。SDKの余分な2byte paddingも除去します。panel subunit 0のCONTROLだけを処理し、短すぎるheaderは無視します。宣言長不一致や5byte超はアプリを呼ばず、無制限コピーもせず拒否します。busy時（metadata continuationの途中を含む）は新しいpass-through要求を破棄し、保留応答やtransaction labelを上書きしません。peerはidle復帰後に再試行する必要があります。

どちらのcallbackも短い同期処理とし、SDK応答関数の呼出し・API再入・例外送出は禁止です。二重ACKを送らないでください。Acceptedはintentの受理であり、実際のmedia動作や応答配送の証明ではありません。SDK本体は変更せず、生成時に固定SHA-256を検証し、未知revisionでは停止します。

## 未完とSDK制約

- [固定SDK overlay](avrcp_sdk_overlay_jp.md)で標準equalizer/repeat/shuffle/scanの4設定を追加しました。`configurePlayerSetting(id,value)` をbegin前に呼び、設定した属性のみ広告します。列挙/value/current/text要求は境界検証し、SETは全項目を検証して `SetPlayerSettings` handlerの成功後に一括更新します。textは固定英語UTF-8で、vendor独自設定や多言語化機構ではありません。
- `SetAddressedPlayer`要求は検証済みBindingとplayer IDをhandlerへ渡し、未登録なら拒否します。成功時はSDKの選択状態を更新しますが、実際のplayer切替と必要な変更通知はアプリの責任です。
- overlayではtrack start/end・position・system status・settings・available players通知を追加しました。`notifyTrackReachedStart/End`、`notifyPosition`、`notifySystemStatus`、`notifyPlayerSettings`、`notifyAvailablePlayers`を使用します。`positionNotificationInterval(binding)`の秒数を読み、アプリ側で定期更新を行ってください。自動timer・seek検出・media databaseはなく、settings変更時の自動通知もありません。
- **Browsing Targetとcover art**は未ラップ。PlayItem/AddToNowPlaying control PDUは、完全なbrowsing database・filesystem・player実装ではありません。
- **Pass-throughのwire応答方針:** 固定overlayでは以下の明示的な方針を実装します。overlayなしの元SDKはlegacyイベント前に自動acceptするため、wire拒否が必要な用途では使用しないでください。
- **フルAVRCP 1.6保証なし:** SDP versionはSDK依存。SDPはplayer/recorderに設定済みsettings/group-navigation bitを追加。Browsing buildではbegin(config, true)でbrowsingも広告可能ですが、multiple-player対応を意味しません。adapter試験は無線相互接続や実音声効果の証明ではありません。

## 検証

- 共有 Controller/Target 経路の host 回帰: 共有roleのライフサイクル検査。実機接続交渉・timing の保証ではありません。

- Target API検査: 実SDK型/accessorとSDK動作stub、ASan/UBSan。所有コピー・255/256byte境界・null・2peer分離・応答/continuation中更新拒否・通知ID whitelist・値域・SDKエラー伝播・古いbinding・音量/item・callback再入拒否・終端stopを検査。
- Target adapter検査: 既存の寿命・release・不正イベント・play-status・失敗・2peer回帰。
- Target は実 SDK で Pico W 向けのビルドとリンクを確認しています。無線動作を証明するものではありません。

Target overlay検査 は生成SDK本体のparser/serializerをASan/UBSanで実行し、player選択のCID/長さ/拒否、settings応答、item応答lookup、通知transaction/busy/one-shot、不正ID、facadeの一括設定/Bindingを検査します。pass-throughの3応答種別・正確なframe・押下/解放・group echo・不正判断・busy保持・長さ境界・単一応答・sequence/reentry/peer世代・生成再現性も検査します。L2CAPは単一packetとしてmock化しており、無線fragmentation、実機相互接続、browsingを証明しません。

[検証範囲](validation_jp.md)

[pending signalling接続取消しの共有契約](avrcp_controller_jp.md)：TargetにもcancelConnection(peer)、cancellationPending(peer)、cancellationStatus(peer)を追加。両roleの接続試行へ作用し、release前の資源再利用はしません。

## 長文metadata応答

固定版Target overlayはmetadataを最大502 parameter byteのvendor応答へ分け、各応答を独立したAVCTP framingで送信します。REQUEST_CONTINUEでも属性byteと全体offsetを保持し、属性数は最初の応答だけに含めます。ABORTで保持transactionを終了します。metadata置換を拒否し、queue済み通知は完了/abort後まで延期します。相手がabortせずcontinuationを放棄するとchannelのbusyが残る場合があります。

metadata continuation検査 は生成SDK serializerと共通framingを実行し、L2CAPのみ模擬して長文属性・continuation/abort・保持buffer・通知順を検査します。無線やfacadeの所有コピー自体の試験ではありません。

同梱サンプルのビルドと実行可能な検査手順は[ビルドガイド](build_guide_jp.md#compile-only)を参照してください。上記の機能別検証範囲をすべて再実行する手順ではありません。
