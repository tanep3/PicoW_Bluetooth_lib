# Sink START 判断サンプル

[English](README.md)

公開 START 許可・拒否 API の、処理量を制限した利用例です。**音声 renderer はなく PCM はすべて破棄**します。pairing confirmation は既定で無効です。自動 listen/connect/reconnect はありません。

## ビルドのみ

リポジトリのルートで、インストール済み Pico SDK とローカルのビルドツールを使います。

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S examples/A2dpSinkStartDecisionSample -B build/sink-start
cmake --build build/sink-start
```

既定は radio OFF・START 機能 OFF です。機能を含めるには `-DSINK_START_DECISIONS=ON` を追加します。手動操作の radio 経路をコンパイルする場合は、さらに `-DSINK_START_ENABLE_RADIO=ON -DSINK_START_PEER=<接続予定の送信元アドレス>` が必要です。空・zero・broadcast アドレスはこの経路では拒否します。SDK/picotool の準備は[ビルドガイド](../../docs/build_guide_jp.md)を参照してください。オフラインでビルドする場合は互換性のある picotool を事前に導入し、必要に応じて CMake パッケージの場所を `picotool_DIR` に指定します。ビルドだけでは書き込みや実行は行われません。

## Pico W への書き込み後のコマンド

- `a`: 以降の START を許可。
- `r`: 以降の START を拒否（既定方針）。
- `b`: radio 有効ビルドだけ receiver を初期化。START 機能がなければ `FeatureUnavailable`。
- `l`: 指定 peer からの接続を受け付け。
- `c`: 指定 peer への A2DP 接続を要求。
- `x`: disconnect し、保留中の判断を取り消し。
- `s`: ビルド時の対応有無と現在の状態を表示。

相手が START を送る**前に**方針を選びます。poll 後の要求をその方針ですぐ判断し、相手の応答 timeout 中に人の入力を待ちません。suspend/restart は新しい要求となり、その時点の方針を使います。入力は一ループ一文字です。USB 出力の待機 timeout はゼロなので、Bluetooth 処理を止める代わりにログが欠ける可能性があります。

表示の `Accepted`（enum 値 0）は許可・拒否の SDK ローカル受付のみで、wire ACK や streaming の証明ではありません。通常の START 完了は状態表示の `streaming` で確認します。このサンプルはそれ以外の判断結果なら disconnect し、retry しません。

[機能契約](../../docs/sink_start_decisions_jp.md)を参照してください。ホスト検査と ARM ビルドの記録は[検証範囲](../../docs/validation_jp.md)にあります。実機実行は **NOT_RUN** で、相手機器との START 応答や音声出力を確認した例ではありません。

## 困ったとき

`feature=0` なら START 機能付きで再ビルドします。radio 無効の表示なら radio 有効設定と実際の peer を確認してください。`begun=1` を確認してから `l` または `c` を使います。接続後も再生開始しない場合は既定方針が拒否であることを確認し、次の START より前に `a` を選びます。自動ペアリング確認はないため相手の認証準備も必要です。
