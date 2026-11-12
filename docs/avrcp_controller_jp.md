# AVRCP Controller: メディア操作を送る

[English](avrcp_controller.md)

Controller は設定した相手へメディアボタン、metadata/capability query、通知登録、音量/item 操作を送ります。`begin` → `poll` → 接続観測 → 操作 → 応答確認の順で使います。公開サンプルは [メディア操作例](../examples/AvrcpMediaControlSample/README_jp.md)を参照してください。限定した実機観測はありますが、全機能・任意相手の相互接続保証ではありません。Browsing と Cover Art は別 facade です。

## APIと所有権

`AvrcpController.h` の `picow::avrcp::AvrcpController` を使います。CMakeでは `cmake/picow_avrcp_controller.cmake` をincludeし、`picow_target_avrcp_controller(target config_directory)` を呼びます。helperはTargetソースも含むため、同じアプリで片方または両方のownerを作れます。2 peerにはAVRCP connection objectを最低4個設定します。BTstackはsignalling接続ごとにController/Target両objectを使います。

- `Config`は異なる非ゼロpeerアドレス1〜2件。`begin(config)`はownerごとに一度、`stop()`は終端です。process lifetimeのobjectを推奨します。
- `connect(peer)`は明示的にoutgoing接続を開始し、設定peerからのincomingも受けます。戻り値0はSDK要求受理で、相手との接続完了ではありません。`Connected`と`bindingForPeer()`を観察します。
- `Binding`はpeer、ACL epoch、CID、owner内単調増加generationを含み、`press`/`release`/`disconnect`は古いbindingを拒否します。2 peerは独立slotです。
- `press(binding, operation)`の対象は固定SDKの標準no-operand operation ID全域（navigation/menu、numeric/channel、power/media、angle/subpicture、F1〜F5）です。予約IDと任意vendor-unique operand操作は拒否し、next/previous groupのvendor-unique clickは別の`group(binding, next)`で扱います。BTstackのpress-and-holdを使い、**releaseまでSDKが押下を反復します**。短押しでも`release(binding)`を呼び、pressだけで解放されると解釈しません。
- press応答前にもreleaseを要求できます。この場合SDKは個別press応答を通知せずcompletionだけ通知することがあります。同じpeerでコマンド待機中は追加pressを拒否します。
- `PressResponse`/`ReleaseResponse`は相手のAV/C応答byte（9 accepted、10 rejected、8 not implemented）を公開します。ローカルAPIの0は相手ACKではありません。拒否pressにはSDK反復を止めるreleaseを要求し、後の応答は別に扱います。
- callbackは同期実行。callback内からの変更APIは拒否するため、観測をアプリqueueへコピーしてmain loopで操作します。callbackの参照はその呼出し中だけ有効です。
- `poll()`は共有Bluetooth runtimeを進めます。IRQ/別core/thread-safe APIではありません。

## Target/Controller共有ライフサイクル

`AvrcpShared`はSDK signallingと内部両role handlerを一度だけ初期化します。SDKの`avrcp_init()`はL2CAP登録済みでもconnection listを消すため、二度目の初期化はできません。両application roleは単一のruntime AVRCP枠を共有し、radioを重複初期化しません。

Target/Controllerは別SDP record・callbackを持ち、同じsignalling channelを観察できます。片ownerのstopは自分のSDP登録・状態を解除し、他roleが使用中のchannelを維持します。Controller stopは保持中コマンドのreleaseも要求します。release失敗時は無期限の反復を避けるためsignallingを切断し、この安全経路は両roleへ影響します。
明示`disconnect(binding)`は同じCIDの両AVRCP roleを切断します。A2DP/SPPの切断やradio OFFを意図する操作ではありません。

`AvrcpTarget` も `connect(peer)` / `disconnect(binding)` に対応します。outgoingはSDKの直列化されたSDP queryを使い、SPPが共有SDPを所有中、またはSDK SDP clientがbusyなら拒否します。SDP再初期化や所有権の迂回は行いません。
pairing/discoverabilityは共有アプリpolicyに従い、これらのAPIはpairingの無条件承認や全体の可視化を行いません。

## 検証と残る工程

- 共有 Controller/Target 経路の host 回帰: 共有roleのライフサイクル検査。実機接続交渉・timing の保証ではありません。

- Controller adapter検査: 実SDK headerとmock transport。両登録順、全128操作IDの対応/予約値判定、group、query dispatchと引数検証、press/release応答、拒否、先行release、ローカル失敗rollback、2 peer、旧epoch/generation、不正packet、stop、初期化失敗。ASan/UBSan・warnings-as-errors。
- Target adapter検査: 既存Target adapter回帰。
- runtime/core検査: runtime/core/probe回帰。
- `examples/PicoAvrcpCompileProbe`: 両roleを含むARM compile/link。既定起動ではradioを初期化しません。newlib `_getentropy`警告は残ります。

未確認は実Target/Controller同時動作、任意peer、長時間、host確認範囲外の障害回復です。限定した Controller の実機観測は[検証範囲](validation_jp.md)を参照してください。任意の command deadline と signalling 接続試行の取消しは下記を参照してください。SDK callbackはgenerationを含まずCID再利用後の古い生packet再投入は区別できないため、callbackを保存して再投入しません。
これらの API は AVRCP の完全準拠を保証しません。

## capability・metadata・notification・item adapter

- `query(binding, Query)`はUnitInfo、SubunitInfo、SupportedCompanies、SupportedEvents、PlayStatus、NowPlayingInfo、SettingAttributes、SettingAttributeText、SettingValueText、ShuffleAndRepeatをSDKへ渡します。
- `notification(binding, eventId, enable)`はevent ID 1〜13の登録／再登録停止をSDKへ要求し、相手の対応とcapability cacheでローカル受付が決まります。登録済みへの`false`は次のCHANGED応答まで保留され、即時の解除frameは送りません。Controller overlay v5では登録がqueue中、または送信済みでINTERIM待ちなら`ERROR_CODE_COMMAND_DISALLOWED`（12）を返します。これは解除未受付なので、アプリ側の待機上限を設け、callback外の後続main loopで必要に応じて再試行します。INTERIM未応答への自動取消timeoutはありません。完全未登録への解除は成功no-opのままです。再現した欠陥と修正後の正常経路証拠。
- `elementAttributes`、`settingValues`、`absoluteVolume`（0〜127）、`batteryStatus`、`addressedPlayer`、`maximumFragments`、`playItem`、`addItem`は引数範囲を検査します。metadata IDとitem UIDは固定SDK内で同期コピーされ、これらadapterは呼出し元領域を保持しません。
- `setSdkCallback`はlive Bindingを添えたSDK event bytesを公開します。packet envelope/CID/epochを検査しますが、入れ子の可変長fieldは解釈しません。必ずsizeを確認してdecodeし、非同期利用前にコピーします。callback中の変更操作は拒否します。ローカルquery成功と相手応答は別です。

## player settingsのSDK overlay修正

CMake helperはhash固定の[SDK overlay](avrcp_sdk_overlay_jp.md)をbuild directoryに生成して選択します。installed SDK原本は変更せず、原ライセンスを維持します。未知のsource bytesはconfigureを停止します。原SDKのsettings data callback欠落、setting pair count/value-text要求の不正、charsetのcount/length/index/byte-order不良を修正しました。host試験では原本の欠陥も独立再現し、修正結果と照合します。

- `displayableCharsets`はcharset IDを1〜5個コピーし、count＋big-endian 16bitで送信します。`settingAttributeText`/`currentSettings`はID 1〜10個、`settingValueText`は明示attributeとvalue 1〜9個、`setSettings`はID/value pair 1〜5組です。上限は固定SDKの所有command buffer 11byteによります。
- `shuffleMode`/`repeatMode`は修正済みの1 pair serializerを使います。SDKの引数なしvalue-text queryはrepeat attribute 2、value 1〜4を明示要求する形に修正しました。他の値は`settingValueText`で指定します。ローカル成功は相手の受理とは別です。
- overlayなしでbuildした場合、該当 method は既知の不正serializerを呼ばずunsupportedを返します。
- `setSettingsCallback`はlive Binding、response、PDU、全体byte数、offset、借用bytesを含む`SettingsChunk`を同期通知します。callback中にコピーしてoffset順に結合し、`final()`後に完全な応答をdecodeします。payload内はAVRCP network byte orderを維持します。空のaccepted/rejected payloadでも1回通知します。
- 独自subevent `0xf0`のheaderは11byte：HCI event code、event length、subevent、CID LE16、AV/C response、PDU、total LE16、offset LE16、続いて最大246byteのpayloadです。これは本repositoryの拡張でupstream BTstack eventではありません。overlayは全list/pair/text長、transaction label、要求PDUを確認してからchunkを通知します。facadeはchunk範囲/順序とlive接続を確認し、入れ子payloadを別途解釈しません。
- Controller overlay検査は実SDK parser/serializerをASan/UBSanで実行し、正常・切詰め・不一致応答、要求byte列、chunk分割、生成再現性、未知hash拒否を検証します。facade試験はoverlay有効/無効の両方で実行します。host試験であり無線相互接続の証拠ではありません。

vendor-PDU settings応答はSTART/CONTINUE/ENDを再構成し、2 peer分の所有bufferへ各最大4096byteをコピーします。継続要求ごとに新transaction labelと元PDU IDを指定し、途中dataは通知しません。`maximumFragments`の上限はSTARTも含み、上限・buffer超過・完了時の構造不正にはAbortContinuingResponseを送り途中状態を破棄します。stop、ACL reset、signalling releaseでも破棄します。facadeなしでC拡張を使う場合はrelease時に`picow_avrcp_controller_forget_settings(cid)`が必要です。欠落断片には下記のcommand deadline、または明示disconnectを使います。AVCTP transport分割とは別機能です。PID/company/PDU/transaction不一致はsettings transactionを消費せず拒否します。

custom command は下記の有界 owned buffer、transport 分割、vendor continuation、登録済み通知との併用に対応します。Browsing Controller/Target と Cover Art Client は別 facade と契約を使います。Cover Art の実機受入は未完です。

Target 側の metadata/notification API は[Target機能対応表](avrcp_target_features_jp.md)を参照。role claimは破棄後も保持し、同じprocess内でownerを作り直してgeneration保護がリセットされることを防ぎます。

[Browsing ガイド](avrcp_browsing_jp.md)に、専用 transport、上限、検証範囲を記載しています。

[Cover Art Client対応範囲](avrcp_cover_art_jp.md)は固定SDKのClient APIを扱います。SDKには画像serverがありません。

[検証範囲](validation_jp.md)

## Command deadlineとpending接続取消し

`setCommandTimeout(milliseconds)`は0（既定無効）～0x7fffffff。単調uint32ミリ秒を`poll(now_ms)`へ渡し、時刻付きpoll間隔は2^31 ms未満。busy command/press/releaseを最初に観測したpollから絶対予算を開始し、fragmentで延長しません。設定変更は次の計時開始へ適用。引数なしpollはruntimeとcustom送信retryのみ。応答待ちでないholdやidle通知登録は対象外、custom INTERIM後のCHANGED待ちは対象。callback内変更は拒否します。

期限切れはTimeoutを1回通知しbindingを隔離して共有signalling切断を要求します。両roleと従属transportへ影響し、ローカル失敗はretry、受付後は実release待ち。遅延応答が新transactionへ混入しないようlive transactionを再利用しません。期限内releaseの保証ではありません。

Controller/Target の両方が `cancelConnection(peer)`、`cancellationPending(peer)`、`cancellationStatus(peer)` を提供します。許可peerの共有pending接続を取消し、一方のroleだけの取消しではありません。戻り0は受付で同期完了の場合もあり、pending中は再接続/資源解放しません。queued/active SDPはshared callbackと所有権を完了まで保持。pending L2CAPはOPENED失敗、または遅延成功後のdisconnectを待ちます。取消済み接続をConnectedと通知せず、pair清掃後だけRELEASEDを通知。遅延disconnect失敗はstatusで取得でき、pending保持のままcancelConnectionで再試行できます。

接続が存在しなければpending=false/status=0です。自分の取消し受付後に確認し、別の新接続成功の証拠にはしません。確立済み通常接続は拒否し、disconnect(binding)を使用。下位cancel拡張はBrowsingが接続している場合も副作用なしで拒否するので先にその終了を待ちます。facadeに自動接続timerはありませんが、アプリの同一main loopで接続期限到来時にcancelConnectionできます。受付は即解放ではなく、pollとstatus確認を継続。callback内取消しは拒否。overlayなしでは取消し/statusは非対応。生APIはpicow_avrcp_cancel/pending/statusです。

## Owned custom vendor command

`customCommand(binding, command_type, subunit_type, subunit_id, pdu, company, bytes, length)`は静的2peer領域へ要求をコピー。command 0–4、subunit type 0–31、ID 0–7、company 24bit。parameter最大502byteでvendor header 10byteと合わせAV/C 512byte、空入力も有効。MTU>=48でmin(MTU,512)に合わせAVCTP SINGLE/START/CONTINUE/END送信し、最初のpacket countも付与。失敗時offsetを進めずpollで同じframeをretry。戻り後に入力解放可能。overlayなしは非対応で、任意uint16長のvendor commandを保証する契約ではありません。

`setCustomCallback`のCustomChunkはbinding/response/元PDU/total/offset/借用byte。subevent 0xf1はrepository拡張。PID/label/response bit/subunit/opcode/company/PDU/宣言長を検証し、AVCTPはAV/C 4096byte（1応答fragmentのparameter 4086byte）へ再構成。さらにvendor START/CONTINUE/ENDを別の4096byte parameter領域へ集約します。continuationはcompany/subunit/元PDUを保持し、通知labelを避ける新labelと、STARTを含むSDK fragment予算を使用。途中aggregateは渡さず、容量/予算超過はAbortContinuingResponse。continuationの相手拒否/非対応は元PDUの結果として通知。INTERIMは待ち継続、最終/CHANGEDで完了。

`EventKind::CustomError`はローカル失敗で、operationは元PDU、responseは2=容量、3=fragment予算、4=送信失敗。偽の相手成功にはしません。送信失敗はretry可能、abortは相手応答またはcommand deadline/disconnect待ち。不正/不一致応答は完了扱いにしません。生C利用時はcustom_poll(cid)でretryし、release確認後だけforget_custom(cid)。取消し/owner破棄時も要求byteを早期消去しません。

送信済み標準通知登録をevent ID/labelで追跡し、custom分割/continuation中でも妥当なinterim/changed/rejectedをSDKへ配送。custom transactionは変更せず、再登録送信はcustom完了までqueueします。長さと識別子を検査し、曖昧な拒否labelで別購読を解除しません。全SDK通知の意味decoderを新規実装したという主張ではありません。標準PDUと同じ番号のcustomでも独自company/PDUの経路を保持します。

Custom command の host 回帰は MTU48–512、retry、continuation、通知併用と二 peer の境界を検査します。実機相互接続や timing の保証ではありません。

同梱サンプルのビルドと実行可能な検査手順は[ビルドガイド](build_guide_jp.md#compile-only)を参照してください。上記の機能別検証範囲をすべて再実行する手順ではありません。
