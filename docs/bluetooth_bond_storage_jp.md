# Bluetooth Classic のボンド保存

[English](bluetooth_bond_storage.md)

永続化は opt-in です。[保護された snapshot TLV の契約](bluetooth_committed_tlv_jp.md)と provisioning／read-only 方針を先に確認してください。既定のリンクキー保存は RAM です。旧 SDK の試験履歴は[検証範囲](validation_jp.md)を参照してください。

## Runtime の契約

`BluetoothRuntime::setLinkKeyDatabase(const btstack_link_key_db_t *database)`
は A2DP Source、A2DP Sink、SPP、AVRCP が共有する **Bluetooth Classic** の
リンクキーデータベースを選択します。BLE のデバイス DB は設定しません。

- 各 profile の `begin()`、成功する runtime `claim()`、`initialize()` より前に
  呼びます。全 profile が同じ無線と DB を使用します。
- 呼ばない場合、または `nullptr` を指定した場合は従来の BTstack RAM backend
  です。MCU リセットをまたいだ永続保存はしません。
- 最初の claim 成功または初期化開始までは、選択を変更できます。不正な指定は
  `false` を返し、以前の選択を維持します。この設定時に backend は呼びません。
- 初期化中、初期化失敗後、および初期化後の選択変更は禁止です。無線の OFF/ON
  でも解除しません。別の所有者が CYW43 を初期化済みの場合も拒否します。
- backend はアプリ所有です。関数テーブルは変更せず、テーブルと内部状態を
  runtime の全期間で有効に保ちます。runtime は保存領域の確保、解放、消去、
  移行、列挙をしません。
- `open`、`set_local_bd_addr`、`close`、`get_link_key`、`put_link_key`、
  `delete_link_key` は全て必要です。固定 controller でも
  `set_local_bd_addr` に空実装が必要です。SDK が null 確認なしで呼ぶためです。
- iterator の 3 関数は全て指定するか、全て null にします。省略時は BTstack の
  iterator 初期化が利用不可を返し、`gap_delete_all_link_keys()` による全件列挙・
  削除はできません。
- この設定が確認するのは関数テーブルの構成です。媒体の準備、容量、整合性、
  暗号化、永続化成功を保証しません。BTstack の `open` と `put_link_key` は
  `void` 戻り値なので、runtime 初期化成功は永続保存の成功通知ではありません。
- 通常の単一 main loop 所有モデルを守ってください。初期化後に
  `hci_set_link_key_db()` を直接使って DB を差し替えないでください。

アプリが backend を定義する場合の開始手順です。

```cpp
#include "BluetoothRuntime.h"

extern const btstack_link_key_db_t application_link_key_db;

bool configure_bonds_before_starting_profiles() {
    return BluetoothRuntime::instance().setLinkKeyDatabase(
        &application_link_key_db);
}
```

戻り値を確認してから profile を開始します。既存 example が自動的に永続保存へ
切り替わることはありません。

## アプリ提供 TLV と初期化順序

BTstack の `btstack_link_key_db_tlv_get_instance(tlv, context)` を使用できます。
アプリ側で長期間有効な `btstack_tlv_t` と context を用意し、`get_tag`、
`store_tag`、`delete_tag` を実装します。アプリの build 設定で適切な
`NVM_NUM_LINK_KEYS` を定義してください。上流実装の `BTL` タグ空間と有限容量の
追い出し規則を使うため、他のレコードとの衝突を避けて容量を設計します。
保存形式は SDK の native 構造体なので、firmware/ABI 変更時には形式互換性または
移行方針が必要です。読み出すレコードは事前検証してください。現 SDK は
`get_tag` の非ゼロ結果を使用し、リンクキーレコードの正確なサイズを検証しません。

**初期化前に上流 TLV singleton を設定し、そのポインタを setter に渡すだけでは、
その設定は Pico 初期化で維持されません。** 調査した SDK では
`cyw43_arch_init()` → `btstack_cyw43_init()` → `setup_tlv()` が、ライブラリの
backend 適用より先に同じ singleton を設定します。

代わりに、アプリ所有の転送用 `btstack_link_key_db_t` を用意します。

1. `open()` から、CYW43 初期化 **後に**
   `btstack_link_key_db_tlv_get_instance(application_tlv, application_context)`
   を呼び、取得した delegate を open します。
2. 残りの callback は、その delegate に転送します。
3. TLV テーブル、context、転送 wrapper、保存領域を runtime 全期間で有効に
   保ちます。他の component に同じ singleton を再設定させないでください。
4. 媒体の準備と検証は、アプリ側の platform に合った手順で行います。DB callback
   から別 profile を開始したり、runtime を再帰的に初期化したりしないでください。

転送の全体例は
`test_selection.cpp` で検証しています。
RAM 上の疑似 TLV と合成レコードのみを使った接続方法の例であり、実用 flash driver
ではありません。

## Pico SDK の flash に関する注意

**RAM または独自 backend を選んでも、SDK 起動時の flash アクセスは止まりません。**
調査した SDK はこのライブラリの選択適用前に独自 flash-bank TLV を初期化します。
その際、既存 flash の読み出し、header 書き込み、未初期化 bank の消去、障害回復・
移行を行う可能性があります。`powerOff()` でそれらを取り消したり、保存済み bond
を消したりはしません。

SDK の既定は flash 末尾の 2 sector で、`PICO_FLASH_BANK_STORAGE_OFFSET` と
`PICO_FLASH_BANK_TOTAL_SIZE` で設定します。ライブラリが位置を決めたり、領域を
予約・変更したりすることはありません。実際の SDK version を確認し、firmware、
filesystem、OTA slot、他の flash 利用領域と重ならない配置をアプリで保証して
ください。**一切の flash アクセスを避ける** 必要がある場合は、アプリ管理の
SDK/port 初期化経路が必要です。この setter だけでは実現できません。

永続保存するアプリでは、消去・リセットの承認、書き込み失敗通知、電源断回復、
寿命管理、割り込み・別 core と競合しない flash 実行も設計してください。
raw TLV と本 wrapper は暗号化やアクセス制御を提供しません。リンクキーは認証情報
です。キーの表示、packet/log dump への出力、実際の保存イメージの commit は避けて
ください。今回の選択機能はキーのログを追加しません。アプリ既存の HCI dump 設定も
別途確認してください。

調査した SDK 内のパス:

- `src/rp2_common/pico_cyw43_driver/btstack_cyw43.c`
- `src/rp2_common/pico_btstack/btstack_flash_bank.c`
- `src/rp2_common/pico_btstack/include/pico/btstack_flash_bank.h`
- `lib/btstack/platform/embedded/btstack_tlv_flash_bank.c`
- `lib/btstack/src/classic/btstack_link_key_db{,_memory,_tlv}.h` と対応実装
- `lib/btstack/src/hci.c`

## 検証と未保証範囲

同梱検査は[ビルドガイド](build_guide_jp.md#compile-only)を参照してください。

host test は実 SDK 宣言と上流 TLV 実装を使用し、ASan/UBSan を有効にします。
RAM 既定・復帰、callback 検証、iterator 省略、全 profile の claim 後ロック、初期化
失敗・外部所有、open 中の再入防止、無線 OFF/ON 後の選択保持、初期化後 TLV 再設定、
合成レコードの保存・取得・削除・列挙、同じ疑似媒体を保った backend 再構成を確認
します。ARM test は既存 CMake compilation database の Pico/BTstack 設定を使い、
実 runtime を compile します。

これらは実機書き込み、実際の bond の読み出し・保存、永続媒体検証、電源断安全性、
MCU 再起動後の再接続を確認するものではありません。アプリと実機での検証が必要です。

## 明示的に有効化する SDK 永続化バックエンド

`PicoPersistentBonds` と `cmake/picow_persistent_bonds.cmake` を追加しました。
既定は引き続き RAM です。組込み例で
専用ソースを追加し、全プロファイルの `begin()` より前に
`setLinkKeyDatabase(PicoPersistentBonds::database())` の成功を確認します。
Source、Sink、SPP、AVRCP に共通の選択であり、各プロファイルの実機動作保証ではありません。

`open()` は SDK 初期化済みの共有 TLV を取得し、HCI からローカルアドレスが
通知されるまで ready にしません。別のフラッシュ領域や上流シングルトンの
再設定は行いません。タグ `0x50424b00`〜`0x50424b07` を予約します。
他の利用者はこの範囲を使用できません。既存 BTL/BTD データとは別形式で、
自動取込みや移行はありません。同じ物理バンクの容量は共有します。

形式は 48 バイト固定で、バージョン、64 ビット連番、ローカル／相手アドレス、
キー、種別、スロット番号、予約バイト、CRC-32 を明示的に直列化します。
コピー前に実際の長さを問い合わせます。CRC は偶発的なレコード破損の検出用で、
暗号化、認証、巻戻し防止や SDK ジャーナルヘッダーの保護ではありません。
保存数は 8 件。空きは最小スロットから使用し、満杯なら最も古く保存した正常な
レコードを置換します。不明形式、破損、別コントローラーのレコードがあれば、
put 全体を拒否し自動上書きしません。通常の削除／全件削除は現在のコントローラーの
正常レコードだけを対象とし、安全な物理消去ではありません。

`status().ready` は接続先 TLV とローカルアドレスの準備完了で、新しい保護経路では
provider の READY も毎回確認します。未知・混在状態は通常認証に使用しません。媒体の健全性や
永続性ではありません。errors は明示的に消すまで残り、stores/deletes/evictions は
現在の読戻しで確認した操作数です。再起動後の保存保証ではありません。
`clearErrors()` はデータ修復や readiness 復旧をしません。BTstack の void 戻り値では
保存失敗をペアリング結果に返せないため、アプリのメインループで状態を確認してください。
open 中のローカルアドレス変更は失敗扱いです。close/open 後は iterator を作り直します。

CMake の有効化には、アプリが確認済みの offset と二バンク合計サイズが必須です。
セクター境界、容量、設定値をコンパイル時に検査し、Release を含めリンク時に
ファームウェア末尾との重なりを拒否します。ファイルシステム、OTA、ブートローダー、
他の書込み主体との領域予約はアプリ側で別途確認が必要です。安全な実機アドレスを
この例が選ぶことはありません。保護対象では SDK 初期化時に open より先に読取りだけを行います。
このヘルパーを使わない SDK 経路では、RAM を選んでも起動時の書込み・消去リスクが残ります。

旧 SDK の未保護ジャーナルには、モデル上の書込み中断でレコードを失う事例があります。現在の保護形式と混同しないでください。[検証範囲](validation_jp.md) に当時の試験と制約を保持しています。実機の電源断、摩耗、再起動後の再接続は未保証です。

CMake ヘルパーは最終 executable ターゲット専用です。static/interface ライブラリ
への設定は拒否します。`pico_flash_bank_get_storage_offset_func` による動的な領域指定も
拒否します。別途コンパイルされた SDK フラッシュ実装やカスタムポートは、この検査の
保証対象ではなく、独立した領域設計の確認が必要です。
