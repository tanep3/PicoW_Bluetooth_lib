# AvrcpCoverArtCompileProbe

[English](README.md)

これは**compile/link専用probe**で、実行手順の利用サンプルや実機試験ではありません。Cover Art Client plus shared signalling/Browsingの公開API参照を残し、固定Pico SDKと生成BTstack overlayの組合せでlinkを確認します。`initialize_for_link_check`は既定falseで、begin/無線開始をせず終了します。このflagを有効化して、並んだAPI呼び出しを動作するBluetooth手順と扱わないでください。相手接続・応答・送信可否・切断完了を待たず、API参照を作るためのコードです。

## ビルドだけを行う

対応SDKが導入された環境でrepository rootから実行します。

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S examples/AvrcpCoverArtCompileProbe -B build/build-avrcp-cover-art-compile-probe -DCMAKE_BUILD_TYPE=Debug
cmake --build build/build-avrcp-cover-art-compile-probe -j4
```

ELF/UF2は指定build directory内。helperはこのconsumerにだけhash検証済みoverlayを生成し、導入済みSDKは変更しません。未知/変更済みSDKでは誤patchを適用せず停止します。

## 確認できること

- 公開header/typeと参照APIがPico W向けにcompileできること。
- 選択profileとSDK依存を一緒にlinkできること。
- その構成でELF/UF2を生成できること。

実peer相互運用、応答順序、AVRCP全意味動作、音声出力、同時通信時の資源、長時間安定性は証明しません。build確認にflashや録音を含めません。

上限/所有権/実行計画は[検証範囲](../../docs/validation_jp.md)、[Controller](../../docs/avrcp_controller_jp.md)、[Browsing](../../docs/avrcp_browsing_jp.md)、[Cover Art](../../docs/avrcp_cover_art_jp.md)を参照。共有roleは単一main loop・process-lifetime ownerです。callbackから借りたbyteは後で使う前にコピーし、ローカルAPI受付を相手の成功と混同しません。

## 構成・リンクで困ったとき

SDK/toolchain が見つからない場合は[ビルドガイド](../../docs/build_guide_jp.md)を確認してください。overlay の hash 不一致は SDK との不整合です。検査を迂回せず対応 SDK を使用してください。既定 firmware が何もせず戻るのは正常です。
