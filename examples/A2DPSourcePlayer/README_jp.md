# A2DP Source を短く使う

日本語 | [English](README.md)

この見本は `IAudioSource → SBCEncoder_BTStack → BTStackA2DPTransport` を
`AudioPipeline` で結び、Bluetooth 接続の手順をライブラリに任せる。
アプリに BTstack のイベント処理、SDP、SEID、パケット組み立ては不要。
初めて組み込む場合は [開発者サンプル一覧](../../docs/developer_samples_jp.md) と
[DeveloperSamples手順](../DeveloperSamples/README_jp.md) も参照する。各例のビルド結果と実機検証範囲は別に扱う。

```cpp
FixedTone source;
source.configure(48000);
A2dpSourcePlayer::Settings settings;
settings.target_address = "00:11:22:33:44:55";
settings.sample_rate = 48000;
A2dpSourcePlayer player(source, settings);
if (player.begin() != A2dpSourcePlayer::BeginResult::Ok) { /* エラー表示 */ }
while (true) player.poll();
```

## 差し替える場所

- 音源: `FixedTone` を自分の `IAudioSource` に替える。`hasSamples()` と `read()` を実装。
- PCM: 符号付き16bit、左右交互のstereo。`samples` は左右を合わせたint16値の個数。
  `read()` は要求された1024値（512 stereo frames）を満たせるようにする。
  現行pipelineは部分供給を保持し、必要量が揃うまで待つ。無音補充や再サンプリングは行わない。
- 周波数: 音源と `settings.sample_rate` を同じ48000または44100にする。
  選択した周波数だけを相手に広告し、勝手な周波数変更をしない。
- 相手: `target_address` を実機のBluetooth addressに替える。指定した相手だけをinquiryで探す。
- 通常は `play_duration_us=0` なので連続再生。見本のmain.cppは実機比較用として
  USBの `c` で明示開始し、OPEN後4秒待ち、3秒再生で切断する。ライブラリの普遍的なタイムアウトではない。
- `auto_connect=false` なら電源working後に `connect()` で発見を開始する。
  `disconnect()` は明示停止。自動リトライはしない。
  すでに同じpeerの有効ACLがある場合は、明示opt-inの
  [connectOverExistingAcl](../../docs/a2dp_source_lifecycle_jp.md) を使える。
  通常の `connect()` は探索型のままで、要求受理は接続・認証・Playing成功を意味しない。

## 一つの所有者、一つのループ

`begin()` は共有 `BluetoothRuntime` にA2DP所有者を登録する。CYW43 / HCI / L2CAP / SDP / 電源は
runtimeが共通管理し、SPPと新AVRCP targetも同じruntimeを使う。
アプリ側の独立 `cyw43_arch_init()`、BTstack初期化、旧connection pluginを併用しない。
二つ目のA2DP playerや競合するowner claimは `RuntimeAlreadyOwned` で拒否する。
playerと音源はプログラム終了まで生存させる。破棄・再初期化は未対応。
すべて同じメインループで呼び、併用する全profileを毎周pollする。IRQ、別コア、バックグラウンドpollを併用しない。

SPP・AVRCP Target と共有 runtime を使えます。HFP は[非対応](../../docs/HFP_UNSUPPORTED_REASON.md)、Wi-Fi 併用は未検証です。

## 状態と履歴

`snapshot()` は純粋なコピーで、power/inquiry/signaling/stream/credit、cid/seid、
codec、送信packet/sample累計、error/status、拒否数を確認できる。
`snapshot().error/status`（接続core）と `diagnostics().last_send_status`（直近SDK送信結果）は別の指標。
現在のbasic transportでは `0x57 = BTSTACK_ACL_BUFFERS_FULL` を `Deferred` に分類し、
pipelineは未送信encoded batchを保持する。非ゼロだけでcoreの終端失敗と同一視しない一方、
他の非ゼロstatusを一律に許容しない。pending解消・packet増加・core errorとbindingを併せて観察する。

`historySize()` と `history(i)` は古い順の有界履歴を返す。
各履歴にはイベント、前後状態、生成アクション、sequenceがある。
非同期完了・失敗は次のイベントとして記録する。APIの返値が0でも相手の受諾とは区別する。
`overwrittenHistory()` と `droppedEvents()` で観測の欠落を確認する。
重要な遷移は毎pollで新しい履歴sequenceだけを読み、CONTROL行として自動出力する。
電源・探索・交渉・開始・停止・切断・失敗とaction列を残し、高頻度の媒体送信はSTATE累計で補完する。
履歴読取りが間に合わなかった数はcontrol_lost、USBキュー欠落はtrace_lostに分けて報告する。
CONTROLのresult_statusはその遷移後の状態値で、非同期actionの成功を先取りして意味しない。
ログはUSB未接続でも待たない。`?`は状態、`h`は履歴、`d`は停止、`c`は接続要求。
有界USBキューが溢れたときはtrace_lostで報告し、無制限にログを溜めない。

## ビルド

```sh
PICO_SDK_PATH=/path/to/pico-sdk cmake -S examples/A2DPSourcePlayer \
  -B build/a2dp-player -DPLAYER_TARGET_ADDRESS=00:11:22:33:44:55
cmake --build build/a2dp-player -j4
```

SDK 2.1.1の `pico_btstack_sbc_encoder` を使用する。USBは非ブロッキングCDCと
SDK標準reset interfaceを併用し、picotoolの通常書き換え経路を残す。
UF2の生成と実機の聴取確認は別の検証である。

## 実行と状態確認

上のコマンドはリポジトリのルートで実行し、SDK パスと仮の peer アドレスを自分の環境へ置き換えます。Pico W、Pico SDK、CMake、ARM GCC が必要です。環境準備と UF2 書き込みは[ビルドガイド](../../docs/build_guide_jp.md)を参照してください。

書き込み・起動後は USB CDC console で `?` を送り power=2（Working）を待ってから `c` を一度送ります。OPEN 後に4秒待機し、3秒のトーン送信後に切断します。`?` で Playing と packet 増加を確認し、途中停止には `d`、遷移確認には `h` を使います。音量は接続先で小さくしてから開始してください。

接続しない場合は peer アドレスと相手の接続受付状態、error/status を確認します。`c` の連打を復旧手順にしないでください。出力欠落は `trace_lost` / `control_lost` で確認し、必要なら `?` で現在値を再取得します。

## 別プロジェクトから使う

`pico_sdk_init()` 後にhelperをincludeし、アプリtargetへ1回適用する。
旧 `PicoWBluetooth` target / background arch / connection pluginはリンクしない。

```cmake
# pico_sdk_import.cmake の後、pico_sdk_init() より前
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
pico_sdk_init()
add_executable(my_player main.cpp)
include(/path/to/PicoW_Bluetooth_lib/cmake/picow_a2dp_source.cmake)
picow_target_a2dp_source(my_player ${CMAKE_CURRENT_SOURCE_DIR})
pico_add_extra_outputs(my_player)
```

A2DP単体ならアプリディレクトリに本見本の `btstack_config.h` をコピーする。
SPP/AVRCP併用ではそのまま流用せず、[DeveloperSamplesの共用設定](../DeveloperSamples/btstack_config.h)と
[SPP統合契約](../../docs/spp_sdk_integration_jp.md)のHCI容量・profile資源を確認する。
helperは音声ソース群とpoll方式のSDK依存をまとめる。USBはアプリの責務で、
通常はSDK stdio USBを使ってよい。本見本の独自CDCを再利用する場合だけ
`usb_descriptors.c` / `tusb_config.h` とCMakeのreset interface設定も引き継ぐ。
音源とplayerをmainループで生存させる。最短コードは自動接続・連続再生なので、
短時間試験には見本の `auto_connect=false` / `play_duration_us=3000000` を使う。

## 実装を読む順序と確認範囲

1. `main.cpp`: 音源・設定・begin・pollの4段階。
2. `audio-core/A2dpSourcePlayer.cpp`: BTstack通知の正規化とAction実行。
3. `audio-core/connection/A2dpConnection.h`: State / Event / Rules / Engine。
4. `docs/connection_state_transitions_jp.md`: 実測と規則の対応。

共通stack初期化は共有runtime、A2DP固有の初期化・SDP登録は `execute(Initialize)`、packet handlerは `normalize()`、
再生タイマーは `Rules::apply(Tick)`、can-send処理はcredit規則と
`execute(PumpAudioOnce)` に対応する。現行pipelineは未送信encoded batchや部分PCMを保持する
[共存時の送信延期契約](../../docs/a2dp_spp_coexistence_jp.md)にも従う。

本見本自体の発音・機器互換性・長時間動作の保証ではありません。詳しくは[検証範囲](../../docs/validation_jp.md)を参照してください。
