# SPP 制御メインループサンプル

[English](README.md)

現在の `spp::Client` 公開 API を使い、シリアルポート制御、受信状態の観測、サービス登録解除、接続先の順次切替、任意の RFCOMM ERTM を示します。実機試験用プローブや、到達保証付きのコマンドプロトコルではありません。

## 安全な初期設定

`SPP_SAMPLE_ENABLE_RADIO`、`SPP_SAMPLE_ENABLE_ERTM` はどちらも既定で `OFF` です。既定の `main()` は stdio 初期化、クライアント生成、`configureErtm`、`begin`、`poll`、Bluetooth 操作より前に return します。ビルドはファイルを生成するだけです。無線有効版を書き込む前に、実際の接続先と操作手順を確認してください。

`main.cpp` の分岐は通常の `if` なので、既定ビルドでも実際の使用経路を C++ として型検査します。ただし、最適化で到達不能コードが除去された構成だけでは十分ではありません。有効経路の別ビルドでリンクまで確認します。

## ファイルと実行順序

- `SppControlWorkflow.h`: Client と同じインターフェースを注入できる、アプリケーション処理順序の全体。製品コードの SPP 状態型を使いますが、Pico/BTstack ヘッダーは含まず、注入先以外の副作用を持ちません。
- `main.cpp`: プロセス寿命のクライアント、ERTM バッファ、単一ポーリングループ、コピー済み診断値の出力。
- `btstack_config.h`: スタンドアロン用途の固定リソース設定。ERTM 有効時は必要な二つの定義を同時に有効化します。
- `CMakeLists.txt`: 検証済みトランスポート overlay を含む `picow_target_spp` を使用。SDK の元ソースは編集しません。

有効経路は次の順序です。

1. ERTM を使う場合、`begin` **より前に一度だけ**、呼び出し側が所有するバッファを設定します。
2. `begin` をキューに入れ、`Runtime::Ready` まで poll。listener を登録し、`Listening` を待ちます。
3. 最初の peer の既知の RFCOMM チャネルへ route 0 を接続します。探索や候補選択ポリシーは含みません。新しい世代の `Open` を確認して現在の handle を取得します。
4. `queryPort`、`configurePort`（115200、8 data bits、1 stop bit、parity なし）、`sendLineStatus(0)`、`sendModemStatus(0x0c)` を一件ずつ送ります。最後の flags は RTC/RTR で、wire EA bit は SDK が付けます。
5. listener を解除して `Disabled` を確認します。`unlisten` は既存 route を切断しません。その後、受け入れた incoming route も含め、全 SPP route を閉じます。
6. 拡張アプリで SDP 問い合わせが残っていればキャンセルし、`query.busy == false` を待ちます。`Cancelled` だけでは不十分です。全 route の `Closed` と `ertmStatus().active == 0` も待ちます。
7. `selectPeer(next_peer)` を呼びます。facade が非公開の待機イベントと SDK binding も確認します。false は未投入なので、次の poll 後に制限時間内で再試行できます。true でも完了ではなく、poll 後の `snapshot().config.peer == next_peer` を確認します。次の peer への接続は行いません。

workflow は facade を単独で所有します。callback、別 thread/core、別ループから競合する変更操作を行わないでください。outgoing 要求より先に incoming 接続が route 0 を使った場合、別接続を誤って制御せず、保守的に後片付けへ進みます。この例ではアプリケーションのデータは送受信しません。byte stream の flow control は独立した facade の責務です。

## キュー受理、ローカル結果、相手の観測

workflow が使うライフサイクル/制御メソッドの `bool` は **キュー受理** であり、処理成功・完了ではありません。`configureErtm` は例外で、同期的なローカル検証・設定の結果を返します。キュー操作や negotiation の結果ではありません。workflow は状態遷移を待ち、上限のある履歴を単調増加する sequence で読みます。履歴の欠落を成功と見なさず、診断情報の欠落として扱います。キュー拒否、ライフサイクル操作拒否、runtime failure、予期しない切断、段階ごとの timeout では、可能なら時間制限付きの後片付けを行います。原因は `failure()`、完了と失敗は別々の終端 stage で表します。

各制御の完了確認には、現在の handle/世代、新しい `control_token`、`control_pending == false`、対応する保存済み `ControlResult` が必要です。その status はローカル SDK の戻り値であり、**相手からの ACK ではありません**。非ゼロの制御結果は記録・集計しますが、開いている独立した byte stream まで壊れたと判断せず、次の制御を送れます。SDK が更新をまとめる場合もあり、すべての中間値の到達は保証できません。

`report().routes` は snapshot から remote line/modem status、local/remote port 設定、世代ごとのイベント数をコピーします。接続準備中の初期通知も観測対象です。受信 modem 値は wire EA bit を保持します。受信 port 値には XON/XOFF もありますが、固定 SDK の設定 API では変更できないため送信設定ではゼロのままにします。snapshot は最新値であり、出力一回がイベント一件とは限らず、要求に対応づけた返信でもありません。世代が変われば以前の値・カウンターは無効です。これらは仮想シリアル設定で、UART ハードウェアや Bluetooth 転送速度の設定ではありません。

各 route の `transport` は `Client::transport(current_handle)` による `Basic`、`Ertm`、`Unknown` の実観測です。コンパイル設定、要求したモード、バッファ割当数から推定しません。切断済み・古い・観測不能な binding は `Unknown` です。

## 任意の ERTM とバッファ寿命

ERTM 版は、互いに独立した `alignas(16)` static 12288-byte バッファを二つ持ちます。static クライアントとすべての非同期 callback の間、保存先を維持します。`configureErtm` が検証・コピーするのは設定であり、バッファ本体ではありません。一つの lease は route ごとではなく RFCOMM multiplexer ごとなので、二つの route が共有する場合があります。

この例の `ertm_mandatory` は **0 固定** で、Basic fallback を許可します。必要な二つの define と設定済み pool があっても ERTM negotiation の成立証明にはなりません。mandatory ERTM は意図的にオプション化していません。pool 不足や、再利用した Basic multiplexer との不整合で terminal fault になり、共有無線を停止するためです。[peer/ERTM lifecycle](../../docs/spp_lifecycle_jp.md) を参照してください。

切断要求、timeout、fault だけでバッファを解放・再利用しないでください。対応する SDK release callback を待ちます。この例は release を作りません。bond 削除、別プロファイルの接続先変更、別プロファイルの ACL 切断も行いません。このリソース設定は単独 SPP 用です。A2DP/AVRCP と共用する場合、統合したリソース設定と別途の検証が必要です。

完了後、または時間制限付きの後片付けに失敗した後も、`main` は所有者と保存先を保持して poll を続けます。一度限りのクライアントを再生成したり、後片付けの代わりに共有無線を止めたりしません。後片付け失敗を正常切断と表示しません。公開 `Client::end`/再起動契約はないため、製品側で終端エラー時の方針を決めてください。

## ビルドのみ

[ビルドガイド](../../docs/build_guide_jp.md) の固定 Pico SDK/toolchain を使います。以下のコマンドは firmware の実行やデバイス操作を行いません。

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S examples/SppControlMainLoopSample -B /tmp/spp-sample-basic
cmake --build /tmp/spp-sample-basic --parallel 2
cmake -S examples/SppControlMainLoopSample -B /tmp/spp-sample-ertm \
  -DSPP_SAMPLE_ENABLE_ERTM=ON
cmake --build /tmp/spp-sample-ertm --parallel 2
```

到達可能な有効経路の compile/link 確認は別ディレクトリで行います。下のアドレスは **コンパイル専用の架空値** で、実際の相手に対する許可を示しません。この出力を書き込み・実行しないでください。

```sh
cmake -S examples/SppControlMainLoopSample -B /tmp/spp-sample-reachable \
  -DSPP_SAMPLE_ENABLE_RADIO=ON -DSPP_SAMPLE_ENABLE_ERTM=ON \
  -DSPP_SAMPLE_FIRST_PEER=02:00:00:00:00:01 \
  -DSPP_SAMPLE_NEXT_PEER=02:00:00:00:00:02
cmake --build /tmp/spp-sample-reachable --parallel 2
```

さらに別ディレクトリで `SPP_SAMPLE_ENABLE_ERTM=OFF` として、到達可能な Basic 経路もビルドします。無線有効時はゼロ/同一 peer を拒否し、アドレス形式は六つの 16 進 octet を要求します。`SPP_SAMPLE_REMOTE_CHANNEL`、`SPP_SAMPLE_LOCAL_CHANNEL` は既定が 1、2 で、許容範囲は 1–30 です。実際に使うには正しい peer/channel と、サービス公開・相手の仮想 port 設定変更に対する許可が必要です。認証は有効、自動 pairing 確認は無効です。永続 bond backend や認証情報の import は設定していません。

## 検証の範囲

```sh
bash tests/host/run_spp_usage_sample_tests.sh
```

ホスト受入試験では、**同じ** workflow ヘッダーへ決定的な facade を注入できます。ready/result の遅延、制御拒否、受信観測、busy SDP、未返却 lease、キュー中の peer 選択、timeout を扱えます。ホストでの順序試験や Basic/ERTM の ARM ビルドは、無線動作を証明しません。status の到達、peer 切替、ERTM negotiation、再送、相互接続、永続 bond、他プロファイルとの共存の実機検証済みとは主張しません。

契約の詳細は [serial controls](../../docs/spp_control_jp.md)、[peer/ERTM lifecycle](../../docs/spp_lifecycle_jp.md)、[SDK integration](../../docs/spp_sdk_integration_jp.md) を参照してください。
