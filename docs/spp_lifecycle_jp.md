# SPPのpeer再設定とERTM統合

[English](spp_lifecycle.md) · [bond保存先の選択](bluetooth_bond_storage_jp.md)

停止境界でのpeer選択と、有限のRFCOMM ERTMバッファ管理を追加しました。host tests・SDKビルドと無線相互接続性は区別します。**ERTM再送、無線接続先の切替、電源断、永続bondの復元は実機認定していません。**

## 他profileをリセットせず許可peerを変更

`Client::selectPeer(Address)` は、このSPP facadeが同時に1台だけ許可するpeerの選択をキューに入れます。A2DP/AVRCPの相手やペアリング方針は変更しません。

1. 全SPP routeを切断し、`poll()` で全てClosedになるまで処理します。
2. 実行中SDPをcancelしてdrainします。Cancelledでも `query.busy` がtrueなら未完です。
3. SDK bindingとERTM multiplexerのbuffer lease返却を待ちます。DLCが閉じてもmultiplexer領域の返却完了とは限りません。
4. `selectPeer` を呼びます。falseは未受付、trueはキュー受付です。poll後の `snapshot().config.peer` とhistoryで結果を確認します。

ゼロアドレスは拒否します。coreもroute活動中・SDP busy・endpoint登録/解除中を拒否します。Listening endpointの登録は維持しますが、以後の着信許可先は新peerです。旧offerは元のSDK識別子で拒否し、新peerとして読み替えません。

変更成功時はroute generationと旧query結果を無効化します。遅延した旧peerのACL/SDP/data通知を新peerに転用しません。**他profileの既存ACLは切断しません。** 新peerが既に別profileのACLを持つ場合、実際のlink/epochを取り込みます。このepochが前peerのepochより古くても構いません。これは順次1peerの再設定であり、複数peer同時SPP、自動探索、再接続方針、bond削除ではありません。

## ERTMのビルド設定

consumerとそのBTstack全ソースで、両方のdefineが必要です。

```c
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE_FOR_RFCOMM
```

`Client::ertmCompiled()` がこの構成を返します。AVRCP browsingは前者だけを必要とする場合があります。browsingのためだけにRFCOMM用flagまで有効にしないでください。browsingとRFCOMMのpool/callback/bufferは別ですが、HCI/L2CAP容量は共有します。

[SPP CMake helper](../cmake/picow_spp.cmake) が検証済みtransport overlayも選択します。`PICOW_SPP_SDK_OVERLAY` のないRFCOMM ERTMビルドは拒否します。このmarkerを手動定義せずhelperを使ってください。両transport flagが揃わなければ `configureErtm` はfalseで、通常SPPはBasicのままです。両flagがあってもbuffer pool未設定ならSDKはBasicを使えます。ERTMをコンパイルしただけでは、実際のネゴシエーション成功を証明しません。

## アプリ所有メモリと変更不可の初期設定

`begin` 前に `configureErtm(const ErtmPool::Config&)` を呼びます。engine停止中かつ未処理イベントなしで、1度だけ設定できます。config自体はコピーしますが、1～2個の実バッファはアプリ所有です。Clientの全寿命を通して有効にしてください。切断要求、timeout、radio faultだけでlease中領域を解放・再利用してはいけません。対応するSDK releaseを待ちます。終端失敗/resetでも、観測していないreleaseを捏造しません。

leaseはrouteごとではなくRFCOMM multiplexerごとです。同peerへの2routeは通常1leaseを共有します。IDで重複・別IDのreleaseによる誤解放を防ぎます。peer変更は全lease返却後です。上限2leaseで、heap確保しません。

初期提案は任意ERTM、最大再送8回、再送timeout2000ms、monitor timeout12000ms、local MTU1000、RX/TX各4buffer、FCSなしです。bufferとcountは明示設定してください。アダプタは次を検査します。

- null/重複メモリ、pointer/size overflow。
- MTU48～1029、FCS/mandatory flag、再送回数1以上、SDK最低timeout。
- enhanced-controlのsequence上限に合わせ、RX/TX数はそれぞれ独立に1～63。
- SDKが実際に進める1～16byteのalignment、state配列、再構成領域、MPS最低48。
- 共有 `HCI_ACL_PAYLOAD_SIZE` にL2CAP header、enhanced control、FCSを含めても収まる最大MPS。大きすぎる実バッファは、安全な使用サイズに切り詰めます。

`ertmStatus()` は設定状態、要求/取得/返却/現在lease数、不足、対象外peer、予期しないreleaseを返します。これは確保状況であり、**ネゴシエーション済みmodeや再送回数ではありません。**

`transport(Handle)` は現在OpenのRFCOMMとClassic L2CAPの実transportを調べ、`Client::Transport::Basic`、`Ertm`、`Unknown` を返します。不正・古い・切断済みhandle、失効したACL、Openを確認できないtransportは `Unknown` です。保持bufferや要求configからmodeを推定しません。facadeと同じmain loopから呼んでください。

## fallbackと終端失敗

`link.ertm_mandatory=0` ではlease不足時にBasicへfallbackできます。相手との調整でBasicへ移り、ERTM領域が返却される場合もあります。`ertm_mandatory=1` ではローカル容量不足をBasic成功扱いせず、終端runtime faultにします。共有radioをOFFにするため、他profileにも影響します。用途に合うpoolを用意し、この失敗方針が不適切ならmandatoryを選ばないでください。

transport overlay v3は、ERTMが任意なら相手の明示的なBasic-mode RFC optionを直ちに受け入れ、ゼロのERTM parameterを取り込みません。ERTM leaseは1回だけ返却します。応答待ちconfiguration requestのmodeとchannelの選択modeを別に管理し、行き違った旧ERTM要求への応答をBasic再要求の拒否と誤認したり、modeが不一致のままOpenにしたりしません。古い応答・重複応答は無視します。実際のBasic再要求が拒否された場合は切断し、mandatory ERTMでは引き続きBasic-only peerを拒否します。

選択対象でなかったpeerが、DLCなしのBasic RFCOMM muxをすでに持つ場合があります。`selectPeer` 後にそれを再利用すると、SDKのERTM request callbackを通りません。そのためmandatory接続をOpenとして公開する前に実transportを検査します。BasicまたはUnknownならOpenにせず終端runtime fault（`0x11`）にします。容量不足と同様、共有radioが停止します。別ACLや失効済みACLのcallbackで他接続を誤って停止させません。

固定SDKのERTM request callbackには拒否の戻り値がなく、mandatory時のローカル容量不足は引き続き終端失敗です。一方、原本の発信L2CAP生成失敗経路にあったlist残存・ERTM release欠落はoverlayで修正しました。失敗した両objectをunlinkし、戻る前に1回だけreleaseします。facadeはroute局所の失敗として記録し、共有runtimeを維持して正常な再試行を許可します。修正済み経路でreleaseを重ねたり他profileを停止したりしません。

## どちらのビルドでも送信所有権を維持

Basic-onlyビルドは共有L2CAP bufferをreserve/releaseします。RFCOMM ERTMビルドはSDK独自の1030byte scratch bufferを持ち、SDKのreserve/release関数を呼んではいけません。アダプタではこれらをno-opとし、readinessとprepared-sendの実戻り値は検査します。その後BTstackがL2CAP所有領域へコピーします。一時エラー時はアプリFIFOを保持して再試行します。最終send errorを隠すSDKの `rfcomm_send` に置換しないでください。

## 検証済みtransport overlayと残る受入

`picow_enable_spp_sdk_overlay(target)` は原本SHAを検査し、consumerのbuild directoryへ `l2cap.c` / `rfcomm.c` を生成します。SDK本体は編集しません。target propertyのあるconsumerだけが選択し、無関係なconsumerは原本を使います。AVRCP overlayとも併用できます。未知hash・source配置の変化・原本への出力は拒否し、原著作権表示を保持します。

overlayはremote MPS、6bit transmit window、最低MTU、L2CAP option header/payload全体を算術や状態更新前に検査します。TX容量を広い型で計算してslot数を制限し、TX領域の計算を訂正します。任意Basic fallbackを維持し、古いconfiguration flagで拒否済みchannelが開かないようにします。RFCOMM同期確保失敗cleanupとMSC eventの長さbyteも修正します。試験は生成した実SDK state machineをコンパイルし、原本の反例も含めます。代替モデルだけの試験ではありません。

これは対象を絞った防御で、Bluetooth stack全体の監査や無線認証ではありません。ローカル容量、不正negotiation fixture、SDKビルドだけで再送時刻、loss回復、相互接続性、他profileとの性能は証明しません。これらの実機受入項目は残ります。

2route、同時に選択するpeerは1台、RFCOMM frame最大1024、再試行方針はアプリ所有という境界も残ります。これは明示的resource契約で、無制限接続ではありません。他profile併用は、各roleの個別ビルドだけでなく組合せビルド・実機検証が必要です。

## 試験と必要な実機確認

同梱検査は[ビルドガイド](build_guide_jp.md#compile-only)を参照してください。

pool試験は検証済みSDK overlayの実setup/configure関数を抽出・コンパイルし、alignment、独立RX/TX数、容量不足、最大windowを検査します。正確な件数は保存ログを参照してください。adapter試験は実SDK宣言と通信mockを使い、両ビルドでFIFO再試行、ERTM時のreserve禁止、lease返却、同期生成失敗と正常再試行、mandatory容量不足、既存Basic/unknown muxの再利用拒否、peer generation無効化を確認します。Bluetooth再送のシミュレーションではありません。

v3の機能回帰試験は実L2CAP dispatcher、configuration state machine、signaling serializerを実行します。要求・応答が行き違う両順序、Basic Openと1回のrelease、mandatory拒否、正常ERTM、旧ERTM要求へのpending/成功応答、古い・重複した識別子、未知CIDを検査します。fallback修正の証跡では、v2の反例とv3のhost/ARM検証を無線試験と分けて記録します。

要件を閉じる前に、別手段でERTMを確認できる相手とのpublic API発信/着信、Basic fallbackとmandatory拒否、2route全二重負荷、loss/retransmission、resource failure、lease漏れのない切断/再接続、A2DP/AVRCPを妨げない実2peer間の切替、browsing/A2DP/SPP共有容量を確認します。永続bondのreset/電源断試験は選択した保存backendで行い、実鍵をログへ出しません。[bond保存](bluetooth_bond_storage_jp.md) を参照してください。

証跡: host/ARM結果とSHA256。

[オフライン既定の制御・peer切替利用例](spp_examples_jp.md)

## 実機で確認できた範囲

限定した相手との Basic fallback、実 ERTM の二路転送、通常切断と再接続、15分間の一つの負荷を確認しています。これは損失時再送、電源断、2台の実 peer 間の切替、新規 Pico 起点 ERTM L2CAP 接続、他 profile との同時動作の保証ではありません。中断したアプリデータの自動再送もありません。過去の失敗を含む計測値・判定は [検証範囲](validation_jp.md) を参照してください。
