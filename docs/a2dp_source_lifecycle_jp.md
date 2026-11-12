# A2DP Source: 接続受付とストリーム操作

日本語 | [English](a2dp_source_lifecycle.md)

既存の [A2dpSourcePlayer API](api_reference_jp.md) に、SBC・PCM 経路の操作を追加したものです。A2DP SDK の全操作や同peerでのSource/Sink同時mediaの完成を意味しません。下記の host/ARM 検査と、実機の相互接続結果は区別します。

## 追加した操作

| API | 受け付ける状態と効果 |
|---|---|
| `listen(true)` | `begin()` 後に Working となり、探索・発信接続中ではない場合。指定 peer からの signaling 接続を受け付け、共有無線を接続可能にする。発見可能にはしない。 |
| `listen(false)` | 新たな接続受付を止める。現在の接続は切らず、他 profile が必要とする共有無線の接続可能設定も解除しない。 |
| `pause()` | Playing ならローカルの音声生成を止め、SDK に SUSPEND を要求。Open なら自動開始を取り消し、無線の SUSPEND は送らない。 |
| `resume()` | Open または Suspended から SDK に START を要求する。 |
| `reconfigureSamplingFrequency(rate)` | 可変 rate を明示許可した Open/Suspended から、44,100 または 48,000 Hz への変更を要求する。変更後に自動再開しない。 |
| `disconnect()` | 接続受付を止め、この signaling 接続を閉じる。終端エラー後も終了処理として呼べる。 |

操作の `true` は**要求を受け付けたこと**を表し、相手側での完了ではありません。`snapshot().stream`、`error`、`status` と履歴を確認してください。`SuspendRequested`、`Suspended`、`ReconfigureRequested` は列挙の末尾に追加し、既存値は維持しています。SDK が即座に非ゼロを返した操作は、直前の安定状態へ戻します。相手側の START/SUSPEND 拒否は、導入済み SDK も内部設定状態を変更するため終端エラーです。再設定の拒否は旧形式を維持し、一時停止状態にします。自動リトライは追加していません。

`Settings.control_timeout_us` は既定10秒、0は不可です。こちらから要求した START/SUSPEND/RECONFIGURE が時間内に完了しなければ、送信を止めて切断を要求します。この変更で接続受付・設定全体のタイムアウトを新設したわけではありません。

## 既定動作とアプリ側の準備

`target_address` にはゼロでない Bluetooth アドレスを指定します。`00:00:00:00:00:00` は任意の相手を意味せず、`InvalidSettings` になります。

既存アプリの既定値は固定 rate の SBC と自動開始です。明示的に待ち受ける場合は `auto_connect=false` にしてください。true のままだと起動時に探索を始めている可能性があります。`auto_start=false` なら、Open 後は `resume()` または相手からの START まで待ちます。`start_delay_us` はローカルの自動開始への遅延で、相手の START を遅延させる値ではありません。

rate 変更を許可する場合は `allow_sample_rate_change=true` と `configure_source` callback を設定します。callback のない可変 rate 設定では `begin()` が `InvalidSettings` を返します。callback は最初の設定成立時と、その後の形式変更成立時に呼ばれます。指定 rate の PCM を供給できるよう音源を設定し、不可能なら false を返してください。callback 内から player の操作を再度呼んではいけません。リサンプラーはありません。

```cpp
static bool configure_silence(uint32_t rate, void *) {
    return rate == 44100 || rate == 48000;
}
// A silence generator can accept either rate without changing sample values.
// A waveform, decoder or capture source must actually change its PCM production.
A2dpSourcePlayer::Settings settings;
settings.target_address = "12:34:56:78:9A:BC"; // Your permitted receiver.
settings.auto_connect = false;
settings.auto_start = false;
settings.allow_sample_rate_change = true;
settings.configure_source = configure_silence;
// Construct A2dpSourcePlayer with your IAudioSource, then begin() and poll().
```

この例の無音源なら rate が変わってもサンプル値は同じです。波形生成・デコード・収録源では、PCM の生成速度や形式を実際に変更する必要があります。

こちらから形式を変える場合は、Open/Suspended を待って再設定を要求し、`stream == Suspended`、`error == None`、`status == 0` と要求した `codec.rate` を確認してから `resume()` します。true が返った瞬間に PCM 形式が変わったと判断しないでください。音源 callback と encoder を変更するのは、成功した再設定イベント列を受けた後です。

pause は蓄積した PCM と符号化済みデータを破棄します。resume では RTP の sequence/timestamp counter を保持し、停止中の経過時間を追いかけず、新しい開始時刻から送信を進めます。`play_duration_us` は成功した START ごとの再生区間を制限するため、resume すると新しい時間区間が始まります。packet/sample の累計は保持します。

## 相手からの接続・操作

接続受付では固定 peer、CID、イベントに含まれる local SEID、共有 runtime の現在の ACL handle/epoch を確認します。受け付けた ACL が別世代へ置き換わっても、そのまま stream を復活させません。listen の停止は既存接続を取り消さないため、切断には `disconnect()` を使います。この検査は runtime 上の接続の識別であり、追加の認証プロトコルではありません。

SBC 設定 validator は、非対応の mode/rate を相手からの設定受理前に拒否します。対象経路は引き続き stereo・16 blocks・8 subbands・loudness allocation・bitpool 2〜53 です。可変 rate を無効にした場合は、最初に指定した sample rate のみ受け付けます。相手からの START/SUSPEND で状態を更新し、通常の一時停止を非対応操作エラーにはしません。相手からの再設定は Open/Suspended 中だけ適用します。STOP/RELEASE は送信 credit を無効にし、同じ設定済みendpointをOPENし直せるよう形式を保持します。Idle中の新しい設定で形式を置き換えられ、signaling切断時にはクリアします。遅延報告は `snapshot().delay_100us` と `delay_reports` に記録し、1単位は100マイクロ秒です。

## media payloadへの適応と有界断片送信

[完全frameの旧checkpoint](source_mtu_batching_contract_jp.md)は1 packetあたり1〜4個を選びます。現在は[有界Source SBC断片送信](source_sbc_fragmentation_contract_jp.md)を追加し、完全frameが収まらない場合も対応する1 frameを2〜15断片で送ります。物理packet数は断片ごと、sample数・送信間隔・media完了は最後の断片だけで進め、Busy中は同一の符号化データを保持します。pause／再設定／切断では未完batchを破棄します。codec制約は不変です。実機相互接続や任意 codec の保証ではありません。

## 検証と境界


導入済み Pico SDK 2.1.1 系環境で実行する検査です。

同梱検査は[ビルドガイド](build_guide_jp.md#compile-only)を参照してください。

lifecycle suite は実 SDK ヘッダーと SDK 操作の mock を使い、ASan/UBSan で検査します。ゼロアドレス拒否、受付・接続識別、Inquiry中のSDK設定処理前での拒否、重複/不正イベント、相手/自分からの suspend/start、再設定成功/拒否、即時エラー、timeout、古い ACL/credit/CID、STOP、イベント順序、時計上限を対象とします。既存 ACL と、実 SBC encoder/transport/pacing の回帰検査は別に実施しています。全追加 API を呼ぶビルド専用 Pico W アプリで ELF/UF2 を生成しました。既知の newlib `_getentropy` 警告は残ります。

この変更の検査では新たな書込みや無線通信を行っていません。相手側が Source への着信、suspend/resume、rate 再設定を受け付けるかは実機未確認です。SourceとSinkは、**別々の固定peer**を使う場合に同時登録できます。`btstack_config.h` のAVDTP connectionとendpointをそれぞれ2以上にします。共有raw AVDTP shimは、アプリへの通知後ではなく、SDKの設定処理へ入る前に各roleの固定peerだけを通します。未知の相手や、その時点で受付を許可していない相手との接続成功は、endpoint探索を始めずに切断します。初期受付はInquiry中を除外し、着信の現在のACLも検証します。探索待ちを再開する許可は別に判定し、有効な接続だけを再開します。通知をアプリが無視するだけでは、SDKが不要なendpoint探索を始めるためです。ローカル切断にも他roleのCID所有確認を入れています。別のhost回帰では実SDKの `a2dp.c` をリンクし、roleごとの設定状態の変化、SUSPEND/RELEASEの配送、未知/受付不可peerの拒否、releaseまたは設定完了後の探索待ち再開、取り消し済み探索の再開抑止、gateを迂回したnegative controlを確認します。下位通信・timer・接続lookupはmockで、codec交渉全体の試験ではありません。

同peerの二mediaは登録時に拒否します。導入SDKはconnection単位に一つの `configuration_state` を持ち、別endpointでも二つ目のSET_CONFIGURATIONを拒否するためです。同peerでresetなしのrole切替や逆方向streamの同時実行は残件です。別peerの同時登録を、それらの成功証拠にはしません。任意 codec 設定、非SBCのraw media、複数endpoint、呼び出し側によるSEP寿命管理、汎用MTU調整は今回の追加対象ではありません。この一部実装を SDK 全機能対応と表現しないでください。
