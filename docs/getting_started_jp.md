# はじめての Pico W Bluetooth アプリ

[English](getting_started.md) · [README](../README_jp.md) · [開発ツールの準備](build_guide_jp.md)

独立した C++ プロジェクトにこのライブラリを組み込み、Pico W で動かす UF2 を作ります。Pico SDK を使う手順で、Arduino や MicroPython は使いません。

## Pico に何をさせたいですか？

| やりたいこと | 選ぶ役割 | この先の入口 |
|---|---|---|
| 自分のプログラムの音声を Bluetooth スピーカーへ送る | A2DP **Source** | **A：小さなアプリを自分で書く** |
| スマホ・PC の音声を受け取り、再生・一時停止を送り返す | A2DP **Sink** ＋ AVRCP **Controller** | **B：完成したコンソールアプリをコピーする** |
| 操作ボタンなしで音声だけ受け取る | A2DP **Sink** | [受信サンプル](../examples/A2DPSinkReceiver/README_jp.md) |
| 外部からの操作ボタンを受け取る／任意のデータをやり取りする | AVRCP **Target**／**SPP** | [開発者向けサンプル](developer_samples_jp.md) |

Source / Sink は音声の向き、Controller / Target は操作コマンドの向きを表します。音声を受信する Pico から送信元へ操作を送るなら、Sink ＋ Controller です。

**A と B のどちらかを選んでください。** A は短いアプリのコードをすべて掲載しています。B は接続管理、ボタンの押下・解放、キュー、終了処理を備えたサンプル一式をコピーして使います。

**どちらもスピーカーから音を出すアプリではありません。** A は約10秒の無音 PCM を送信します。動作を確認したら、自分の音声データに差し替えられます。B は受信した PCM を破棄します。Pico から音を出すには、処理を止めない `IAudioSink` の実装と音声出力用ハードウェアが必要です。[PCM 受信サンプル](../examples/A2dpSinkPcmConsumer/README_jp.md)では、録音せずに受信データを処理する方法を紹介しています。

## コマンドを打つ前に

以下は Linux の Bash ターミナルで実行するコマンドです。次を準備してください。

- CMake、Make または Ninja、Python 3、Git、Arm クロスコンパイラー（`arm-none-eabi-gcc` / `arm-none-eabi-g++`）と Arm 用 C++ 標準ライブラリ。
- **BTstack・CYW43・TinyUSB のサブモジュールを含む Pico SDK** と、picotool などのビルド補助ツール。[ビルドガイド](build_guide_jp.md) に記載された SDK の版を使ってください。組み込み用ヘルパーは、対応しない SDK の構成を検出するとエラーにします。
- `cmake`、`tools`、`audio-core`、`avrcp-sdk`、`avrcp-core`、`bluetooth-runtime`、`examples` を含む**ライブラリの完全なソース一式**。ヘッダーファイルだけでは足りません。
- 実機で動かす場合は、**Pico W**、データ通信対応 USB ケーブル、シリアルターミナル、使用してよい Bluetooth Classic の相手機器。普通の Pico には無線がありません。Raspberry Pi のコンピューターはビルド用には使えますが、ここでの書き込み先は Pico W です。

現在のターミナルで SDK の場所を設定します。値は自分のインストール先に変えてください。

```sh
export PICO_SDK_PATH="$HOME/pico/pico-sdk"
cmake --version
arm-none-eabi-g++ --version
python3 --version
test -f "$PICO_SDK_PATH/external/pico_sdk_import.cmake"
test -f "$PICO_SDK_PATH/lib/btstack/src/btstack.h"
```

`test` は成功時に何も表示しません。初回の CMake 構成では picotool などの補助ツールを取得することがあります。[ツールの準備](build_guide_jp.md)を先に済ませてください。対応する picotool を導入済みでも CMake が見つけられない場合は、`picotoolConfig.cmake` があるディレクトリを `-Dpicotool_DIR=/path/to/picotool/cmake-directory` で指定できます。UF2 を作るため、picotool は有効にしておきます。オフラインでビルドする場合は、SDK と補助ツールを事前にそろえてください。

## A. 小さな Source アプリを書く

アプリの実行ターゲットを `my_player` とし、CMake ヘルパーでライブラリのソースと SDK の依存関係を追加します。ライブラリのルートを `add_subdirectory()` で読み込んだり、旧 `picow_bluetooth_lib` ターゲットをリンクしたりする必要はありません。

### A1. アプリの中にライブラリを置く

新しい空の `my-player` ディレクトリを用意し、ライブラリ一式をその `vendor/PicoW_Bluetooth_lib` に取得します。

```sh
mkdir -p my-player/vendor
git clone https://github.com/tanep3/PicoW_Bluetooth_lib.git my-player/vendor/PicoW_Bluetooth_lib
cd my-player
cp vendor/PicoW_Bluetooth_lib/examples/A2DPSourcePlayer/btstack_config.h .
```

ソース ZIP を使う場合は `git clone` の代わりに、展開したフォルダー全体を `my-player/vendor/PicoW_Bluetooth_lib` として配置し、`cd my-player` から続けます。フォルダーを二重にせず、`vendor/PicoW_Bluetooth_lib/cmake/picow_a2dp_source.cmake` が存在することを確認してください。

このあと `CMakeLists.txt` と `main.cpp` を作ります。完成時の配置は次のとおりです。

```text
my-player/
  CMakeLists.txt
  main.cpp
  btstack_config.h
  vendor/
    PicoW_Bluetooth_lib/
      cmake/picow_a2dp_source.cmake
      audio-core/
      bluetooth-runtime/
      ...
```

`btstack_config.h` は Bluetooth スタックの機能やバッファ量を決める設定ファイルです。ここでは **A2DP 単体用**をコピーします。SDK の中ではなく、自分のアプリ直下に置きます。

### A2. CMake でアプリに組み込む

`my-player/CMakeLists.txt` を次の内容で作ります。

```cmake
cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico_w)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(MyPlayer C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
pico_sdk_init()

set(PICOW_BLUETOOTH_ROOT "${CMAKE_CURRENT_LIST_DIR}/vendor/PicoW_Bluetooth_lib")
add_executable(my_player main.cpp)
include("${PICOW_BLUETOOTH_ROOT}/cmake/picow_a2dp_source.cmake")
picow_target_a2dp_source(my_player "${CMAKE_CURRENT_LIST_DIR}")
pico_enable_stdio_usb(my_player 1)
pico_enable_stdio_uart(my_player 0)
target_compile_definitions(my_player PRIVATE PICO_STDIO_USB_STDOUT_TIMEOUT_US=0)
pico_add_extra_outputs(my_player)
```

すでに Pico SDK アプリがあるなら、既存の SDK 読み込み・初期化と `add_executable(...)` を使います。**`pico_sdk_init()` の後**に、ライブラリのパス、`include(...)`、`picow_target_a2dp_source(アプリのターゲット 設定ファイルのディレクトリ)` を追加してください。`PICO_CXX_ENABLE_EXCEPTIONS` は SDK 初期化前に有効にします。同名の実行ターゲットをもう一つ作る必要はありません。

ヘルパーが実装ソースとヘッダーの検索先を追加し、`pico_stdlib`、`pico_cyw43_arch_poll`、`pico_btstack_classic`、`pico_btstack_cyw43`、`pico_btstack_sbc_encoder` をリンクします。この依存一覧を自分で重複指定したり、先に別の `.a` ファイルを作ったりする必要はありません。

### A3. ヘッダーを include して使う

`my-player/main.cpp` に次のコードを書きます。**実機で動かす場合は、ビルドする前に `settings.target_address` の空文字列を、[スピーカーの Classic Bluetooth アドレス](build_guide_jp.md#peer-address)へ置き換えてください。**

```cpp
#include "pico/stdlib.h"
#include "A2dpSourcePlayer.h"
#include "IAudioSource.h"
#include <algorithm>
#include <cstdio>

class Silence final : public IAudioSource {
public:
    bool hasSamples() const override { return true; }
    size_t read(int16_t* output, size_t samples) override {
        std::fill_n(output, samples, int16_t{0});
        return samples; // int16_t values, with left/right interleaved
    }
};

int main() {
    stdio_init_all();
    static Silence source;
    A2dpSourcePlayer::Settings settings;
    settings.target_address = ""; // Set your authorized peer's Bluetooth address.
    settings.sample_rate = 48000;
    settings.auto_connect = false;
    settings.play_duration_us = 10000000; // Ten seconds, then disconnect.
    static A2dpSourcePlayer player(source, settings);

    puts("b=start radio; c=connect; d=disconnect; ?=status");
    while (getchar_timeout_us(0) != 'b') tight_loop_contents();
    if (player.begin() != A2dpSourcePlayer::BeginResult::Ok) {
        puts("begin failed: check the peer/settings, then reset.");
        while (true) tight_loop_contents();
    }
    while (true) {
        player.poll();
        const auto state = player.snapshot();
        const int command = getchar_timeout_us(0);
        if (command == 'c' && state.power == a2dp::Power::Working &&
            state.error == a2dp::Error::None &&
            state.inquiry == a2dp::Inquiry::Idle &&
            state.signaling == a2dp::Signaling::Idle &&
            state.stream == a2dp::Stream::Idle) player.connect();
        if (command == 'd') player.disconnect();
        if (command == '?')
            printf("power=%u stream=%u error=%u packets=%lu\n",
                unsigned(state.power), unsigned(state.stream),
                unsigned(state.error), (unsigned long)player.diagnostics().sent_packets);
    }
}
```

テンプレートでは、コピーしただけで意図しない相手へ接続しないよう、アドレスを空にしています。実機用には、この後のビルドまでに設定してください。コンパイルだけ確認する場合は空のままで構いません。その場合、`begin()` は失敗を返し、無線を開始しません。

アドレスを設定したアプリでは、`b` で無線を初期化し、その相手とのペアリング確認を許可します。`?` で状態を確認し、`power=2` で待機状態になったら `c` で1回の接続を要求します。自動再試行はしません。復帰できないエラーが起きた場合はボードをリセットします。

音源は 48 kHz・符号付き16 bit・ステレオの無音 PCM です。`samples` は `int16_t` 値の個数で、バイト数や左右1組の個数ではありません。`stream=4`（Playing）とパケット数の増加を確認してください。無音なので、音が聞こえないだけでは失敗と判断できません。`d` は切断要求で、無線の電源を切る操作ではありません。音源や動作を変えるときは [API リファレンス](api_reference_jp.md)も参照してください。

### A4. 自分のアプリをビルドする

Pico SDK と ARM コンパイラを導入したうえで、`my-player` で実行します。

```sh
export PICO_SDK_PATH="$HOME/pico/pico-sdk" # 自分の SDK の場所
cmake -S . -B build
cmake --build build --target my_player --parallel 2
ls -lh build/my_player.{elf,uf2}
```

`Built target my_player` が表示され、**`my-player/build/my_player.uf2`** ができれば成功です。[書き込みと動作確認](#flash-and-run)へ進んでください。このコマンドでは、まだボードへの書き込みは行いません。

ほかのプロファイルを使う場合は、[組み込みガイド](build_guide_jp.md#integration)から対応するヘルパーと設定を選んでください。上の Source 専用 `btstack_config.h` を、そのまますべての組み合わせに使うことはできません。

## B. Sink ＋ Controller コンソールを自分のアプリにする

用意されたコンソール一式をコピーし、自分のアプリの出発点にします。**新しい** `my-receiver` フォルダーを作りたい場所で実行してください。ZIP を使う場合は clone 行の代わりに、展開したライブラリ全体を `my-receiver/vendor/PicoW_Bluetooth_lib` に配置します。

```sh
mkdir -p my-receiver/vendor
git clone https://github.com/tanep3/PicoW_Bluetooth_lib.git \
  my-receiver/vendor/PicoW_Bluetooth_lib
cd my-receiver
cp vendor/PicoW_Bluetooth_lib/examples/AvrcpMediaControlSample/{main.cpp,Application.h,CommandParser.h,sample_config.h.in,CMakeLists.txt} .
cp vendor/PicoW_Bluetooth_lib/examples/A2DPSinkReceiver/btstack_config.h .
python3 - <<'PYTHON'
from pathlib import Path
p = Path("CMakeLists.txt")
s = p.read_text()
old = "${CMAKE_CURRENT_LIST_DIR}/../../cmake/"
assert s.count(old) == 2, "Example changed; review its CMake includes first"
p.write_text(s.replace(old,
    "${CMAKE_CURRENT_LIST_DIR}/vendor/PicoW_Bluetooth_lib/cmake/"))
PYTHON
```

ライブラリ全体を `vendor` に残してください。サンプルのフォルダーだけでは足りません。コピー先でも設定を読めるように、コンソール側の相対パス用ラッパーではなく、実体のある Sink の `btstack_config.h` をコピーしています。CMake の include も 2 か所だけ、`vendor` のヘルパーを参照するように変えます。

```text
my-receiver/
  CMakeLists.txt
  main.cpp
  Application.h
  CommandParser.h
  sample_config.h.in
  btstack_config.h
  vendor/PicoW_Bluetooth_lib/   （ライブラリ全体）
  build/                      （CMake が作成）
    sample_config.h           （自動生成。直接編集しない）
```

コピーした CMake 内の次の呼び出しが、**同じ実行ファイル**へ両方の役割を追加します。

```cmake
picow_target_a2dp_sink(avrcp_media_control_sample ${CMAKE_CURRENT_LIST_DIR})
picow_target_avrcp_controller(avrcp_media_control_sample ${CMAKE_CURRENT_LIST_DIR})
```

`configure_file(...)`、ビルド先の include パス、ウォッチドッグのリンク、USB の設定、アプリのヘッダーも残してください。すべてこのアプリを動かすための構成です。`sample_config.h` は CMake オプションから生成されます。

### B1. 音声送信元を設定してビルドする

設定するのは、**音声を送るスマートフォンや PC** の [Classic Bluetooth アドレス](build_guide_jp.md#peer-address)です。Pico 自身や Wi-Fi のアドレスではありません。`my-receiver` で次を実行し、尋ねられたら送信元のアドレスを入力してください。

```sh
read -r -p "Audio source Bluetooth address: " SOURCE_BT_ADDRESS
: "${PICO_SDK_PATH:?Set the path to your Pico SDK}"
: "${SOURCE_BT_ADDRESS:?Enter the actual audio source Classic Bluetooth address}"
cmake -S . -B build \
  -DAVRCP_MEDIA_ENABLE_RADIO=ON -DAVRCP_MEDIA_ALLOW_PAIRING=ON \
  -DAVRCP_MEDIA_PEER="${SOURCE_BT_ADDRESS:?Set the actual Classic Bluetooth address}"
cmake --build build --target avrcp_media_control_sample --parallel 2
ls -lh build/avrcp_media_control_sample.{elf,uf2}
```

**`my-receiver/build/avrcp_media_control_sample.uf2`** が生成されます。アドレスは2桁の16進数をコロンで区切った6組で指定します。無線を使う構成では、全ゼロのアドレスは受け付けません。

このファームウェアは起動時に USB コンソールを開き、`START` が送られるまで無線を初期化せず待ちます。`AVRCP_MEDIA_ALLOW_PAIRING=ON` は、後から `PAIR` で指定相手のペアリングを5分間受け付けるための設定です。起動直後に自動でペアリングすることはありません。

<a id="flash-and-run"></a>
## Pico W に書き込んで動かす

[BOOTSEL と USB シリアルの手順](build_guide_jp.md#flash)に沿って、選んだ UF2 を Pico W に書き込み、シリアル端末を開きます。書き込みでボード上のプログラムが置き換わるため、元に戻す必要がある場合は、元のファームウェアを保管しておいてください。

- **A の場合：** `build/my_player.uf2` を書き込みます。`b`、続いて `?` を送り、`power=2` の待機状態になったら `c` を1回送ります。`stream=4` と `packets` の増加が送信の目安です。送るデータは無音で、再生開始から約10秒後に切断します。`d` は切断要求で、無線の電源を切る操作ではありません。
- **B の場合：** `build/avrcp_media_control_sample.uf2` を書き込みます。[メディア操作サンプルの手順](../examples/AvrcpMediaControlSample/README_jp.md#operator-workflow)に従い、`START` → READY を待つ → `PAIR` → 送信元でペアリングして音声出力先に Pico を選ぶ、の順に進みます。AVRCP の接続、PLAY / PAUSE の確認、`SHUTDOWN` での終了も、同じ手順を使います。A の1文字コマンドではなく、大文字のコマンドを1行ずつ送ってください。

メディア操作サンプルには PLAY / PAUSE があり、NEXT や音量変更のコマンドはありません。また、どちらのアプリも Pico から音は出しません。コマンドを受け付けたという表示だけで判断せず、接続状態や送信元のプレーヤー画面も確認してください。

<a id="compile-only"></a>
## 任意：ボードや接続相手なしでコンパイルを確認する

実機向けビルドの前に、この手順を行う必要はありません。A では `settings.target_address` を空にしたままビルドできます。

B では、**同じアプリ**の無線とペアリングを OFF にし、別の出力先にビルドします。

```sh
cmake -S . -B build-compile-only \
  -DAVRCP_MEDIA_ENABLE_RADIO=OFF -DAVRCP_MEDIA_ALLOW_PAIRING=OFF \
  -DAVRCP_MEDIA_PEER=00:00:00:00:00:00
cmake --build build-compile-only --target avrcp_media_control_sample --parallel 2
```

この構成は USB と無線の初期化前に終了します。シリアルコンソールは出ず、Bluetooth の機器一覧にも現れません。実機で使う場合は、B1 で作った `build/avrcp_media_control_sample.uf2` を書き込んでください。

どちらの自作アプリも、独立したディレクトリで ARM ビルドし、UF2 生成を確認しています。これはコンパイルとリンクの確認であり、このアプリ自体の実機試験は未実施です。

## 最初につまずきやすいところ

| 症状 | 確認すること |
|---|---|
| `pico_sdk_import.cmake` が見つからない | 現在のターミナルで実際の SDK パスを export してから構成する。 |
| BTstack / CYW43 / TinyUSB がない | SDK のサブモジュールを準備する。別のヘッダーをアプリに足して解決しようとしない。 |
| オフラインで picotool が見つからない／UF2 がない | ビルドガイドに従って SDK に適したツールを用意・指定し、再構成する。ELF だけでは今回の目標達成ではない。 |
| A で C++ 例外サポートのエラー | `pico_sdk_init()` より前に `set(PICO_CXX_ENABLE_EXCEPTIONS 1)` を残す。 |
| Sink のオーバーレイやリンク時の配置が拒否される | 対応する SDK の版を使う。検証を無効にしたり、インストール済み SDK を手で改変したりしない。 |
| `Application.h` / `sample_config.h` がない | B のファイル一式をコピーし、`configure_file` とビルド先の include を残す。 |
| B の USB コンソールが出ない | B1 の `build/avrcp_media_control_sample.uf2` を書き込んだか確認する。任意のコンパイル確認用ファームウェアではコンソールが出ない。 |
| A で `begin failed` | `settings.target_address` にスピーカーのアドレスを設定し、再ビルド・書き込みを行う。空のままでは無線を開始できない。 |
| リセット後にペアリングできない | 標準の鍵保存は RAM。相手側に古い鍵が残ることがあるので、試験用 Pico の登録だけを削除して明示的に再ペアリングする。 |
| 接続したのに無音 | A は無音を送り、B は PCM を捨てる。どちらも可聴再生の例ではない。 |
| PAUSE を受け付けたようなのに再生が続く | 対応する応答、操作対象のプレーヤー、OS のメディア操作先を確認する。ローカルの戻り値だけで成功扱いしない。 |

## 自分の処理を追加するとき

各役割のオブジェクトはアプリの動作中ずっと保持し、すべて同じメインループから `poll()` します。旧プラグインと混在させたり、BTstack を二重に初期化したり、別コアや割り込みから呼んだりしないでください。コールバックは短く保ち、一時的に借りたデータはコピーしてから後の処理へ渡します。

関数や戻り値は [API リファレンス](api_reference_jp.md)、構成の変更は[組み込みガイド](build_guide_jp.md#integration)、音声出力の実装は [PCM の受け渡し](a2dp_sink_pcm_jp.md)を参照してください。
