# A2DP Source + Sink の組み込み

[English](a2dp_dual_role.md) · [SDK受入残作業](a2dp_sdk_coverage_jp.md)

現在は **別peer** のSource/Sink ownerを許可し、通常の同peer登録は拒否を維持します。明示的な[SDK実験](a2dp_same_peer_experiment_jp.md)だけでhost/ARM開発用に許可しますが、取消の残経路や実機要件は未受入です。

## 所有権とdispatch

同一peerの実験helperと公開Sink helperは同じCMake構成で併用できません。[実験機構と制約](a2dp_same_peer_experiment_jp.md)を確認してください。

- 異なるpeerアドレスを設定した `A2dpSourcePlayer` と `A2dpSinkReceiver` を各1個、プログラム終了まで保持します。PCM objectも生存させ、同一main loopから両方の `poll()` を頻繁に呼びます。
- `A2dpShared.h` はruntime claim成功後に各roleの固定peerを記録します。登録順はどちらでもよく、同roleの重複や両roleへの同じ非zero peerの登録はSDK初期化前に拒否します。Sourceの空targetは無効化した例のために使えますが、wildcardやrouting方針ではありません。
- SDKはsignalingを両roleへ通知した後、自動SEP探索を始めます。アプリcallbackで無視するだけでは遅いため、`A2dpSharedSdk.h` はSDKの薄いraw AVDTP adapterを、同等の転送に固定peerのallowlistと明示admission判定を加えた処理に置き換え、SDK設定処理の **前** に分離します。不明・未許可peerの成功signalingは両roleの設定処理へ渡さず閉じ、他role所有peerは誤ったroleから切断しません。
- Sourceのdelay-reportと送信creditの転送は元SDKと同じです。shimが受けるのはSDK生成のmeta eventであり、任意のBluetooth packetではありません。wire parsingはSDKの責務です。防御的にframing、一部固定イベントの長さ、connectionの実在を確認します。
- 別peerの一方のsignaling切断で、無線電源や他peerのA2DP sessionは切断しません。SPP/AVRCPは別runtime ownerのままです。実際の同時無線負荷と他profileへの影響は実機受入が必要です。
- SDK callbackの直接登録や旧profileとの混在は禁止です。後からraw AVDTP handlerを登録するとrole gateを上書きします。

## ビルド資源

`pico_sdk_init()` 後に両helperをincludeし、同じtargetへ適用します。

```cmake
include(${PICOW_BT_ROOT}/cmake/picow_a2dp_source.cmake)
include(${PICOW_BT_ROOT}/cmake/picow_a2dp_sink.cmake)
add_executable(app main.cpp)
picow_target_a2dp_source(app ${CMAKE_CURRENT_LIST_DIR})
picow_target_a2dp_sink(app ${CMAKE_CURRENT_LIST_DIR})
```

既存Source pipelineが必要とするため、SDK初期化前に `PICO_CXX_ENABLE_EXCEPTIONS=1` を設定します。アプリの `btstack_config.h` を用意してください。別peer 2台、各role 1 endpointには少なくとも次が必要です。

```c
#define MAX_NR_AVDTP_CONNECTIONS 2
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 2
#define MAX_NR_HCI_CONNECTIONS 2
```

この3項目だけで設定全体は完成しません。L2CAP channel/service、SDP record、ACL buffer、SPP/AVRCPの追加peerも含めて設計してください。既存のSource/Sink単独例は各1個のAVDTP connection/endpointしか予約していないため、併用で設定をそのまま流用しないでください。Sink helperは32 KiBのstackを予約します。リンク成功はheap/stackの最大使用量測定や実時間PCM処理能力の保証ではありません。

## 探索待ちキューの進行

SDKはSEP探索の所有者をグローバルに一つ持ち、一部の設定完了・signaling解放経路は所有者を解除しても待機側を進めません。共有shimはprivate lockを書き換えず、登録peerのSDK待機flagがある処理だけをSDK公開のschedule関数へ再提示します。初期signaling受付と継続探索のadmission callbackは分離し、取消・故障・生存確認不成立のownerは待機処理を再開できません。解放中CIDを除外し、再入も制限します。

実SDK fixtureでSource解放後の待機Sink進行、Sink設定後の待機Source進行、取消後の抑止を検証しています。ただしcodec/media negotiation全体や実機の証拠ではありません。登録、dispatch分離、同時media、並行negotiationは別の受入項目です。

## 同peerで必要なSDK作業

BTstack 1.6.2の `avdtp_connection_t` はpeer接続当たり単一の `configuration_state` とsignaling用 `local_seid` を持ちます。initiator/acceptorの設定処理は二つ目のstream設定を拒否し、両roleはsignaling切断とdeinit状態も共有します。したがって同peer guardを外すだけでは実装になりません。

stream別の設定・signaling所有権、両media channelのopen/routing、role単位のclose/abort、安全なsignaling切断、再接続・role切替、実SDKと実機の試験が必要です。codec、endpoint、encoded media APIの拡張も[対応残作業](a2dp_sdk_coverage_jp.md)に残っています。

## 検証の範囲

別peerの二重stream negotiation、音声timing、無線共存は未検証です。登録・dispatch・探索のhost試験とARMリンクは、同時無線動作の保証ではありません。

[検証範囲](validation_jp.md)
