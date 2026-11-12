# 対応範囲とAPI契約

[English](specification.md)

対象はRaspberry Pi Pico W、Pico SDK、C++17、Bluetooth Classicです。独立したアプリのCMake targetに必要なhelperを適用して使います。Arduino・MicroPython向けのパッケージやHFP/BLE音声ライブラリではありません。

- 音声: SBCのSource送信とSink受信。出力DAC・I2S・音量処理はアプリが担当します。
- 再生操作: AVRCP Controller/Target。追加APIの存在は全機器での実機検証を意味しません。
- データ: SPPのバイトstream。2ルートは2台の独立無線機や2台の同時peerを意味しません。
- 実行: 同じメインループでprofileをpollし、非同期完了、借用buffer、binding世代、容量上限を守ります。

具体的な構成・関数・制約は以下を参照してください。

1. [導入と最初のアプリ](getting_started_jp.md)
2. [機能と制約](supported_features_jp.md)
3. [APIリファレンス](api_reference_jp.md)
4. [アーキテクチャ](architecture_jp.md)
5. [検証範囲](validation_jp.md)
6. [第三者ライセンス](../THIRD_PARTY_NOTICES.md)

機能ごとの所有権・タイミング・容量・実験設定については、APIリファレンスとそこからリンクする個別契約を優先してください。
