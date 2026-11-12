# アプリケーション API リファレンス

最新拡張：[Source lifecycle制御](a2dp_source_lifecycle_jp.md)、[AVRCP Controller](avrcp_controller_jp.md)、[SPP制御・port API](spp_control_jp.md)、[SPP lifecycle/ERTM](spp_lifecycle_jp.md)、[任意のbond保存](bluetooth_bond_storage_jp.md)。追加APIの契約と検証範囲はこれらの補助リファレンスを参照してください。

[任意のSink START判定](sink_start_decisions_jp.md)：`startDecisionsAvailable()`、`pendingStart()`、`decideStart(request, accept)`は専用build helperと`Settings::require_start_decision`で有効化します。要求token全体を保持してcallback外で判断し、ローカル受付とstreamingを区別してください。

受信側の新APIは検証中です：[A2dpSinkReceiver / IAudioSink](../examples/A2DPSinkReceiver/README_jp.md)。Source/Sinkと併用を含む要件全体は[SDK棚卸し・受入残作業](a2dp_sdk_coverage_jp.md)を参照してください。本書の存在を全機能完了と読み替えないでください。

[English](api_reference.md) · [プロジェクト概要](../README_jp.md) · [実行できる開発用サンプル](../examples/DeveloperSamples/README_jp.md)

現在のヘッダと実装に基づく、単一メインループ向け API の説明です。要求を受け付けたことと、Bluetooth 接続が完了したことは区別してください。実機確認の範囲や実験段階の機能は[概要](../README_jp.md)を参照してください。ここに API があることは、すべての機器での動作保証を意味しません。

実用例：[SPPの制御・順次相手切替](spp_examples_jp.md)、[Sink PCM FIFO](../examples/A2dpSinkPcmConsumer/README_jp.md)、[AVRCPの利用順序とリンク検査](avrcp_examples_jp.md)。AVRCPの詳細契約は[Target](avrcp_target_features_jp.md)、[Controller](avrcp_controller_jp.md)、[Browsing](avrcp_browsing_jp.md)、[Cover Art Client](avrcp_cover_art_jp.md)へ。各APIの存在と実機受入は別です。

## 目的から選ぶ

| 目的 | include | API |
| --- | --- | --- |
| Bluetooth音源からPCMを受け取る | `A2dpSinkReceiver.h` | `A2dpSinkReceiver`, `IAudioSink` |
| PCM 音声を Bluetooth スピーカーへ送る | `A2dpSourcePlayer.h` | `A2dpSourcePlayer`, `IAudioSource` |
| RFCOMM でシリアルデータを送受信する | `SppClient.h` | `spp::Client` |
| 相手機器のメディアボタンを受け取る | `AvrcpTarget.h` | `picow::avrcp::AvrcpTarget` |
| 相手へメディア操作・問い合わせを送る | `AvrcpController.h` | `picow::avrcp::AvrcpController` |
| Browsingの要求・応答を扱う | `AvrcpBrowsingController.h` | `picow::avrcp::AvrcpBrowsingController` |
| Cover Artの画像・属性chunkを受け取る | `AvrcpCoverArt.h` | `picow::avrcp::AvrcpCoverArt` |
| 共有 ACL の確認、独自アダプタの実装 | `BluetoothRuntime.h` | `BluetoothRuntime`（上級者向け） |

以下の CMake ヘルパーを使うと、この include 名で利用できます。ソースは [A2DP](../audio-core/A2dpSourcePlayer.h)、[PCM](../audio-core/IAudioSource.h)、[SPP](../spp-sdk/SppClient.h)、[AVRCP](../avrcp-sdk/AvrcpTarget.h)、[runtime](../bluetooth-runtime/BluetoothRuntime.h) です。

### 共通の約束

- C++17 と Pico SDK の polling 構成を使います。メソッド、コールバック、`poll()` はすべて同じメインループから実行し、IRQ、別コア、別スレッドから呼ばないでください。
- 各 facade / role の owner は共有所有契約に従って一つだけ作り、音源・PCM受取先・owner・コールバックの context をプログラム終了まで保持します。破棄・再作成による再起動はサポートしていません。`begin()` は再接続用ではありません。
- 無線はプロファイル間で共有します。アプリ側で先に `cyw43_arch_init()` を呼んだり、旧 Bluetooth plugin や別の BTstack プロファイル実装を混ぜたりしないでください。
- 有効な各プロファイルの `poll()` をこまめに呼びます。長い sleep、待ち続けるログ出力、重いコールバックは避けてください。各 facade が共有 runtime の処理も進めます。
- ACL ハンドル、要求受付、`begin()`、SDP 成功だけで認証完了と判断しないでください。プロファイル接続とセキュリティ処理は非同期です。
- 共有 runtime のリンクキーは既定でメモリ上に保持され、リセットをまたいで永続化されません。

### CMake への組み込み

`pico_sdk_init()` と `add_executable(app main.cpp)` の後に記述します。

```cmake
set(PICOW_BT_ROOT /path/to/PicoW_Bluetooth_lib)
include(${PICOW_BT_ROOT}/cmake/picow_a2dp_source.cmake)
include(${PICOW_BT_ROOT}/cmake/picow_spp.cmake)
include(${PICOW_BT_ROOT}/cmake/picow_avrcp_target.cmake)
picow_target_a2dp_source(app ${CMAKE_CURRENT_LIST_DIR})
picow_target_spp(app ${CMAKE_CURRENT_LIST_DIR})
picow_target_avrcp_target(app ${CMAKE_CURRENT_LIST_DIR})
```

必要なプロファイルのヘルパーだけ使ってください。第2引数は互換性のある `btstack_config.h` の置き場所です。ヘルパーはソース、include、C++17、`CYW43_LWIP=0`、polling 用 Bluetooth ライブラリを追加します。[DeveloperSamples/CMakeLists.txt](../examples/DeveloperSamples/CMakeLists.txt) と[共有設定](../examples/DeveloperSamples/btstack_config.h)から始め、別々のサンプルの設定を無条件に混ぜないでください。SPP は `HCI_ACL_PAYLOAD_SIZE <= 1033` が必要です。任意の RFCOMM ERTM は別の資源・設定契約で、この一般的な手順だけでは有効になりません。AVRCP は `HAVE_MALLOC` を使わない場合、SDK の両 role 分として `MAX_NR_AVRCP_CONNECTIONS >= 4` が必要です。

追加helper（必要なものだけ使用）: Sinkは `picow_a2dp_sink.cmake` / `picow_target_a2dp_sink`、Controllerは `picow_avrcp_controller.cmake` / `picow_target_avrcp_controller`、Browsingは `picow_avrcp_browsing.cmake` / `picow_target_avrcp_browsing`、Cover Artは `picow_avrcp_cover_art.cmake` / `picow_target_avrcp_cover_art`。すべて `(app config_directory)` 形式です。Source-only設定をそのまま受信・Browsingへ流用せず、各機能ガイドのbuffer・stack・共有owner条件を満たしてください。リンク成功はSource/Sink同一peerや全profile併用の動作保証ではありません。

## A2DP：PCM をスピーカーへ送る

### `IAudioSource`

以下の仮想関数を実装します。player は音源を参照しますが、その所有権は取得しません。

| シグネチャ | 契約 |
| --- | --- |
| `bool hasSamples() const` | 現在 PCM を読めるかを返します。長時間待たないでください。 |
| `size_t read(int16_t* buffer, size_t samples)` | 最大 `samples` 個の符号付き16ビット値をコピーし、実際の個数を返します。バイト数でもステレオフレーム数でもありません。部分読み込み可能で、0 は現在データがないことを表します。 |
| `virtual ~IAudioSource()` | 仮想デストラクタ。音源は player より長く生存させます。 |

PCM は指定したレートのインターリーブ・ステレオ（`L,R,L,R,...`）です。リサンプルやモノラル変換はありません。部分読み込みでも左右の順序を保ち、渡されたバッファを保持したり容量を超えて書いたりしないでください。pipeline は部分 PCM を蓄積し、送信が詰まったときは未送信のエンコード済みバッチを保持します。

### 設定と初期化結果

`A2dpSourcePlayer(IAudioSource& source, Settings settings)` で構築します。コピーと代入は禁止です。

| `Settings` フィールド | 既定値 | 意味 |
| --- | --- | --- |
| `const char* target_address` | `""` | ゼロでない接続相手の `XX:XX:XX:XX:XX:XX`（17文字）。十分な生存期間を持つ文字列を渡します。ゼロアドレスは任意の相手を意味しません。 |
| `uint32_t sample_rate` | `48000` | `48000` または `44100` Hz。音源 PCM と一致させます。 |
| `uint64_t start_delay_us` | `0` | OPEN 後、自動 START 要求までの遅延。単位はマイクロ秒。相手からの START は遅延させません。 |
| `uint64_t play_duration_us` | `0` | START/再開した区間ごとの再生時間。単位はマイクロ秒。0 は継続、期限に達すると切断を要求します。 |
| `bool auto_connect` | `true` | 無線が Working になったら指定相手の探索接続を始めます。自動リトライではありません。 |
| `bool auto_start` | `true` | OPEN 後に自動で START を要求します。false なら `resume()` または相手の START を待ちます。 |
| `uint64_t control_timeout_us` | `10000000` | ローカル START/SUSPEND/RECONFIGURE の完了期限。0 は不可。期限超過はエラーとして切断を要求します。 |
| `bool allow_sample_rate_change` | `false` | 44.1/48 kHz の両方を許可します。`configure_source` 必須。リサンプラはありません。 |
| `bool (*configure_source)(uint32_t, void*)` | `nullptr` | 受理した初期設定・変更時にアプリの PCM レートを設定します。対応不可なら false。player を再入呼出ししないでください。 |
| `void* source_context` | `nullptr` | `configure_source` へ渡す呼出し側所有の情報。owner が callback を受ける間は有効に保ちます。 |

`BeginResult` は `Ok`（初期化・電源要求成功）、`InvalidSettings`（アドレス/レート不正、期限0、可変レート用 callback 未指定）、`RuntimeAlreadyOwned`（owner を取得できない）、`InitializationFailed`（runtime/profile 初期化または電源要求失敗）です。`Ok` でも接続・再生完了ではありません。初期化に失敗しても owner の取得済み状態が残ることがあるため、`begin()` の再試行を復旧手段にしないでください。

### メソッド一覧

| シグネチャ | 用途・戻り値 |
| --- | --- |
| `BeginResult begin()` | 一度だけプロファイルを初期化し、無線の電源を要求します。 |
| `void poll()` | Bluetooth、既存 ACL の検証、音声スケジューリングを進めます。 |
| `void connect()` | 指定アドレスを inquiry で探して接続します。戻り値も自動リトライもありません。state/history で確認します。 |
| `bool connectOverExistingAcl(const a2dp::AclBinding& expected)` | 共有 runtime の現在の ACL を明示的に再利用します。true は受付と SDK の即時接続開始成功で、接続・認証完了ではありません。 |
| `bool listen(bool enabled = true)` | 設定peerの着信受付を有効/無効化。無効化だけでは既存接続を切断しません。 |
| `bool pause()` | Openでは自動開始を取消し、PlayingではSUSPENDを要求。未送信音声は破棄します。 |
| `bool resume()` | Open/Suspended streamのSTARTを要求。 |
| `bool reconfigureSamplingFrequency(uint32_t sample_rate)` | Open/Suspended時、可変rate opt-inとsource callbackを条件に44.1/48 kHz変更を要求。自動再開しません。 |
| `void disconnect()` | プロファイルを切断するか、未完了の接続意図を取り消します。他プロファイルの無線電源は切りません。 |
| `a2dp::State snapshot() const` | 現在の状態のコピー。poll は実行しません。 |
| `Diagnostics diagnostics() const` | 送信・スケジューリング診断のコピー。 |
| `size_t historySize() const` | 保持中の履歴件数。既定容量は64件です。 |
| `a2dp::HistoryEntry history(size_t oldest_index) const` | 古い順の履歴。範囲外は既定値の entry を返します。 |
| `uint64_t overwrittenHistory() const` | 履歴の上書き件数。 |
| `uint64_t droppedEvents() const` | 有界イベントキューで失われたイベント数。 |

この player は非同期の pause/resume とsample-rate再設定を公開しています。曲選択・音量変更 API はありません。設定、遷移、callbackの責務、検証は[Source lifecycle制御](a2dp_source_lifecycle_jp.md)を参照してください。AVRCP ボタンを受けても自動では再生を変更しません。重大な状態エラーに対して `begin()` や `connect()` を繰り返すのでなく、状態を調べて報告してください。

`connectOverExistingAcl()` には `auto_connect=false`、begin 済み・Working・idle の owner、idle の inquiry/signaling/stream、未処理の接続意図なし、エラーなしが必要です。`AclBinding` は `peer`（6バイト）、`epoch`（0 でない runtime 世代）、`handle`（実際の HCI ハンドル）を持ちます。`BluetoothRuntime::linkForPeer()` の `active` を確認し、その正確な識別情報を渡してください。完了イベントと poll 時にも再検証します。認証を省略せず、進行中の inquiry も取り消しません。ACL の消失・置換や無線 OFF が発生すると、この明示的再利用経路はリセットまで再利用できません。SDK の過去パケットが CID/handle 再利用後に任意に再送された場合まで識別する仕組みではありません。

交渉する codec は、設定レート（可変レートを明示許可した場合は44.1/48 kHzのいずれか）の SBC stereo、16 blocks、8 subbands、loudness allocation、2〜53内の bitpool 範囲（最小≦最大）である必要があります。他 codec/mode は拒否します。playerは収まる場合に完全frameを1〜4個送信し、収まらない場合は対応形式の1 frameを2〜15断片で送ります。この上限を守れないpayloadはPCM読込前に拒否します。payloadはSBC header 1 byteを含みRTPを除きます。sample数と送信間隔は最後の断片が完了した時だけ進め、packet数は受理された物理packetごとに加算します。汎用engineの従来既定473 byteは維持し、このplayerは実codec長と断片数で判定します。[完全frameの旧checkpoint](source_mtu_batching_contract_jp.md)と[現在のSource断片送信](source_sbc_fragmentation_contract_jp.md)を参照してください。codec制約は不変です。

### 状態と診断

[a2dp::State と履歴の定義](../audio-core/connection/A2dpConnection.h)：

- `power`: `Off`, `Starting`, `Working`。`inquiry`: `Idle`, `Active`, `Stopping`。
- `signaling`: `Idle`, `Connecting`, `Connected`。`stream`: `Idle`, `Configuring`, `Open`, `StartRequested`, `Playing`, `SuspendRequested`, `Suspended`, `ReconfigureRequested`。`credit`: `None`, `Requested`, `Granted`。
- `cid`, `seid`, `payload_limit`, `codec`, `initialized`, `connect_intent`, `paired`, `codec_ready` は設定・接続状態です。`existing_acl`, `existing_acl_bound`, `existing_acl_stopped` は明示的 ACL 再利用の状態です。
- `accepting` は新規着信受付、`incoming` と `incoming_acl` は受け付けた接続を表します。`requested_rate` と `control_due_us` は処理中の制御、`delay_100us` は最新の遅延報告値（単位100マイクロ秒）、`delay_reports` はその件数です。
- `packets`は途中断片を含む受理された物理packet数、`completed_media`は完了した符号化batch数です。`samples`は完了時だけ進みます。`media_deferrals`は進展しなかった試行で、断片の送信成功は含みません。`max_lateness_us`、`pacing_rebases`、`pacing_discarded_us`は送信スケジューリングの診断で、`_us`はマイクロ秒です。
- `error`: `None`, `InvalidInput`, `UnsupportedOperation`, `Failed`, `UnsupportedCodec`, `QueueOverflow`, `ClockRegression`。`status` は SDK/内部結果、`rejected` は拒否イベント数です。
- `HistoryEntry`: `sequence`, `event`, `before`, `after`, `actions`, `count`。容量制限のある診断記録であり、永続ログではありません。

`Diagnostics`は`rtp_timestamp`、`rtp_sequence`、物理packet数`sent_packets`、`last_send_status`、`media_deferrals`、`pending_encoded_bytes`、完了batch数`completed_media`、`abandoned_partial_frames`を返します。最後の値は途中断片を1個以上送信したgroupの破棄を1回だけ数え、未送信Busy batchは含みません。Playing中の生涯最大値は`max_poll_gap_us`、`max_pump_us`、`max_credit_wait_us`です。packetごとのログよりcounterを使ってください。時刻・集計の保証は断片送信契約の対応transport前提に限定します。

### 最小 PCM 例

以下はスピーカーへ無音を送る完全な `main.cpp` です。相手アドレスと音源を置き換えてください。自動リトライや書き込み・ペアリング用コマンドは含みません。

```cpp
#include "A2dpSourcePlayer.h"
#include "pico/stdlib.h"
#include <algorithm>
class Silence : public IAudioSource {
public:
  bool hasSamples() const override { return true; }
  size_t read(int16_t* out, size_t count) override {
    std::fill_n(out, count, int16_t{0});
    return count;
  }
};
int main() {
  static Silence pcm;
  A2dpSourcePlayer::Settings settings;
  settings.target_address = "00:11:22:33:44:55"; // 自分のスピーカーへ変更。
  settings.auto_connect = true;
  static A2dpSourcePlayer player(pcm, settings);
  if (player.begin() != A2dpSourcePlayer::BeginResult::Ok)
    while (true) tight_loop_contents();
  while (true) player.poll();
}
```

A2DP は設定された相手の SSP confirmation を許可します。意図した、接続してよい機器を指定してください。[手動接続サンプル](../examples/DeveloperSamples/a2dp_source.cpp)と[トーン・診断付きサンプル](../examples/A2DPSourcePlayer/main.cpp)にはアプリ操作の例があります。

[Source + Sink 統合と資源要件](a2dp_dual_role_jp.md)

## A2DP Sink：Bluetooth Source から PCM を受信する

新しい facade は検証中です。[実行できる完全な例](../examples/A2DPSinkReceiver/README_jp.md)、[ヘッダ](../audio-core/A2dpSinkReceiver.h)、[SDK 対応の残件](a2dp_sdk_coverage_jp.md)を参照してください。`cmake/picow_a2dp_sink.cmake` を include し、`picow_target_a2dp_sink(app ${CMAKE_CURRENT_LIST_DIR})` を呼びます。この helper は 32 KiB のスタックを予約するため、アプリ全体の RAM 使用量を確認してください。

### 出力、設定、寿命

[IAudioSink](../audio-core/IAudioSink.h) を実装します。

- `size_t write(const int16_t *pcm, size_t frames, uint32_t sample_rate, uint8_t channels)`：借用された interleaved signed-16 PCM を、戻る前に消費またはコピーします。`frames` はチャンネル当たりのフレーム数で、バイト数や `int16_t` 要素数ではありません。受理した先頭部分の長さを 0〜`frames` で返します。未受理部分は破棄として計数し、再送しません。相手へのバックプレッシャーは提供しません。
- `void reset()`：設定、セッション、開始・停止の境界でアプリ側の PCM キューを空にします。`disconnect()` からも同期的に呼ばれます。`write()` は無線 callback ではなく `poll()` から呼ばれます。どちらも短時間で戻り、receiver API を再入呼び出ししないでください。

プログラム終了まで生存する output と settings で `A2dpSinkReceiver(output, settings)` を構築します。`Settings::peer` は許可する Source 1 台の `Address`（`std::array<uint8_t, 6>`）で、全ゼロは無効です。`allow_pairing_confirmation` の初期値は false。true は設定済み peer に対する runtime の pairing confirmation 方針を許可しますが、アプリに必要な explicit IO capability reply handler を実装するものではありません。

`BeginResult` は `Ok`、`InvalidSettings`、`RuntimeAlreadyOwned`、`InitializationFailed`、`FeatureUnavailable` です。`Settings::require_start_decision` の既定値は false で、任意 build 対応なしに true にすると `FeatureUnavailable` を返します。初期化の再試行は restart/recovery ではありません。初期化を試みた owner は破棄時も SDK が参照する記憶域を保持するため、繰り返し構築せず static 寿命で設計してください。

### receiver のメソッド

| メソッド | 用途と結果 |
| --- | --- |
| `BeginResult begin()` | profile を確保して初期化し、無線電源を要求。listen/connect はしない。`Ok` は無線・stream の準備完了を意味しない。 |
| `void poll()` | 共有無線、順序付き制御イベント、上限付きメディアデコードを進める。単一 main loop から頻繁に呼ぶ。 |
| `bool listen()` | 設定 peer の着信受付と共有無線の connectability を有効化。working で故障・切断待ち・未処理制御イベントがない owner が必要。discoverability は有効化しない。 |
| `bool connect()` | 設定 Source への接続を 1 回要求。true は SDK 受付で、接続完了ではない。重複要求、切断待ち、未完了の着信を含む既存 SDK 接続は拒否。 |
| `void disconnect()` | 着信受付を無効化し、接続試行中も含め、この A2DP signaling session の切断を要求。メディアキューを消去し consumer を reset。非同期切断完了まで poll を続ける。無線電源断・自動再接続はしない。 |
| `DelayReportResult reportDelay100us(uint32_t delay_100us, uint32_t expected_generation)` | 測定遅延を 100 µs 単位の 0〜65535 で報告。測定時の session generation を保持。`Accepted` は SDK のローカル受付で、配送・相手受付ではない。 |
| `static bool startDecisionsAvailable()` | 任意 START 判定の build 対応を確認。stream の準備完了ではない。 |
| `StartRequest pendingStart() const` | poll 後に現在の `generation/request_id` token をコピー。request ID が 0 なら判定可能な要求なし。 |
| `StartDecisionResult decideStart(StartRequest request, bool accept)` | callback 外で完全な token を渡して判定。`Accepted` と `SdkRejected` は token を消費し、SDK 拒否後は同じ token を再試行せず切断。`streaming` は別に確認。 |
| `State snapshot() const` | 現在の状態の値コピーを返す。 |
| `Diagnostics diagnostics() const` | 受信・解析・デコード・PCM カウンタの値コピーを返す。 |

`State` は `begun/working/accepting/connecting/closing/signaling/configured/streaming/failed`、セッションの `cid/seid/generation`、codec の `sample_rate/channels/sbc_channel_mode`、`last_event/last_status/last_operation_status` を持ちます。最後のイベント status と最後の初期化・発信要求 status は別物で、どちらもエラー履歴全体ではありません。codec 情報は `configured` のときに利用し、状態フラグから pairing/security を推測しないでください。

`Diagnostics` はパケット・バイト数、media/event の overflow・oversize、破棄・状態不一致・peer 拒否・不正イベント、欠落・順序逆転・timestamp/source 変更、SBC デコード数・失敗数、PCM 生成・配送・破棄数・不正な output 戻り値、キュー深さ・最大値を持ちます。`decoded_frames` は SBC フレーム数、`pcm_frames`、`delivered_frames`、`dropped_frames` はチャンネル当たりの PCM フレーム数です。現 parser の `parse_errors[0..7]` は `None, Short, Rtp, Fragmented, Count, Codec, Bounds, Trailing` に対応します。

遅延結果の enum は `DelayReportStatus::{Accepted, InvalidDelay, StaleSession, NotReady, NotNegotiated, SdkRejected}`、START 結果は `StartDecisionStatus::{Accepted, FeatureUnavailable, NotReady, StaleRequest, SdkRejected}` です。両 result 構造体の `sdk_status` は `Accepted/SdkRejected` の場合だけ意味を持ちます。どちらも自動再試行しません。[PCM 所有権と遅延報告](a2dp_sink_pcm_jp.md)、[任意 START 判定](sink_start_decisions_jp.md)を参照し、main loop の**すべての callback 外**から呼んでください。実験用 `detachSharedRole()` / `cancelSharedDiscovery()` は macro で分離され、通常の Sink + AVRCP 手順には含みません。

### 対応範囲と使用順序

1. 正確な Source アドレスを設定し、static な PCM consumer と receiver を保持します。
2. `begin()` を 1 回呼び、`snapshot().working` になるまで継続して `poll()` します。
3. 着信なら明示的に `listen()`、発信 1 回なら `connect()` を呼びます。`configured` と `streaming` を確認し、PCM は `write()` で受け取ります。
4. 必要時に `disconnect()` を呼び、`closing` 中も poll し、次の操作を明示的に選びます。

現在の PCM 経路は[記載した128種類のSBC形式](sink_sbc_configurations_contract_jp.md)を実装しています。44.1/48 kHz、mono/dual/stereo/joint stereo、4/8/12/16 blocks、4/8 subbands、loudness/SNR allocationで、広告bitpoolは2〜86、形式別の交渉上限を適用します。完全packetと[有界SBC断片列](a2dp_sink_fragments_jp.md)を受け付けます。形式・断片の全組み合わせの証拠はオフライン検証で、包括的な無線受入や独立したcodec適合ではありません。下記の限定SBC実機結果とは区別します。他codecと任意16/32 kHzは未対応です。このreceiverにDAC、I2S出力、resampler、録音、AVRCP Controllerは付属せず、AVRCPは別facadeです。別peerのSource/Sink所有とraw AVDTP role gateには限定したhost証跡がありますが、同peerの登録は引き続き拒否します。広い併用・SDK全機能対応は別の残件で、限定した SBC + AVRCP 到達点の新たな必須条件ではありません。

## A2DP Sink と AVRCP：方向と同時利用

A2DP の方向と AVRCP role は独立です。Windows/スマートフォンの player から Pico へ音声を送る場合、相手が **A2DP Source**、Pico が **A2DP Sink** で、Pico の **AVRCP Controller** が相手の **AVRCP Target** へ再生操作を送ります。ローカル Target を追加すれば、相手 Controller から逆方向の要求を受けられます。発信 `connect()` で role は変わりません。Sink + AVRCP は、同 peer の A2DP Source + Sink 双方向メディア動作とは別です。

互換性のある一つの `btstack_config.h` で `picow_target_a2dp_sink` と `picow_target_avrcp_controller` を使います。Controller helper は Target 対応も含みます。各 role の owner は一つだけ構築し、許可 peer を明示し、それぞれ一度 begin して、同じ main loop で全 active facade を poll します。Sink の準備・stream 状態と AVRCP の `Connected`・有効 binding は別々に確認します。他 profile の自動接続、ボタンと再生の自動対応付け、PCM gain の適用はしません。共有 AVRCP の cancellation/disconnect は両 AVRCP role に影響し、音声 stream 停止の代用ではありません。[公開 media-control 併用サンプル](../examples/AvrcpMediaControlSample/README_jp.md)を参照してください。

host test／build の成功は公開サンプルの実機動作を保証しません。異なる試験アプリでの結果を転用せず、[検証範囲](validation_jp.md) の範囲を確認してください。

## SPP：シリアルのバイト列を送受信する

### 型と上限

[core の型定義](../spp-core/SppCore.h)：

| 型 | 意味 |
| --- | --- |
| `Address = std::array<uint8_t,6>` | 表示順のアドレス。`00:11:22:33:44:55` は `{0x00,0x11,0x22,0x33,0x44,0x55}`。 |
| `Uuid` | 16バイト UUID。`Uuid::from16(0x1101)` は Bluetooth Serial Port UUID。128ビット UUID は `bytes` に設定します。 |
| `Config` | `peer` は許可する一台の相手。`init_timeout_us`, `sdp_timeout_us` は各10,000,000マイクロ秒が既定値です。 |
| `Client::Security` | `allow_confirmation=false`, `authentication=true`。必要な場合に SSP confirmation を明示許可します。ペアリング問題を隠すために認証を無効にしないでください。 |
| `Handle` | `{generation, slot}`。generation は0以外で現在の世代であること。`handleOf(snapshot, slot)` で取得し、古い handle を再利用しません。 |
| `IoResult` | `{size_t bytes, Error error}`。コピーしたバイト数であり、相手の受信確認ではありません。 |
| `Candidate` | SDP の `{record, channel}`。最大8候補。 |

route は2本（slot 0/1）、待受 endpoint は2個、許可 peer は1台、TX/RX は**各 route あたりそれぞれ1024バイト**、RFCOMM frame は最大1024バイト、SDP query は共有で一つです。channel は1〜30。同じ peer/channel を2 route で重複使用できません。バイトストリームなので、メッセージの区切り、ACK、重複排除、再送はアプリ側のプロトコルで扱います。

### `spp::Client` の全メソッド

`Client(Config)` または `Client(Config, Security)` で構築します。コピーは禁止です。接続・シリアル制御要求はキューへ入ります。`configureErtm` は起動前のローカル保存領域の検査・設定、`ertmCompiled` はコンパイル時の機能判定です。キュー受付がtrueでも後から拒否・失敗する場合があり、poll 後の `snapshot()` と `history()` を確認する必要があります。

| シグネチャ | 用途・完了確認 |
| --- | --- |
| `bool begin()` | 起動を要求。`runtime == Runtime::Ready` を待ちます。 |
| `void poll()` | runtime、タイムアウト、イベントキューを処理します。 |
| `bool connect(uint8_t slot, Address peer, uint8_t channel)` | Closed の route を既知 channel へ接続。peer は `Config.peer` と一致させ、`Phase::Open` を待ちます。 |
| `bool connect(uint8_t slot, Address peer, Uuid uuid)` | UUID を解決して接続。1候補なら自動選択、複数なら `select()` が必要です。 |
| `bool discover(Address peer, Uuid uuid)` | route を開かずに SDP 検索。`query.phase`, `query.candidates[0..count)`, `query.error/status` を確認します。 |
| `bool cancelDiscovery(uint32_t token)` | 現在の snapshot の query token を指定して取消。SDK 完了を待つため、即座に SDP を再利用できるとは限りません。 |
| `bool select(Handle h, uint8_t index)` | `Selecting` の route の候補を0始まりの index で選択。古い handle は即座に false、それ以外は要求をキューへ入れます。 |
| `bool disconnect(Handle h)` | 切断・接続取消を要求。未送信 TX と未読 RX を破棄します。Closed を待ち、SDK 切断失敗なら Closing に残る場合があります。 |
| `bool listen(uint8_t slot, uint8_t channel, Uuid uuid)` | ローカル待受 endpoint を登録。送信側接続ではありません。`endpoints[slot].phase` を確認します。着信にも指定 peer と空き route が必要です。 |
| `bool unlisten(uint8_t slot)` | 待受サービス・SDP登録を取り下げる要求。既存routeは接続を維持。 |
| `bool control(Handle h, ControlRequest request)` | 現在のOpen handleへ検査済み制御を要求。結果はローカルSDK状態で、相手ACKではない。 |
| `bool sendLineStatus(Handle h, uint8_t value)` | line statusを要求。有効なwire値は制御ガイド参照。 |
| `bool sendModemStatus(Handle h, uint8_t value)` | modem statusを要求。これ自体はバイト通信停止ではない。 |
| `bool configurePort(Handle h, PortSettings port)` | 仮想port設定を要求。物理UARTの設定ではない。 |
| `bool queryPort(Handle h)` | 相手port設定の問い合わせを要求。 |
| `bool selectPeer(Address peer)` | route・SDP・lease解放後の静止状態で相手選択を要求。snapshotで反映を確認。 |
| `static constexpr bool ertmCompiled()` | ERTM用defineとoverlayの有効性を判定。交渉結果ではない。 |
| `bool configureErtm(const ErtmPool::Config& config)` | Stoppedかつ未処理要求なしの起動前に、呼出側所有bufferを設定。 |
| `ErtmPool::Status ertmStatus() const` | pool/lease状態をコピー。lease確保はERTM交渉成功を意味しない。 |
| `Transport transport(Handle h) const` | 現在のOpen transportを観測。古い・Closed・観測不可handleはUnknown。 |
| `IoResult tryWrite(Handle h, const uint8_t* p, size_t n)` | 現在空いている TX へ可能な分だけコピー。未受付の残りだけ保持して再試行します。ポインタは保持しません。 |
| `IoResult read(Handle h, uint8_t* p, size_t n)` | RX から最大 n バイトをコピーして取り出し、取り出した数を返します。ポインタは保持しません。 |
| `State snapshot() const` | 現在の状態のコピー。poll は行いません。 |
| `size_t historySize() const` | 保持履歴の数。既定16件。 |
| `HistoryEntry history(size_t i) const` | 古い順。範囲外は既定値 entry。 |
| `uint64_t droppedEvents() const` | キューで失われたイベント数。バイト数ではありません。 |

facade には `stopListening()`、`stop()`、`overwrittenHistory()` はありません。core engine のメソッドをそのまま Client から呼べるわけではありません。`listen()` は service 登録であり、無線をグローバルに discoverable/connectable にする操作は含みません。着信を受ける場合は開発サンプルのように、意図したアプリの接続許可方針を別途設定します。

### 状態、結果、送信詰まり

- `Runtime`: `Stopped`, `Starting`, `Ready`, `Failed`。runtime の Failed はリセットまで継続します。
- route `Phase`: `Closed`, `Resolving`, `Selecting`, `Connecting`, `Open`, `Closing`。
- `EndpointPhase`: `Disabled`, `Registering`, `Listening`, `Failed`。
- `QueryPhase`: `Idle`, `Querying`, `Complete`, `Failed`, `TimedOut`, `Cancelled`。次の query 前には `busy` も確認します。
- route には `generation`, `peer`, `channel`, `cid`, `mtu`, `error`, `status`, `candidate_count`, `candidates`, `query_token`, `tx_size`, `rx_size`, `txFree()` があります。
- `tx_accepted` はコピー受付数、`tx_sent` は SDK の送信受付数で相手アプリの ACK ではありません。`tx_discarded`, `rx_received`, `rx_read`, `rx_discarded` は破棄・受信・消費数です。`writable_epoch` は TX 空きなしから空きありへ変わると更新されます。
- `HistoryEntry`: `sequence`, `event`, `before/after` の view、`actions`, `count`, `result`, `writable_mask`。拒否された要求すべてが `State.error/status` に現れるわけではなく、route/query と履歴結果も確認します。

`Error` の意味と対応：

| 値 | 意味 |
| --- | --- |
| `None` | その操作にエラーなし。ただし部分 I/O でも None のため必ず bytes を確認。 |
| `WouldBlock` | 非ゼロ write で TX 空きなし、または非ゼロ read で RX 空。poll して後で再試行。 |
| `Busy` | イベント処理待ち・処理中の I/O、または必要な資源を別操作が使用中。poll せずに再試行し続けない。 |
| `InvalidInput`, `InvalidHandle`, `NotReady`, `NotConnected`, `DuplicateChannel` | 引数、世代、状態を直してから再試行。 |
| `Failed`, `Timeout`, `NotFound`, `CandidateOverflow`, `Cancelled` | route/query、SDK status、履歴を確認。勝手に相手/channel を変更したり owner を再起動したりしない。 |
| `StaleEvent` | 現在の接続/query と一致しないイベント。 |
| `QueueOverflow`, `ClockRegression`, `RxOverflow`, `TokenExhausted` | 資源上限・不変条件の違反。route/runtime が Failed になったかを確認。 |

長さ0の read/write は handle と接続が有効なら0バイト成功になり得ます。null ポインタが不正になるのは長さが非ゼロのときです。Open を確認していても、コールバックがイベントを追加すれば I/O が Busy を返す場合があります。

### 一度だけ接続し、部分送信と受信を処理する

以下は、指定 peer の `Config` から生存期間の長い `client` を作り、`client.begin()` が true を返した後のループ断片です。実際に相手が channel 1 の service を提供していることが前提です。実機の channel を推測で決めないでください。送信中は取得した handle を固定し、新世代へ未送信の残りを持ち越すにはアプリの明示的な再送方針が必要です。

```cpp
bool requested = false, bound = false, finished = false;
spp::Handle transfer{};
const uint8_t message[] = {'p', 'i', 'n', 'g', '\n'};
size_t offset = 0;
while (true) {
  client.poll();
  const auto state = client.snapshot();
  if (!requested && state.runtime == spp::Runtime::Ready) {
    requested = true; // キュー拒否でも、自動では再試行しない。
    if (!client.connect(0, state.config.peer, uint8_t{1})) finished = true;
    continue;
  }
  const auto& route = state.routes[0];
  if (!bound && requested && route.phase == spp::Phase::Open) {
    transfer = spp::handleOf(state, 0);
    bound = true;
  }
  if (!bound || finished) continue;
  if (!spp::matches(state, transfer) || route.phase != spp::Phase::Open) {
    finished = true; // 別接続へ再送せず、失敗として報告する。
    continue;
  }
  if (offset < sizeof(message)) {
    const auto result = client.tryWrite(transfer, message + offset,
                                        sizeof(message) - offset);
    offset += result.bytes;
    if (result.error != spp::Error::None && result.error != spp::Error::Busy &&
        result.error != spp::Error::WouldBlock) finished = true;
  }
  uint8_t received[64];
  const auto result = client.read(transfer, received, sizeof(received));
  // received[0..result.bytes) だけ処理する。C文字列ではない。
  // result.error も確認する。Busy/WouldBlock なら poll 後に再試行。
}
```

完全な状態・エラー処理、2 route、待受、明示的再接続は [SppSample.h](../examples/DeveloperSamples/SppSample.h)、[PendingMessage.h](../examples/DeveloperSamples/PendingMessage.h)、[spp_two_routes.cpp](../examples/DeveloperSamples/spp_two_routes.cpp)、[spp_reconnect.cpp](../examples/DeveloperSamples/spp_reconnect.cpp) を参照してください。

## AVRCP Controller：操作を送り応答を観測する

[AvrcpController.h](../avrcp-sdk/AvrcpController.h) を include します。以下の Controller 固有型は `picow::avrcp::AvrcpController` の入れ子型で、`Address` と `Binding` は `picow::avrcp` に属します。`Config` は `AvrcpTarget::Config` の別名で、相異なる非ゼロの許可 peer 1〜2 台を指定して一度だけ begin し、stop は終端操作です。`Binding` は peer、ACL epoch、CID、owner 固有 generation を含みます。この Controller の `Connected` イベントまたは `bindingForPeer()` から取得し、Target の binding や CID だけで代用しないでください。disconnect/reset 後の古いキュー要求は破棄します。

| シグネチャ | 契約 |
| --- | --- |
| `bool begin(const Config&, bool advertise_browsing = false)` | ローカル初期化結果で、相手接続ではない。Browsing 広告だけで Browsing owner は作られない。 |
| `void poll()` / `void poll(uint32_t now_ms)` | 共有 runtime を処理。時刻付き overload だけが任意 command deadline を進める。 |
| `bool setCommandTimeout(uint32_t milliseconds)` | 0 は無効、最大 `0x7fffffff`。単調時刻の timed poll を使用。期限切れは共有 signaling 終了要求で、即時解放の保証ではない。 |
| `void stop()` | owner の終端停止。通常は Target が使う signaling を保持するが、held button の release 失敗時は共有切断が起こり得る。 |
| `int connect(const Address&)` / `int disconnect(const Binding&)` | ローカル要求状態。共有 signaling への影響は Target と同じ。 |
| `int cancelConnection(const Address&)` | 未完の共有接続試行を取消。後処理まで poll を継続。 |
| `bool cancellationPending(const Address&) const` / `int cancellationStatus(const Address&) const` | cancellation の観測で、新しい接続の成功判定ではない。 |
| `Binding bindingForPeer(const Address&) const` | 現 binding、不在ならゼロの既定 binding。 |
| `Phase phase(const Binding&) const` | `Idle`、`PressPending`、`Held`、`ReleasePending`。phase だけで有効 binding と判断しない。 |
| `int press(const Binding&, uint8_t operation)` / `int release(const Binding&)` | operand なし標準ボタンを送り、SDK hold/反復を終了。明示 release が必要で、押下応答前にも要求可能。 |
| `int query(const Binding&, Query)` | capability、play status、now-playing 情報、settings の問い合わせ。戻り値でなく後続応答を解釈する。 |
| `int group(const Binding&, bool next)` | vendor-unique の次/前 **group** click。曲の NEXT/PREVIOUS とは別。 |
| `int notification(const Binding&, avrcp_notification_event_id_t, bool enable)` | 登録または更新停止。disable は即時の wire unsubscribe ではなく、INTERIM 待ちではローカル拒否され得る。 |
| `int absoluteVolume(const Binding&, uint8_t volume)` | 7-bit の 0〜127 要求。UI の 0〜100% やローカル gain ではなく、`press(0x41/0x42)` 音量ボタンとも別。 |
| `const Status& status() const` | `started/stopped`、`malformed/ignored/connection_errors/responses`、`last_error`。アプリの再生操作結果ではない。 |

`int` を返す command メソッドは 0 がローカル SDK 受付、非 0 がローカル失敗で、`bool` の成功値として扱わないでください。`setCallback(void (*)(const Event&, void*), void* = nullptr)` は `Connected`、`Disconnected`、`PressResponse`、`ReleaseResponse`、`Reset`、`Timeout`、`CustomError` を受け取ります。ボタン応答は AV/C response 9 Accepted、10 Rejected、8 Not Implemented を示しますが、Accepted でも player の曲や UI 音量が変わった証明ではありません。早期 release では独立した押下応答なしで完了する場合があります。peer ごとに pending button 操作は一つとし、結果を確認し、generation 変更後に無条件で再送しないでください。

`setSdkCallback` は `SdkEvent::{binding, bytes, size}` を渡します。`setSettingsCallback` と `setCustomCallback` は `binding, response, pdu, total, offset, bytes, length` と `final()` を持つ `SettingsChunk` / `CustomChunk` を渡します。これらのバイト列と callback 参照は**同期 callback の間だけ借用可能**です。後で使う場合は pointer を含む構造体だけでなく、上限付きでバイト列自体をコピーします。サイズと chunk 全体の組立てを確認してから入れ子の field を解釈し、payload を C 文字列と決めつけないでください。callback は短時間の観測にとどめ、command は main loop のすべての callback 外で発行します。

ほかに `int` adapter として `elementAttributes`、`settingValues`、`batteryStatus`、`addressedPlayer`、`maximumFragments`、`playItem`、`addItem`、`displayableCharsets`、`settingAttributeText`、`settingValueText`、`currentSettings`、`setSettings`、`shuffleMode`、`repeatMode`、`customCommand` があります。引数の型・上限、overlay 要件、notification、cancellation、応答組立ては [Controller 契約](avrcp_controller_jp.md)と [SDK overlay](avrcp_sdk_overlay_jp.md)を参照してください。記載は各機能の無線検証済みという意味ではありません。

## AVRCP：相手のボタン操作を受ける

`picow::avrcp::AvrcpTarget` は Target role を扱い、明示的な発信 `connect(peer)` と `disconnect(binding)` も追加されています。[Target/Controller共有lifecycle](avrcp_controller_jp.md)を参照してください。両roleはsignaling/SDP所有権を共有し、自動接続、ペアリング許可、discoverable化、player操作へのボタン割当は行いません。

### 設定とメソッド

`Config` は `std::array<Address,2> peers` と `uint8_t peer_count=0` を持ちます。重複しない、全バイト0でないアドレスを1〜2台指定します。未使用要素は0のままにします。`Address` は6バイト配列です。

| シグネチャ | 契約 |
| --- | --- |
| `AvrcpTarget()` / `~AvrcpTarget()` | コピー不可の owner。プログラム終了まで保持。デストラクタの stop は再起動手段ではありません。 |
| `bool begin(const Config&, bool advertise_browsing = false)` | peer 検証、profile claim、SDP 登録、電源要求。true は起動受付で、接続完了ではありません。owner 取得・初期化を試みた後は同じ owner で再試行しません。 |
| `void poll()` | runtime を処理し、終端的な runtime 障害ではこの profile を停止。 |
| `void stop()` | この owner にとって終端操作。この role の状態を解除。他方 role が使う signaling は保持。A2DP/SPP の電源は切りません。 |
| `int connect(const Address&)` / `int disconnect(const Binding&)` | 0 はローカル SDK 受付。明示的 disconnect は両 role の共有 AVRCP signaling を閉じるが、A2DP stream や無線の電源断ではない。 |
| `int cancelConnection(const Address&)` | 接続済み link ではなく、未完の共有 signaling 接続試行を取り消す。poll を続け、受付と資源解放を区別する。 |
| `bool cancellationPending(const Address&) const` / `int cancellationStatus(const Address&) const` | cancellation の後処理とローカル状態を観測。相手接続の成功証明ではない。 |
| `void setCallback(void (*)(const Event&, void*), void* context)` | callback と借用 context を設定。null callback で無効化。 |
| `bool setSupported(uint8_t operation, bool enabled)` | 対応 operation を **begin 前に**変更。不正 ID や begin 試行後は false。 |
| `bool setPlayback(uint32_t length_ms, uint32_t position_ms, uint8_t playback_status)` | play-status query の回答値を設定。長さ・位置はミリ秒、UINT32_MAX は不明。status は0停止、1再生、2一時停止、3早送り、4巻き戻し、0xffエラー。それ以外は false で更新しません。 |
| `const AvrcpTargetCore& core() const` | 読み取り専用のボタン状態、binding、カウンタ。 |
| `const Status& status() const` | adapter 状態：`started`, `stopped`, `malformed`, `ignored`, `connection_errors`, `response_errors`, `last_error`。 |

既定で対応する operation ID は `0x41` 音量上、`0x42` 音量下、`0x44` 再生、`0x45` 停止、`0x46` 一時停止、`0x48` 巻き戻し、`0x49` 早送り、`0x4b` 次、`0x4c` 前です。pass-through のボタン識別子であり、音量設定や音楽プレイヤー自体の実装ではありません。

### Metadata、notification、着信要求

`int setMetadata(const Binding&, const Metadata&)` は title/artist/album/genre（各最大 255 バイト）を owner の記憶域へコピーします。応答・continuation が記憶域を参照中は command-disallowed を返す場合があります。`int setCompanies(...)` と `int setSubunitInfo(...)` も capability buffer をコピーします。正確な上限、`setUnitInfo`、notification API、player setting 対応は[ヘッダ](../avrcp-sdk/AvrcpTarget.h)と [Target 機能契約](avrcp_target_features_jp.md)を参照してください。`bool configurePlayerSetting(uint8_t attribute, uint8_t initial_value)` は begin 前に初期対応を設定し、`bool updatePlayerSetting(uint8_t attribute, uint8_t value)` は有効な設定値を更新します。

`int supportNotification(const Binding&, uint8_t event_id)` と `notifyPlayback/notifyVolume/notifyTrack/...` は対応イベントと通知状態を管理します。`notifyVolume` は 0〜127 で、PCM gain を適用しません。ローカル結果 0 は相手 UI が変化した証明ではありません。`uint32_t positionNotificationInterval(const Binding&) const` の単位は秒で、position 値のミリ秒と異なります。

`setRequestHandler(Response (*)(const Request&, void*), void* context)` は absolute volume、item 操作、addressed player、player settings の要求へ上限付き同期応答を返します。`Response` は AVRCP `status` と実際の `volume` を持ち、既定の volume 応答は要求値を保つだけで audio gain を実装しません。`setPassThroughHandler(PassThroughDecision (*)(const PassThroughRequest&, void*), void* context)` は overlay 専用で、`NotImplemented=8`、`Accepted=9`、`Rejected=10` を返します。これらの判定 handler は観測だけの callback の例外です。即時の protocol 判定を返し、アプリへの要求をコピーして、再生・hardware 操作は main loop へ延期してください。API 再入、人の応答待ち、要求参照の保持はしません。操作の対応表明・受付だけでアプリ上の効果を自動実行するわけではありません。

### イベントとコールバックの注意

`Event` は `kind`, `binding`, `operation`, `pressed`, `result` を持ちます。`Kind` は `Connected`, `Disconnected`, `Operation`, `Reset`, `Rejected`。`Binding` は `peer`、0でない ACL `epoch`、AVRCP `cid`、単調増加する `generation` です。CID だけで接続を識別しないでください。

callback 内では状態参照はできますが、core/target を再入的に変更しないでください。イベントのコピーやアプリへの要求だけ記録し、`poll()` が戻ってから操作します。`pressed=true/false` は押下・解放で、2回のクリックではありません。`Disconnected` と `Reset` では押しっぱなし動作を解除します。core は仮の release イベントを生成しません。重複した押下・解放は拒否されます。

```cpp
struct Buttons {
  bool stop_requested = false;
  static void receive(const picow::avrcp::Event& e, void* context) {
    auto& self = *static_cast<Buttons*>(context);
    if (e.kind == picow::avrcp::Kind::Operation &&
        e.operation == 0x45 && e.pressed) self.stop_requested = true;
  }
};
// ループ開始前の準備。owner と context は長く生存させる。
static Buttons buttons;
static picow::avrcp::AvrcpTarget target;
// target.setCallback(Buttons::receive, &buttons);
// target.setPlayback(UINT32_MAX, UINT32_MAX, 1);
// if (!target.begin(config)) { /* 初期化失敗を報告 */ }
// begin 済み A2dpSourcePlayer の player があるメインループ：
// target.poll();
// player.poll();
// if (buttons.stop_requested) {
//   buttons.stop_requested = false;
//   player.disconnect(); // 自動割当ではなく、アプリが明示的に決める動作。
// }
```

完全な組み合わせは [a2dp_avrcp.cpp](../examples/DeveloperSamples/a2dp_avrcp.cpp) と[3プロファイル例](../examples/DeveloperSamples/a2dp_spp_avrcp.cpp)にあります。

### 読み取り専用 core と単体テスト用 core

[AvrcpTargetCore](../avrcp-core/AvrcpTargetCore.h) は実機なしの決定的テストにも使えます。`target.core()` からは `status()`、`supported(operation)`、`bindingForCid(cid)`、`bindingForPeer(peer)`、`isPressed(binding, operation)`、`validateOperation(binding, operation, pressed)` を利用できます。見つからない binding は0の既定値です。core `Status` は `connections`, `disconnections`, `operations`, `rejected`, `resets`, `active`, `last_result` を持ちます。

別途単体で構築した core では、`setCallback(callback, context=nullptr)`、`setSupported(operation, enabled)`、`connect(requested, accepted=nullptr)`、`disconnect(binding)`、`operation(binding, operation, pressed)`、`reset()` も利用できます。これらは Bluetooth I/O を行いません。`connect()` が accepted の generation を割り当てます。`setSupported()` は接続 peer なし、operation < 128 が条件です。`reset()` は接続を消しますがカウンタ、対応 operation、単調増加 generation は保持します。`Result` は `Ok`, `InvalidInput`, `NotConnected`, `StaleBinding`, `Capacity`, `Duplicate`, `Unsupported`, `InvalidSequence`, `Exhausted`。別 core から SDK adapter の状態へ注入しないでください。

## 共有 runtime（上級者向け）

通常は facade が runtime の初期化を管理します。以下は観測や新しいアダプタのための API であり、普通のアプリに追加で必要な初期化ではありません。

| メソッド | 意味・戻り値 |
| --- | --- |
| `static BluetoothRuntime& instance()` | プロセス内 singleton。 |
| `bool claim(Profile)` | `A2dp`, `Spp`, `Avrcp`, `A2dpSink` の生涯一度の取得。release なし。取得済み、Failed、不正 profile、外部初期化済み CYW43 なら false。 |
| `int initialize()` | 一度初期化。0成功、非0失敗。自分が初期化済みなら0、外部 CYW43 所有は失敗。 |
| `int powerOn()` / `int powerOff()` | 共有無線への要求。0成功、非0エラー。完了は非同期。OFF は全 profile に影響し、個別切断用ではありません。 |
| `void poll()` | CYW43 と保留中 confirmation 判断を処理。 |
| `bool ownsRadio() const` / `bool failed() const` | 初期化済み所有状態／終端的な runtime 失敗。接続・認証完了の判定ではありません。 |
| `void setHandler(Profile, btstack_packet_handler_t)` | profile handler を一つ設定。既存 facade の handler を上書きしないでください。 |
| `void setPairingPolicy(Profile, Address, bool)` | profile の指定 peer の SSP confirmation 許可/不許可。 |
| `void confirmPairing(const Address&)` | 保留中の confirmation にだけ応答。取得済み profile の一致する許可方針がある場合のみ承認。 |
| `void setSecurityLevel(Profile, uint8_t)` | 0〜4に制限。全体の level は取得済み profile の最大値。 |
| `Link linkForPeer(const Address&) const` / `Link linkForHandle(uint16_t) const` | 現在の ACL（`peer`, `epoch`, `handle`, `active`）または inactive の既定値。必ず active を確認。 |
| `int startSdp(uint32_t token, const Address&, const std::array<uint8_t,16>& uuid, btstack_packet_handler_t)` | 共有 SDP を取得。0 は SDK 開始成功、非0は失敗/busy。非0 token と非null callback を指定。 |
| `void cancelSdp(uint32_t token)` | 一致する query を取消扱いにする。SDK 完了まで所有は続き、SDK 内部 SDP 状態はリセットしません。 |
| `bool sdpOwned() const` | 共有 SDP token を現在保持しているか。 |

## 旧 API と内部インターフェース

PicoWBluetooth.h は**旧構成の集約ヘッダ**で、現在の facade の入口ではありません。`AudioManager`、connection plugin、controller、HFP 関連クラスは旧 plugin 構成です。クラスの存在だけで HFP マイク転送、FastStream、A2DP sink のアプリ API が動作することを意味しません。現在の profile owner と同時に初期化しないでください。

`AudioPipeline`、`BTStackA2DPTransport`、SBC encoder adapter、`a2dp::Rules/Engine`、`spp::Rules/Engine`、SDP/parser helper は低レベル実装・テスト用の面です。本書はアプリ用 facade とその戻り値の型を対象とし、内部ヘッダの public 宣言すべての安定性を保証するものではありません。特に、`A2dpSourcePlayer` 使用中に singleton の `AudioPipeline` を別途設定しないでください。

## 困ったときの確認順

1. 起動結果を確認したら poll を続け、実際の profile 状態を調べます。キュー受付や無線 Working だけでは接続完了ではありません。
2. SPP は世代付き handle を保持し、`IoResult.bytes` と `error` の両方を確認。未受付の残りを保持します。TX 満杯や RX 空は必ずしも切断ではありません。
3. 音切れは、バッファを増やす前に音源供給、`media_deferrals`、poll 間隔最大値、credit 待ち最大値を比較します。パケットごとのログは避けます。
4. ボタンは adapter の `status()` と core のカウンタを確認し、許可 peer を正確に指定。押下/解放と disconnect/reset を処理します。
5. 自分の機器・相手で実機確認してください。[サンプル検査](../examples/DeveloperSamples/check.sh)は host test とコンパイル用であり、書き込み、ペアリング、無線上の動作確認はしません。

実験用の同peer role join/detach APIは明示target macroに分離されており、通常の保証済みinterfaceではありません。build設定・lifecycle・未完の制約は[同peer実験](a2dp_same_peer_experiment_jp.md)を参照してください。
