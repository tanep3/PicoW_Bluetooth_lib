# HFP通話音声が非対応である理由

[English](HFP_UNSUPPORTED_REASON_en.md) · [対応機能](supported_features_jp.md)

このライブラリはBluetooth ClassicのA2DP音声、AVRCP再生操作、SPPデータ通信を対象とします。対応済みのHFP/HSP通話音声・マイク入力APIは提供していません。

HFPの制御接続とSCO/eSCOの音声転送は別の経路です。制御接続や要求受信が成功しても、Pico Wで音声を転送・取得できる証明にはなりません。調査ではSCO音声を端から端まで取得できる動作経路を確立できませんでした。

停止の内部原因や場所は未特定です。チップの物理故障、すべての機器構成での原理的な不可能性、特定のBTstack関数の責任を断定しません。SPI/WLANの読取り成功だけでもBluetoothコアのSCO経路の正常性は証明できません。

マイクや通話音声が必要な場合は、音声経路を正式にサポートし検証されたプラットフォーム・SDKを選んでください。A2DP SBC受信はHFPマイク対応の代わりにはなりません。FastStream、BLE、LE Audioも代替機能として提供していません。

対応profileの限界は[検証範囲](validation_jp.md)を参照してください。
