# AVRCP のメインループ実装例

[English](README.md)

この例は組み込み方を示すもので、プレーヤーでも実機受入試験でもありません。
全 AVRCP 機能の実機動作を保証しません。通常のビルドは、所有オブジェクトの生成、
`begin()`、無線起動を行わず `main()` から戻ります。無線用コードも C++ の型検査は
受けますが、リンク時に未使用コードとして除去される場合があります。
音声、メディア一覧、画像デコード、ペアリング UI、永続ボンド保存、自動的な Cover Art 要求は未実装です。

## 単独ビルド

実際の Pico SDK がある環境で、リポジトリのルートから実行します。

```sh
PICO_SDK_PATH=/path/to/pico-sdk cmake \
  -S examples/AvrcpMainLoopSample \
  -B build/build-avrcp-mainloop-sample \
  -DAVRCP_SAMPLE_ENABLE_RADIO=OFF
cmake --build build/build-avrcp-mainloop-sample -j2
```

出力はビルド先の `avrcp_mainloop_sample.elf`、`.bin`、`.uf2` です。
独立した CMake プロジェクトから、このリポジトリの Browsing/Cover Art ヘルパーを使い、
Controller/Target と共有ランタイム、SDK オーバーレイを取り込みます。
A2DP/SPP は初期化しません。`btstack_config.h` は既存の
`../AvrcpCoverArtCompileProbe/btstack_config.h` を再利用します。そのため、この
ディレクトリだけでなくリポジトリが必要です。生成したオーバーレイはビルド先に置き、
インストール済み SDK を書き換えません。

## main.cpp の実行順序

1. 有効化した経路でのみ static な `Application` を生成します。Target、Controller、
   Controller/Target 共用の Browsing、Cover Art Client を一つずつ所有し、同じ許可 peer
   を使います。失敗・stop 後の再生成や `begin()` の再実行、旧 AVRCP プラグインとの混用はしません。
2. コールバックを先に登録します。Target は操作要求を拒否し、再生情報は不明とします。
   `target.begin()` 自体が無線を起動し得ます。`connect()` まで無線が停止しているわけではありません。
3. メインループで `HCI_STATE_WORKING` を待ち、signalling 接続を一度だけ要求します。
   戻り値 `0` はローカル受付だけです。busy/error は記録し、自動再試行や成功の推測はしません。
4. Controller の `Connected` をコピーしてキューに入れ、メインループで PlayStatus を
   要求します。その query が生成した event を消費し、完全な signalling binding の生存を
   再確認してから Browsing 接続を一度要求します。生存中の binding と一致するコピー済み
   Browsing `Connected` 後に player list を一度要求します。
   binding は CID だけでなく peer、epoch、generation を保持します。切断後の古い binding
   が失敗することを前提にします。
5. Browsing 要求では `RequestToken` をコピーし、メインループから
   `reject(token, 0x00)`（INVALID_COMMAND）を呼びます。メディア一覧は実装していません。
   切断・期限切れの token を再試行しません。実際に応答を作る場合は必要な要求バイト列も
   コールバック内で所有バッファへコピーしてください。
6. キューは単一スレッド用で 8 件、各 payload は最大 512 バイトです。コールバックは
   コピー、フラグ更新、同期判断だけを行い、プロファイルの変更 API、ログ出力、I/O を呼びません。
   借用ポインタは残しません。キューへの追加は意図したコールバック処理です。
   満杯・サイズ超過では切り詰めず、エラーフラグを立て、メインループで停止します。
   Cover Art data handler はさらに `false` を返して遅延 cancel を要求します。pause ではありません。
   この小さな例は正常な大きい chunk/list でも停止し得ます。RAM を測定して上限を調整するか、
   有界なストリーミング sink を実装してください。別 core、IRQ、background poller を追加するには
   キューの同期方式を変更する必要があります。
7. `use_deadlines` で時刻付き poll と絶対期限を選びます。時刻は単調増加のミリ秒を 2^32 周期で
   渡し、poll 間隔を 2^31 ms 未満に保ちます。受付後の最初の時刻付き poll から計時し、進捗で延長
   しません。引数なし `poll()` は期限を進めません。プロファイル期限を無効にしても、アプリの
   60 秒制限は残ります。
8. 終了時に Cover Art の接続待ち／接続中 cancel と、共有 signalling の接続待ち cancel を
   示しています。戻り値はローカル結果であり、相手の応答や後処理完了の証明ではありません。
   `stop()` は終端操作ですが共有無線を止めません。この単独アプリだけは最後に runtime の
   `powerOff()` を呼びます。他プロファイルと共有する製品に、その終了方針をそのまま移さないでください。

## ローカル結果と相手の成功を区別する

- Controller query の `0` は受付だけです。コピーした SDK パケットの subevent、ctype、長さを
  検査してから相手の応答を解釈します。この例はサイズを表示するだけで PlayStatus 成功と判定しません。
- Browsing `Response` の `Event.status == 0` は相手の AVRCP 成功ではありません。
  コピーした payload の先頭バイトが `0x04` で初めて相手の成功です。他の値は相手の失敗です。
  `TargetResponseSent` も送信結果であり、相手アプリが操作した証明ではありません。
- Cover Art の chunk は途中データです。binding/request と offset を照合し、成功 status の
  `Complete` を待つ必要があります。Error、cancel、上限超過、sink 拒否なら未完成データを
  成果として扱いません。この例には下記の有界 RAM assembler がありますが、画像を保存・表示・デコードしません。

## 無線試験前に必要な組み込み

`AVRCP_SAMPLE_ENABLE_RADIO=ON` は実際の無線起動を有効にします。許可された試験 peer の
非ゼロアドレスを `AVRCP_SAMPLE_PEER=AA:BB:CC:DD:EE:FF` の形式で明示する必要があります。
このアドレスは書式例であり実在 peer の指定ではありません。有効化した firmware の書き込み・
書き込む前に実際の接続先アドレスを設定してください。CMake の指定だけでは実機受入完了になりません。

自動ペアリング確認、鍵取り込み、ペアリング UI、discoverable 設定変更は行いません。
認証接続を期待する前に、レビュー済みのペアリング／ボンド方針と相手側の準備が必要です。
標準の RAM-only database は再起動で鍵を失います。connect のローカル成功後でも非同期の接続失敗が
あり得るため、製品では status と終端イベントも監視してください。

Cover Art は **Client のみ**です。下記の明示入力による手順で接続と選択した GET を
一度実行できます。firmware に実用 handle は埋め込まず、無線を有効にするだけでは GET
しません。相手の対応状況と現在の metadata に含まれる本物の 7 桁 handle を先に取得します。

プレーヤーに組み込む際は、実際の状態・metadata と、アプリの受付能力に基づく有界な同期判断を
追加してください。Target の同期判断は Browsing token のように後回しにできません。
実行を引き受けられる場合にだけ Accepted を返します。音量適用、再生、曲選択、設定変更は
AVRCP アダプタの自動動作ではなくアプリの責任です。

ビルド確認はソースとビルドの互換性確認に限ります。実機ペアリング、相手応答、cancel のタイミング、
キュー負荷、他プロファイルとの共存には別の試験が必要で、この例はその結果を提供しません。

## オフラインcallback queue試験

`OwnedRecordQueue.h` はこの例のSDK非依存・単一loop用queueです。8件のrecordへ各最大512 payload bytesをコピーします。空入力は許可し、非空null・上限超過・満杯は待機recordを変更せず拒否します。failure flagは一度立つと維持され、main loopは欠落後に黙って続行せず停止します。metadataのコピーでpointerが所有領域になるわけではありません。この例のようにcallback側で借用pointer欄を消してからqueueへ渡す必要があります。

`bash tests/host/run_avrcp_sample_queue_tests.sh` で実ヘッダーをASan/UBSan付きで試験します。借用元の変更、空/null/上限超過、容量、FIFO順序、failure維持、参照queueとの40,000 stepを検証します。アプリ補助部の試験であり、adapter/SDK protocol review、callback統合の証明、実機受入ではありません。


## 明示入力による一回限りの Cover Art Client

`CoverArtFlow.h` は `main.cpp` が実際に使う SDK 非依存のアプリ手順です。
未呼出し hook や symbol の強制参照 probe ではありません。このフローを使うには、
`AVRCP_SAMPLE_ENABLE_RADIO=ON` と `AVRCP_SAMPLE_ENABLE_ART_INPUT=ON`、許可された
`AVRCP_SAMPLE_PEER` を明示します。両 switch の既定値は OFF です。radio OFF で input ON
は configure 時に拒否します。ペアリング方針、metadata 探索、永続 bond は追加しません。

`Cover Art ready` の表示後、USB stdio console から改行付き command を送ります。
placeholder を、その peer の現在の metadata から別途取得した本物の handle に置き換えます。

```text
art thumbnail <REAL_SEVEN_DIGIT_HANDLE>
art image <REAL_SEVEN_DIGIT_HANDLE>
art properties <REAL_SEVEN_DIGIT_HANDLE>
cancel
```

GET は三つ全部ではなく **一つだけ** 選びます。大文字小文字を区別し、区切りは半角 space
一つです。handle は ASCII 数字 7 桁ちょうどです。構文検査だけでは出所、鮮度、相手の対応、
内容の妥当性を証明できません。`image` は descriptor なしで元画像を要求します。
`properties` は未解釈の byte 列であり XML の解析結果ではありません。架空の player/database、
decode、画像 server、ファイル保存、upload はありません。

parser は 32 byte の行を所有し、長すぎる行・不正な行は全体を捨て、次の改行から再開します。
CR は LF の直前だけ許可します。途中・連続 CR、他の制御文字、余分な space、不正な桁数を
文字のつなぎ直しなしに拒否します。編集用 terminal ではなく backspace も不正です。
main loop は一周最大 16 文字を非 blocking で読みます。USB stdout timeout はゼロなので、
遅い／未接続の console の空き待ちで poll を止めません。その代わりログが欠落し得ます。
実機上の時間・転送性能は未測定です。

### 手順と session の所有

1. 最初のコピー済み Controller `Connected` を、現在の完全な live binding と照合します。
   PlayStatus query を一度要求し、それが生成した event を消費してから、一度だけ Browsing
   接続を許可します。その直前にも session と queue 欠落を再確認し、ローカル失敗を再試行しません。
2. コピー済み Browsing `Connected` の peer、ACL epoch、generation、CID を live binding と
   照合します。Browsing 完了を待つのは shared SDP startup を直列化する **この複合 sample の方針**
   であり、Cover Art protocol 固有の必須条件ではありません。この例では Browsing 接続に成功
   しない peer は Cover Art prompt に進めません。他の SDP 利用者による busy もあり得ますが、
   むやみに再試行しません。
3. Ready、正常で空の queue、既存 Cover Art binding なしの場合だけ command を受け付けます。
   最初の文字を消費した時点の Ready を記録し、行末でも確認します。重複入力で所有 command を
   上書きしません。最初の完全な signalling session を生存期間中固定し、入力前でも切断、
   epoch/generation/CID の変更、古い event で終端失敗にします。途中の入力や queue 済み intent
   を再接続後の session に付け替えません。ただし host/USB backlog がいつ入力されたかは
   証明できず、操作者が現在の metadata に基づく新しい入力を渡す必要があります。
4. main loop から `cover.connect(peer)` を一回呼びます。戻り値ゼロは Connected ではなく受付です。
   live binding が見えるだけでは GET せず、一致するコピー済み Cover Art `Connected` を先に
   消費します。peer、ACL epoch、Cover generation/CID、signalling CID を live state と固定した
   signalling session に照合し、残りの queue event を消費します。
5. 所有する 16 KiB の `CoverArtObject` を **GET より前に arm** し、選んだ `thumbnail`、`image`、
   `properties` を一回呼びます。GET のゼロは受付であり **request ID ではありません**。
   他の場所から GET せず、reset／再接続／再試行もしません。queue が空でも別 owner の古い
   GET がない証明にはならないため、別 command owner を追加しないでください。
6. 最初の非空・offset ゼロの Data から request ID を取得します。以後の Data と成功 Complete は
   全 binding 欄、operation、request が一致し、offset は受信済み byte 数と等しい必要があります。
   Complete は status ゼロかつ count 完全一致が必要です。request ゼロ、空 chunk/object、非空 null、
   上限超過、識別子不一致、順序不正／重複、Data なしの Complete を失敗にします。成功 Complete
   まで byte 列を公開せず、公開後も画像/XML の内容は未検証です。

### 上限、失敗、cancel

object 用 16 KiB と、8 件 × 最大 512 payload byte の queue、および owner/record metadata は
別領域です。adapter の object 上限も 16 KiB です。正常な大きい object/chunk でも拒否され得ます。
callback は有界 record のコピー、借用 pointer の消去、戻り値の判断だけを行います。
profile の変更 API は呼ばず、組立・connect・GET・cancel を main loop が担当します。

ローカル拒否、live session の消失／変更、queue 済み Error／Disconnected、不正転送、queue 欠落、
cancel／stop で領域をゼロ化して破棄します。保守的な方針として、完成済み byte も後の error／切断で
無効化します。view は変更までしか有効でなく、失敗後の遅延 event で復活しません。
失敗は終端であり、重複 command から二回目を開始しません。

flow 自身はローカル要求から connect／Connected 消費に絶対 15 秒、GET に絶対 30 秒の予算を
設けます。profile 自身の期限も有効で、Data で延長しません。uint32 の単調 ms 差分で wraparound に
対応し、poll 間隔は 2^31 ms 未満とします。別途アプリ全体の 60 秒制限で先に終わる場合もあります。
期限時点で queue に残っている Complete は保守的に拒否します。

`cancel` は Ready 前でも受け付け、flow を終端にします。Cover Art 接続要求後は、ローカル拒否や
cancel 受付失敗も含め `cancelPeer` の明示要求は最大一回です。結果は記録しますが相手側の解放完了
とはみなしません。重複 cancel／stop／event でその要求を再試行しません。別の `cover.stop()` は facade
本来の終端時の遅延 cleanup を保持します。cancel 受付は解放の保証ではありません。単独アプリとして
終了時に owner 停止と無線 OFF を行う方針は従来どおりで、A2DP/SPP 共有アプリへそのまま移せません。

### 対象を絞ったオフライン検証

`bash tests/host/run_avrcp_sample_art_flow_tests.sh` で **実際の helper と 16 KiB object** を
ASan/UBSan 付きで試験します。三つの GET 選択、入力／byte の所有、parser／途中入力／CRLF、
遅延 Browsing startup、callback を模したコピー済み event 順序、完了成功、全 binding 欄、古い
session/event、受付失敗、重複、queue 欠落、cancel、stop、期限境界と wraparound が対象です。
IO は model であり SDK/OBEX peer ではありません。既存 queue/object helper 試験は別の回帰試験です。

この例の実機実行は **NOT_RUN** です。ペアリング、相手応答、USB 入力、cancel のタイミング、共存動作は未検証です。[検証範囲](../../docs/validation_jp.md)を参照してください。

## 困ったとき

既定ビルドがすぐ終了するのは正常です。有効構成で `Cover Art ready` が出ない場合は signalling と Browsing の接続結果を確認します。この例は Browsing 非対応 peer では ready になりません。入力拒否なら両 CMake option、7桁 handle、空白と改行形式を確認してください。失敗・cancel 後の再入力では再開しません。画像を表示・保存する機能はありません。
