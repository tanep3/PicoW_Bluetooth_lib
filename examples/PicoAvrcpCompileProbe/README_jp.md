# AVRCP共有compile/link probe

日本語 | [English](README.md)

polling runtimeによるA2DP Source + raw SPP + AVRCP Targetのcompile専用検査です。API、SDK応答契約、制約は [AVRCP Target](../../docs/avrcp_target_jp.md) を参照してください。実機試験ではなく、通常entryではBluetoothを初期化しません。

```sh
PICO_SDK_PATH=/path/to/pico-sdk cmake -S . -B ../../.work/builds/build-avrcp-target
cmake --build ../../.work/builds/build-avrcp-target -j4
```

上記はこのexample directoryから実行します。検査にflash、無線、音声操作は不要です。
