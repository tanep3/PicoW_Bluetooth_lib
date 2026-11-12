# Pico W Bluetooth Library ビルドガイド

[English](build_guide.md)

[README に戻る](../README_jp.md) / [サンプルの詳しい操作](../examples/DeveloperSamples/README_jp.md)

使いたいサンプルを一つ選び、接続相手を設定してビルドし、Pico W に書き込んで動かします。その後、自分のアプリへの組み込みへ進みます。

対象読者は、C++ の基本を知っていて、Pico SDK や Bluetooth の開発環境を初めて組む方です。コマンド例は Bash と Ubuntu / Debian 系 Linux を前提とします。Windows では WSL に Linux のビルド環境を用意する方法がありますが、USB の書き込み・シリアル接続は別に準備が必要です。WSL、macOS、Windows ネイティブ環境での動作は未確認です。

**コマンドを打つ前に、目的に合うルートを一つ選びます。** 全サンプルをビルドする必要はありません。

| やりたいこと | ビルド・設定 | 書き込み後 |
|---|---|---|
| 音声送信、SPP通信、相手からのボタン受信 | 下の手順3（DeveloperSamples） | 手順5の1文字コマンド |
| 音声を受けて送信元を操作する | [Sink/Controllerビルド](#sink-avrcp) | [メディア操作サンプルの手順](../examples/AvrcpMediaControlSample/README_jp.md#operator-workflow)。行単位のコマンド |
| 受信PCMを処理する | [Sinkビルド](#sink-avrcp) | [PCM受信の手順](../examples/A2dpSinkPcmConsumer/README_jp.md#first-use)。録音は不要 |
| 独立した自分のアプリを作る | [はじめてのアプリ](getting_started_jp.md) | そのアプリで実装した操作 |

ツールの準備と BOOTSEL での書き込みは共通です。シリアル端末から送るコマンドは、選んだサンプルの説明に従ってください。

## 1. ビルドの全体像

```text
アプリの C++ + このライブラリ + Pico SDK / BTstack
                         ↓ CMake でビルド設定を作る
                         ↓ ARM 向けコンパイラでコンパイル・リンク
                  .elf と .uf2 ができる
                         ↓ 選んだ UF2 を Pico W へ書き込む
                  シリアル端末から操作する
```

- **Pico SDK**: GPIO、USB、無線などを使うための Raspberry Pi 公式開発キットです。Bluetooth の基盤である BTstack も含みます。
- **ARM クロスコンパイラ**: 開発用 PC ではなく、Pico W の CPU が実行するプログラムを作ります。コマンド名は `arm-none-eabi-gcc` / `arm-none-eabi-g++` です。
- **CMake**: どのソースをどの設定でビルドするかを決めます。`-S` はソースの入口、`-B` は生成物の保存先です。
- **target**: ビルドする実行ファイルの名前です。このプロジェクトでは `sample_a2dp_source` などを指定します。
- **ELF / UF2**: ELF は実行コードとデバッグ情報などを含むファイル、UF2 はボードへの書き込みに使うファイルです。

開発用 PC は Raspberry Pi 5 でも一般的な Linux PC でも構いません。**実行先のボードは Raspberry Pi Pico W** です。通常の Pico には無線機能がありません。以下のサンプルは CMake 内で `PICO_BOARD=pico_w` を指定しており、別ボード用として検証していません。

<a id="prerequisites"></a>
## 2. 必要なものを用意する

### ハードウェア

ビルドだけならボードは不要です。実機で使う段階では次を用意します。

- Raspberry Pi Pico W と、充電専用ではない USB データケーブル。
- 用途に合う Bluetooth Classic 対応の接続相手。
  - PicoをA2DP Sourceとして使う場合：音声を受けるスピーカーなど。
  - PicoをA2DP Sinkとして使う場合：Bluetooth ClassicのSBC音声を送れるPCやスマートフォン。
  - SPP: RFCOMM サービスを提供する機器・アプリ、または Pico のサービスへ接続するクライアント。
  - PicoをAVRCP Targetとして使う場合：Pico へ接続して操作を送れるコントローラー。すべてのヘッドセットが単体接続できるとは限りません。
  - PicoをAVRCP Controllerとして使う場合：AVRCP Target に対応した送信元のプレーヤー。OS やプレーヤーによって操作への反応が異なります。
- USB シリアル端末ソフト。UART を使う場合は 3.3 V 対応の USB-UART アダプター。

### Linux 側のツール

新しく環境を用意する場合のパッケージ例です。すでに導入済みなら重複インストールは不要です。

```sh
sudo apt update
sudo apt install build-essential cmake git python3 \
  gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
  pkg-config libusb-1.0-0-dev
```

ARM 向け C++ 標準ライブラリは容量が大きいため、空き容量も確認してください。`libusb` は SDK が補助ツールの picotool をビルドする場合に備えるものです。

導入確認:

```sh
cmake --version
arm-none-eabi-gcc --version
arm-none-eabi-g++ --version
c++ --version
python3 --version
```

このライブラリは C++17 を使い、CMake の最低バージョンを 3.13 としています。実際にビルドを確認したのは 3.28.3 です。[確認済みの環境](#verified)も参照してください。過去の HFP 実験には 3.19 以上が必要なものがありますが、このガイドのサンプルには関係しません。

### Pico SDK

すでに SDK がある場合は、その場所とバージョンを確認してください。以下は**新しい SDK ディレクトリを作る場合**の例です。

現在の検証では SDK 2.1.1 系の commit `9a4113fbbae65ee82d8cd6537963bc3d3b14bcca` を使用しています。通常の `2.1.1` タグの直後に picotool の取得バージョンなどを修正した版です。再現しやすいよう、以下では同じ commit を指定します。

```sh
mkdir -p "$HOME/pico"
git clone https://github.com/raspberrypi/pico-sdk.git "$HOME/pico/pico-sdk"
git -C "$HOME/pico/pico-sdk" checkout 9a4113fbbae65ee82d8cd6537963bc3d3b14bcca
git -C "$HOME/pico/pico-sdk" submodule update --init --recursive
export PICO_SDK_PATH="$HOME/pico/pico-sdk"
```

`checkout` で「detached HEAD」と表示されるのは、特定の commit を固定しているためです。ここでは正常です。既存の同名ディレクトリがある場合、clone 先には別の新しい名前を使ってください。

**submodule の取得は省略できません。** Git submodule は SDK 本体とは別に管理される依存コードです。BTstack、CYW43 ドライバ、TinyUSB などが欠けるとビルドできません。

```sh
test -f "$PICO_SDK_PATH/external/pico_sdk_import.cmake"
test -f "$PICO_SDK_PATH/lib/btstack/src/btstack.h"
test -d "$PICO_SDK_PATH/lib/cyw43-driver/src"
test -d "$PICO_SDK_PATH/lib/tinyusb/src"
git -C "$PICO_SDK_PATH" submodule status
```

`test` は成功時には何も出力しません。直後の `echo $?` が `0` なら成功です。`submodule status` の行頭が `-` なら未初期化です。

`export` は現在のターミナルだけに適用されます。新しいターミナルを開いたら再設定してください。SDK が別の場所にあるなら、その実際のパスを使います。

初回構成では、SDK が picotool などの補助ツールを取得・ビルドすることがあります。インターネット接続と Git が必要になる場合があります。SDK 本体の clone だけで完全オフライン環境になるわけではありません。

参考: [Pico SDK の公式リポジトリ](https://github.com/raspberrypi/pico-sdk)、[picotool の公式リポジトリ](https://github.com/raspberrypi/picotool)。

### ライブラリを取得する

```sh
git clone https://github.com/tanep3/PicoW_Bluetooth_lib.git
```

ソース ZIP を使う場合は展開し、以降の `/path/to/PicoW_Bluetooth_lib` をそのフォルダーに置き換えます。`cmake`、`tools`、`examples` を含むソース一式が必要です。

<a id="source-build"></a>
## 3. 接続相手を設定してサンプルをビルドする

ここでは `sample_a2dp_source` を使い、Bluetooth スピーカーへ約10秒の無音データを送ります。音声を受信したい場合や、送信元の再生を操作したい場合は、下の [Sink サンプル](#sink-avrcp)へ進んでください。

スピーカーの [Classic Bluetooth アドレス](#peer-address)を調べ、ライブラリのディレクトリで次を実行します。`/path/to/` は実際の場所に置き換えてください。アドレスを尋ねられたら、調べた値を入力します。

```sh
cd /path/to/PicoW_Bluetooth_lib
export PICO_SDK_PATH="$HOME/pico/pico-sdk"
read -r -p "Speaker Bluetooth address: " PEER_BT_ADDRESS
: "${PEER_BT_ADDRESS:?Enter the actual speaker Classic Bluetooth address}"
cmake -S examples/DeveloperSamples -B .work/builds/samples-usb \
  -DSAMPLE_PEER="${PEER_BT_ADDRESS:?Set the actual Classic Bluetooth address}" \
  -DSAMPLE_USB_CONSOLE=ON -DSAMPLE_ALLOW_PAIRING=OFF \
  -DSAMPLE_CHANNEL_0=1 -DSAMPLE_CHANNEL_1=2
cmake --build .work/builds/samples-usb --target sample_a2dp_source --parallel 2
ls -lh .work/builds/samples-usb/sample_a2dp_source.{elf,uf2}
```

`Built target sample_a2dp_source` と表示されればビルド成功です。**`.work/builds/samples-usb/sample_a2dp_source.uf2`** を[手順4](#flash)で書き込みます。

- `SAMPLE_PEER`：接続する機器のアドレスです。
- `SAMPLE_USB_CONSOLE=ON`：USB シリアルで操作と状態表示を行います。
- `SAMPLE_ALLOW_PAIRING=OFF`：SPP / AVRCP 単体用の設定です。**A2DP では、この値が OFF でも開始時に指定相手とのペアリング確認を許可します。**
- `SAMPLE_CHANNEL_0`、`SAMPLE_CHANNEL_1`：この音声専用サンプルでは使いませんが、共通設定として 1〜30 の異なる番号が必要です。
- `--parallel 2`：最大2つのコンパイルを並行して実行します。メモリが足りない場合は `1` にします。

このコマンドはビルドだけを行います。書き込み後も、`b` を送るまでは無線を起動せず、`c` を送るまでは接続しません。

ここで使うのはサンプル専用の CMake です。ルートの `CMakeLists.txt` や `make.sh` は旧静的ライブラリ用です。

SPP、AVRCP Target、複数機能の組み合わせを試す場合は、[DeveloperSamples の一覧と設定](../examples/DeveloperSamples/README_jp.md)から選んでください。`--target` を省略すると全7例をビルドできます。SPP のチャンネル番号は使用するサービスに合わせます。`c` で接続する場合は相手側、`l` で待ち受ける場合は Pico 側の番号です。SPP / AVRCP 単体で初回ペアリングを行う場合は、`SAMPLE_ALLOW_PAIRING=ON` を指定します。選んだサンプルに合わせ、最初の構成時に設定してください。

CMake はビルド先に設定を保存します。設定を変更したら、構成とビルドをもう一度行ってください。以前の値が残らないよう、接続相手とペアリングの設定は明示します。SDK・コンパイラ・ボードを変える場合は、新しい `-B` ディレクトリを使います。

<a id="sink-avrcp"></a>
## 音声を受信する・送信元を操作する

目的に合うサンプルを一つ選んでください。それぞれの README にビルドコマンド、生成ファイル、操作手順がまとまっています。先に DeveloperSamples をビルドする必要はありません。

| やりたいこと | ビルドと操作の手順 | 起動時の動作 |
|---|---|---|
| 音声を受信し、送信元へ PLAY / PAUSE を送る | [メディア操作サンプル](../examples/AvrcpMediaControlSample/README_jp.md) | USB コンソールが起動。`START` を送ると無線を初期化 |
| 受信した PCM を自分の処理へ渡す | [PCM 受信サンプル](../examples/A2dpSinkPcmConsumer/README_jp.md) | 起動直後に無線を初期化。Ready を待ってから `l` を送信 |

実機で使う場合は、`AVRCP_MEDIA_ENABLE_RADIO=ON` または `SINK_PCM_ENABLE_RADIO=ON` を指定し、**音声を送るスマートフォンや PC** の Bluetooth アドレスを指定します。全ゼロのアドレスは使えません。ペアリングの許可は指定相手に限られます。メディア操作サンプルでは、ペアリングを許可してビルドしても、`PAIR` を送るまで受付を開始しません。

メディア操作サンプルは大文字のコマンドを1行ずつ、PCM 受信サンプルは1文字ずつ送ります。`SAMPLE_*` 設定は使いません。別のサンプルの設定やコマンドと混ぜず、選んだ README の手順を使ってください。

どちらもスピーカーから音を出すサンプルではありません。メディア操作サンプルは PCM を破棄し、PCM 受信サンプルは標準でフレーム数とピーク値を調べます。最初の動作確認に録音は不要です。音を出すには、音声出力用のハードウェアと `IAudioSink` の実装を追加してください。メディア操作サンプルの再生操作は PLAY / PAUSE です。NEXT や音量変更のコマンドはありません。

このままビルドする場合はリポジトリの配置を保ってください。独立したアプリに移す手順は[はじめてのアプリ](getting_started_jp.md)にあります。

<a id="peer-address"></a>
## 接続相手の Bluetooth アドレスを調べる

**peer（ピア）** は Pico の接続相手です。設定するのは **Classic Bluetooth のアドレス**で、Wi-Fi の MAC アドレスや機器の表示名とは異なります。BLE スキャンで得たアドレスも、そのまま使えるとは限りません。

| Pico の用途 | 設定するアドレス |
|---|---|
| スピーカーへ音声を送る | スピーカーの Bluetooth アドレス |
| Windows / Linux PC から音声を受け、その PC を操作する | その PC の Bluetooth アダプターのアドレス |
| スマートフォンから音声を受ける | スマートフォンの Classic Bluetooth アドレス |

**Linux PC が送信元の場合**は、BlueZ の `bluetoothctl list` でアダプター一覧を表示し、使うアダプターのアドレスを確認できます。すでに認識しているスピーカーは `bluetoothctl devices` で確認できます。これらの一覧コマンドはスキャンやペアリングを開始しません。詳しくは [BlueZ のコマンド説明](https://github.com/bluez/bluez/blob/master/doc/bluetoothctl.rst)を参照してください。ビルド用 PC と音声送信元が別の場合は、送信元のアドレスを使います。

**Windows PC が送信元の場合**は、デバイスマネージャー → Bluetooth → メーカー名の付いた Bluetooth アダプター → プロパティ → 詳細設定（Advanced）を確認します。Microsoft Bluetooth Enumerator や RFCOMM Protocol TDI ではなく、実際のアダプターを選びます。[Microsoft のアダプター確認手順](https://support.microsoft.com/en-us/windows/hardware/bluetooth/what-bluetooth-version-is-on-a-windows-device)も参考になります。Address / Bluetooth Radio Address 欄があれば、その値を使います。表示項目はドライバーによって異なり、この欄がない場合もあります。

見つからない場合は、機器やアダプターのメーカーの手順で確認してください。スマートフォンでは非表示の場合もあります。このサンプルは相手をアドレスで指定するため、名前だけでは設定できません。アドレスを推測せず、確認できる機器を使ってください。

アドレスが12桁続きやハイフン区切りで表示される場合は、並び順を変えず、2桁ずつコロンで区切った `XX:XX:XX:XX:XX:XX` 形式にします。

### ペアリングと再接続

ペアリングでは、接続相手との認証情報を作ります。相手側の操作は、選んだサンプルの説明に従ってください。ペアリングを許可する設定でも、指定外の機器を受け入れることはありません。

サンプルの鍵は RAM に保存されます。Pico の再起動後、相手側にだけ古い鍵が残り、再接続できないことがあります。その場合は相手側の Pico の登録だけを削除し、ペアリングし直してください。旧式の PIN ペアリングには対応していません。

<a id="flash"></a>
## 4. 選んだ UF2 を Pico W へ書き込む

**書き込みはボード上の既存プログラムを置き換えます。** 既存の用途があるボードなら、元へ戻す UF2 と手順を先に確保してください。このガイドでは自動書き込みコマンドは使いません。

1. 対象の Pico W を識別し、USB を外します。
2. ボードの **BOOTSEL** ボタンを押しながら USB を接続し、接続後にボタンを離します。
3. PC に `RPI-RP2` ドライブが現れたことを確認します。
4. ビルドした目的の `.uf2` を **一つだけ**、そのドライブへコピーします。
5. コピー後はドライブが外れ、新しいファームウェアで再起動します。USB コンソールを使う構成なら、シリアル機器として現れます。

無線を有効にした PCM 受信サンプルは、起動直後に無線の初期化を始めます。DeveloperSamples は `b`、メディア操作サンプルは `START` を送るまで待ちます。

WSL でビルドした場合、ファイルを Windows 側から開いて `RPI-RP2` へコピーする方法もあります。シリアル端末も Windows 側で開くか、USB の WSL 側への接続を別途用意します。ビルドのためだけに WSL へ USB を接続する必要はありません。

## 5. USB シリアルから操作する

Linux なら、接続前後の一覧やボードの識別情報を見て、対象のポートを確認します。他の機器のシリアルポートを開かないでください。

```sh
ls -l /dev/serial/by-id/
```

端末ソフトの例は `picocom` です。未導入なら `sudo apt install picocom` で用意できます。次のポート名は説明用なので、確認した実際の名前に置き換えます。

```sh
picocom -b 115200 /dev/serial/by-id/REPLACE_WITH_YOUR_PICO
```

USB CDC は仮想シリアルなので、ここでの 115200 は端末側の設定です。UART を使う場合の実際の通信速度 とは区別します。picocom の終了は通常 `Ctrl-A`、続けて `Ctrl-X` です。

**書き込んだサンプルに合うコマンドを使ってください。**

| ファームウェア | 起動と操作 |
|---|---|
| DeveloperSamplesの`sample_*` | `b`、次に`?`を1文字ずつ。下表の手順を使う |
| `avrcp_media_control_sample` | `STATUS` / `START` を大文字で入力し、Enter を押す。[操作手順](../examples/AvrcpMediaControlSample/README_jp.md#operator-workflow)へ |
| `a2dp_sink_pcm_consumer` | 起動直後に初期化を開始。Ready を待って `l` / `?`。[初回操作](../examples/A2dpSinkPcmConsumer/README_jp.md#first-use)へ |

### DeveloperSamples の操作

接続相手を設定した**DeveloperSamplesは、`b`を送るまで無線を起動しません**。文字は1文字ずつ送ります。起動時メッセージは端末を開く前に出て見えない場合があります。接続相手を設定済みなら `b` を一度送り、その後 `?` で状態を確認してください。接続相手のアドレスが空・不正なら `b` でも始まらないので再ビルドが必要です。

| 最初に使う例 | 操作の順序 | 確認するもの |
|---|---|---|
| `sample_a2dp_source` | `b` → `?` で power=2 → `c` → `?` | stream=4（Playing）と packets の増加。約10秒後に自然終了。`d` は手動切断 |
| `sample_spp_client` | `b` → `?` で runtime=2 → `c` → `?` で route phase=4 → `w` | 相手側で挨拶メッセージを受信。`d` で切断 |
| `sample_avrcp_target` | ペアリングを ON で構成 → `b` → Ready 表示 → `p` → 相手から接続・操作 → `?` | 操作 ID と押下・解放。`x` は AVRCP の終了で、再開始にはボードをリセット |

**A2DP のサンプルは無音です。** 48 kHz、符号付き16 bit、左右交互のステレオ PCM を約10秒送ります。音が出ないだけで故障とは判断せず、まず状態と パケット数を見ます。パケット数の増加だけでは、音質やスピーカーからの出力までは確認できません。

音源を替える場合は `SilenceSource.h` の `IAudioSource::read()` を自分のデータ供給に置き換えます。`samples` は左右を合わせた `int16_t` 値の個数で、ステレオ1組の数ではありません。音源と settings のサンプルレートを合わせます。自動再サンプリングはありません。

3機能併用の接続順や SPP の待ち受けは [サンプルの詳細手順](../examples/DeveloperSamples/README_jp.md)を参照してください。単体例の `c` をそのまま全例に当てはめないでください。

`d` は接続を切る操作で、必ずしも無線電源を落としません。SPP の待ち受け も残ります。サンプルの利用が終わったら、必要に応じてボードをリセットするか、電源を外します。これらの例に、PC 側が停止したとき必ず無線を切る独立したウォッチドッグ はありません。

## 6. UART を使う場合

DeveloperSamples を UART で操作する場合は、USB 版と別のビルド先を使います。接続相手は手順3で設定したものを使います。

```sh
: "${PEER_BT_ADDRESS:?Set the actual peer Classic Bluetooth address}"
cmake -S examples/DeveloperSamples -B .work/builds/samples-uart \
  -DSAMPLE_PEER="${PEER_BT_ADDRESS:?Set the actual Classic Bluetooth address}" -DSAMPLE_USB_CONSOLE=OFF \
  -DSAMPLE_ALLOW_PAIRING=OFF -DSAMPLE_CHANNEL_0=1 -DSAMPLE_CHANNEL_1=2
cmake --build .work/builds/samples-uart --target sample_a2dp_source --parallel 2
```

Pico W の UART0 は **GP0=TX、GP1=RX、115200 baud、8N1** です。アダプター側の RX を GP0、TX を GP1 へ接続し、GND を共通にします。配線は電源を外してから行います。

**3.3 V TTL 対応アダプターを使ってください。RS-232 や 5 V の信号を直結しないでください。** USB-UART アダプターからの電源供給を前提とした説明ではありません。

UART 出力は待ちが入るため、`?` を連打しないでください。USB 出力も混雑時はログを落とす設計なので、ログが完全に残る保証はありません。

<a id="integration"></a>
## 7. 自分の CMake プロジェクトへ組み込む

新しいアプリの配置から `CMakeLists.txt`、`main.cpp` まで、必要な手順は[はじめてのアプリ](getting_started_jp.md)にまとめています。Source の最小アプリと、Sink ＋ Controller のアプリを作れます。

このライブラリは `make install` や `find_package(PicoWBluetooth)` ではなく、CMake ヘルパーで組み込みます。**既存の Pico SDK アプリ**では、SDK の読み込み・初期化と実行ターゲットを残し、`pico_sdk_init()` の後にヘルパーを読み込んで、そのターゲットに適用してください。最後の引数は、アプリの `btstack_config.h` があるディレクトリです。SDK の場所ではありません。使用するプロファイルに合った設定ファイルを選びます。

Source を組み込むときの注意：

- `IAudioSource` と `A2dpSourcePlayer` はプログラムの終了まで生存させます。
- `begin()` の結果を確認し、メインループで `poll()` を続けます。
- 手動接続なら `settings.auto_connect=false` にします。既定値は自動接続です。
- 連続再生の既定値は `play_duration_us=0` です。開発者サンプルの10秒制限はサンプル側の設定です。
- 独立した `cyw43_arch_init()` や BTstack 初期化、旧プラグインのターゲットを足さないでください。

複数機能を組み合わせるときは、同じターゲットに次のヘルパーを追加できます。

| 機能 | include するファイル | target に適用する関数 |
|---|---|---|
| A2DP Source | `cmake/picow_a2dp_source.cmake` | `picow_target_a2dp_source(target config_directory)` |
| A2DP Sink | `cmake/picow_a2dp_sink.cmake` | `picow_target_a2dp_sink(target config_directory)` |
| AVRCP Controller | `cmake/picow_avrcp_controller.cmake` | `picow_target_avrcp_controller(target config_directory)` |
| SPP | `cmake/picow_spp.cmake` | `picow_target_spp(target config_directory)` |
| AVRCP Target | `cmake/picow_avrcp_target.cmake` | `picow_target_avrcp_target(target config_directory)` |

Sink ＋ Controller では、[メディア操作サンプルの構成](../examples/AvrcpMediaControlSample/CMakeLists.txt)を参考に、同じ実行ファイルへ両方のヘルパーを適用します。Sink と Controller のオブジェクトを一つずつ作り、同じメインループから `poll()` します。BTstack をそれぞれで初期化する必要はありません。この組み合わせは音声受信と操作送信であり、音声の双方向通信ではありません。

Source / SPP / Target を組み合わせる場合は、[DeveloperSamples の共用設定](../examples/DeveloperSamples/btstack_config.h)から始めます。[SPP の資源・HCI 設定](spp_sdk_integration_jp.md)も確認してください。各機能の `poll()` を毎周呼び、サービス検索が重ならないようにします。接続順は[3機能サンプル](../examples/DeveloperSamples/a2dp_spp_avrcp.cpp)が参考になります。

<a id="compile-only"></a>
## 8. 任意：実機を使わずビルドを確認する

ここは、動かす機器がまだない場合や、コードを変更した後の確認用です。実機向けビルドの前に実行する必要はありません。DeveloperSamples は接続相手を空にしてビルドできます。Sink とメディア操作サンプルにも、**同じサンプルの無線 OFF 設定**があります。この構成は USB と無線の初期化前に終了するため、コンソールは出ません。必要な場合は [PCM のビルド確認](../examples/A2dpSinkPcmConsumer/README_jp.md#compile-only)・[メディア操作サンプルのビルド確認](../examples/AvrcpMediaControlSample/README_jp.md#compile-only)を参照してください。

次のスクリプトは、3種類のサンプル用ホストテストと全7例の ARM ビルドを行います。初回は時間がかかります。書き込み、無線起動、ペアリングは行いません。

```sh
SAMPLE_PEER="" SAMPLE_ALLOW_PAIRING=OFF SAMPLE_USB_CONSOLE=ON \
  examples/DeveloperSamples/check.sh .work/builds/samples-check
```

`PICO_SDK_PATH` は手順2で export したものを使います。ホストテストは PC 用コンパイラの `c++`、ARM ビルドは Pico 用コンパイラを使う別工程です。生成 UF2 は `.work/builds/samples-check/arm/` に入ります。

これらはホスト側の動作とコンパイルを確認するものです。実際の相手への接続、音質、電波環境での安定性まで証明するものではありません。

メディア操作サンプルと PCM 受信サンプルにもホストテストがあります。完全なチェックアウトから `bash tests/host/run_avrcp_media_control_sample_tests.sh` と `bash tests/host/run_sink_pcm_sample_tests.sh` を実行します。

## 詳細：Sink の設定検証と START 判定

`picow_target_a2dp_sink` は、受信する SBC 設定を検証する処理を自動で組み込みます。ホスト側の Python 3 と対応版の Pico SDK が必要です。SDK 内の対象ソースが未知の版なら、CMake の構成時にエラーになります。インストール済み SDK は変更せず、Source 専用ターゲットの処理も変えません。受信側の C++ ファイルを手動で追加するのではなく、ヘルパーを使ってください。詳しくは [SBC 設定検証の仕様](sink_raw_configuration_contract.md)を参照してください。

同一相手との接続を扱う実験用の same-peer オーバーレイとは、別ターゲットであっても同じ CMake 構成に混在できません。別の構成に分けてください。また、この検証は SBC の設定を対象とし、シグナリング全般の分割処理を扱うものではありません。

相手からの START 要求をアプリで判断する場合は、`picow_target_a2dp_sink(app ...)` の後に `picow_enable_sink_start_decisions(app)` を呼び、`Settings::require_start_decision=true` を設定します。SDK の明示 START マクロだけを有効にしても使えません。[専用サンプル](../examples/A2dpSinkStartDecisionSample/README_jp.md)と [API・応答タイミング](sink_start_decisions_jp.md)を参照してください。専用サンプルでは機能と無線の設定が分かれており、既定のビルドでは無線を起動しません。

<a id="troubleshooting"></a>
## 9. つまずいたとき

| 症状 | 確認すること |
|---|---|
| `pico_sdk_import.cmake` が見つからない | `echo "$PICO_SDK_PATH"` と手順2の `test -f` を確認。SDK のルートを指定し直す |
| `arm-none-eabi-gcc` / `g++` がない | ARM 向けコンパイラを導入し、新しい端末で `--version` を確認。PC 用 `gcc` だけでは不足 |
| `<algorithm>` や `libstdc++` が見つからない | ARM 向け C++ 標準ライブラリがあるか確認。Ubuntu / Debian では `libstdc++-arm-none-eabi-newlib` |
| BTstack / CYW43 / TinyUSB がない | SDK 内で `git submodule update --init --recursive` を行う。ライブラリ側への別のヘッダーのコピーでは直さない |
| picotool の取得・ビルドに失敗する | ネットワーク、Git、ホスト側の C/C++ コンパイラと libusb 開発用パッケージを確認。対応版を入れた場合は CMake パッケージが見えるかも確認 |
| picotool の版が不一致 | エラーに示された要求版を確認。この検証 SDK は 2.1.1 を要求する。必要なら新しいビルド先で `-DPICOTOOL_FORCE_FETCH_FROM_GIT=ON` を指定して対応版を取得する |
| `.elf` はあるが `.uf2` がない | ビルドが最後まで成功したか確認。`PICO_NO_PICOTOOL` / `PICO_NO_UF2` を有効にすると UF2 は作られない |
| 古い SDK / 別ディレクトリを参照する | CMake キャッシュに絶対パスが残っている可能性がある。既存生成物を消さず、新しい `-B` の保存先で構成する |
| 接続相手やチャンネルの設定エラー | [アドレス確認手順](#peer-address)を参照。無線 ON の Sink / メディア操作サンプルには全ゼロ以外のアドレスが必要。SPP のチャンネルは 1〜30 の異なる番号を使う |
| コンパイルが `Killed` で止まる | メモリ不足の可能性がある。`--parallel 1` で再ビルドし、OS 側の記録や空き容量も確認 |
| シリアルポートや起動表示がない | 書き込んだ UF2、データ通信対応ケーブル、ポート、USB / UART の設定を確認。Sink / メディア操作サンプルを無線 OFF でビルドした場合はコンソールも出ない。起動ログを見逃しただけなら、各サンプルの状態表示コマンドで確認できる |
| 送信元の Bluetooth 一覧に Pico が出ない | メディア操作サンプルはペアリング ON の構成で `START` → READY → `PAIR`。PCM は `SINK_PCM_DISCOVERABLE_LISTEN=ON` と `l` が必要で、検出できる時間は60秒。通常の `listen()` だけでは一覧に出ない |
| シリアルが `Permission denied` | 対象ポートの所有グループと自分の権限を確認。OS の手順に沿って許可を設定し、全デバイスを一律に緩い権限へ変えない |
| 接続したのに音が出ない | Source サンプルは無音を送るので `stream=4` とパケット数を見る。Sink / メディア操作サンプルもスピーカー出力を持たない。受信状態と復号フレーム数を確認し、音声出力は別途実装する |
| メディア操作サンプルがコマンドを拒否する | `START` 後に READY になっているか、接続状態や処理中の要求を確認。コマンドは1行ずつ送り、応答と相手のプレーヤーの動作を確認してから次へ進む |
| `begin` や `connect` は成功表示なのにつながらない | 要求受理と接続完了は別。状態やエラー、相手の対応プロファイルとサービス、ペアリング条件を確認 |
| 再起動後につながらない | 鍵が RAM のみであることと相手側の古い登録を確認。必要なら対象の登録だけ解除し再ペアリング |
| `c` を押しても再開しない | 起動未完了、接続中、または終端エラーの可能性。状態を読み、リセットが必要な場合に無限再試行しない |

A2DP を含むリンクでは、検証したツールチェーンで `_getentropy is not implemented` という newlib の警告が出ます。既知の警告を残した状態で UF2 生成を確認していますが、ほかの警告やエラーは個別に確認してください。

<a id="verified"></a>
## 確認済みのビルド環境

- Linux: Raspberry Pi 5 / Ubuntu 24.04 系
- 対象ボード: Pico W / RP2040
- Pico SDK: `9a4113fbbae65ee82d8cd6537963bc3d3b14bcca`（2.1.1系）
- BTstack submodule: `501e6d2b86e6c92bfb9c390bcf55709938e25ac1`
- CMake 3.28.3、Arm GCC 13.2.1、picotool 2.1.1

自作アプリの手順は、Source と Sink ＋ Controller をそれぞれ独立したディレクトリに配置し、ARM ビルドと UF2 生成を確認しています。Windows / WSL での環境構築と、この自作アプリ自体の実機動作は未確認です。検証内容と制限は[検証範囲](validation_jp.md)にあります。
