# A2DP SDK対応範囲と受入残作業

**対応範囲・受入の棚卸し資料です。初回セットアップ手順ではありません。**

[English](a2dp_sdk_coverage.md)

対象は、ライブラリ全体としてSource **と** Sink、インストール済みBTstackのA2DP機能全体、両roleの併用まで扱うというユーザー要件です。BluePicoは利用例の一つで、ライブラリを限定する条件ではありません。本書は棚卸しであり、**完了や実機保証の宣言ではありません**。

調査対象はPico SDK 2.1.1 / BTstack 1.6.2の `src/classic/a2dp_source.h`, `a2dp_sink.h`, `a2dp.c`, `avdtp.c` と両role adapterです。2つのA2DP公開ヘッダの全宣言を末尾に掲載します（Source21、Sink17。共通payload-size照会を含む）。内部AVDTP機能やbuild option依存動作は別途受入が必要です。汎用codec設定関数があることとencoder/decoderが実装されていることは別です。

## 実装と検証の段階

- `A2dpSourcePlayer`はPCM→SBC/RTP送信と[着信・pause/resume・sample-rate制御](a2dp_source_lifecycle_jp.md)を実装。[完全frameのbatching](source_mtu_batching_contract_jp.md)は1〜4個を選び、[有界Source断片送信](source_sbc_fragmentation_contract_jp.md)は大きすぎる対応frameを2〜15断片へ分け、物理packet数とmedia完了を別々に集計します。実codecの固定host試験とoffline ARMリンクはこの経路の証拠であり、汎用codec/encoded-media APIや実機相互接続の合格ではありません。
- 新規 [A2dpSinkReceiverサンプル](../examples/A2DPSinkReceiver/README_jp.md)は固定peerのSBC受信→アプリ `IAudioSink`、着信受付/発信要求、有界callback、厳密復号、明示的な損失診断を提供する第一段階。新facadeのhost試験/cross-buildと旧probeの実機証拠は別です。新facadeの実機受入は未完です。
- 既存Sink probeには受信/復号/共存の実機証拠がありますが、診断用にPCMを消費するもので、新公開API、DAC/I2S出力、全codec/role組合せを証明しません。
- 現在のSinkは[128種類のSBC形式](sink_sbc_configurations_contract_jp.md)を実装。44.1/48 kHz、mono/dual/stereo/joint、4/8/12/16 blocks、4/8 subbands、loudness/SNR、bitpool広告2–86で形式別制限を適用します。通常packetと[有界SBC断片再構成](a2dp_sink_fragments_jp.md)はオフライン検証済みです。[生設定guard](sink_raw_configuration_contract.md)は対応するSINGLE_PACKET方針の範囲で、選択bitや不正TLVをSDK解析前に確認します。[callback拒否後の回復](sink_validator_recovery_contract_jp.md)には、正常な固定入力による別のhost/ARM証拠を追加し、拒否通知と同一sessionでの継続を確認しました。より広いsignaling lifecycle、分割処理、任意16/32 kHz、独立したSBC適合、他codec、無線・MTU相互接続は未完です。この回復作業では旧raw入力試験や拒否された全bitmap試験を再実行していません。通常Sink helperが生成guardを選び、手動ソース追加では適用されません。同じCMake構成のsame-peer実験overlayとは明示的に競合します。

[任意のSink START判定](sink_start_decisions_jp.md)は要求固有ID・live session確認付きでSDKの開始許可／拒否を公開します。応答時期はアプリ責任で、ビルドとreceiverの両方に有効設定が必要です。既定は自動開始を維持。固定した正常SDK／facade試験とARMビルドを、全lifecycleや実peerの受入とは扱いません。

正常STARTの結合3 sessionでは、実際の共有dispatcher、SDKのA2DP変換、receiverを通過しました。接続／codecの初期設定、transport、runtime／ACL epochは模擬で、交渉・認証・実epoch失効・他profile同時動作は証明しません。本体修正は不要でした。


Runtime失効の結合試験では、対象切断・別peer切断・同一handle再作成・OFF通知について、実ACL epoch処理とSink要求の寿命を接続しました。機器側境界と他profileの観測callbackは模擬で、profile同時動作や実controller停止の受入ではありません。


## 両role併用がクラス追加だけでは済まない理由

1. SDKの `a2dp_init()` は空、`avdtp_init()` は共有L2CAP登録をガードします。両roleを想定する処理もあり、SDKが一律非対応という意味ではありません。
2. 従来 `BluetoothRuntime` はA2dp claim/handlerが一つ。A2dpSink枠と共有所有registryを追加しました。別peerの同時登録は検証中で、同peer登録は拒否します。従来のガードを外すだけでは危険です。
3. SDKの自動SEP探索はアプリのsignaling callback後も進むため、アプリ側のfilterだけでは他roleを保護できません。`A2dpSharedSdk.h` はSDK設定処理より前にraw AVDTPイベントをCID/peerで分離し、Sourceのdelay-reportと送信creditの転送も保持します。このshimはインストール済みSDKに依存し、SDK更新時に回帰確認が必要です。
4. 同じpeerのSource/SinkはAVDTP signalingを共有します。`a2dp_sink_disconnect()` は共通の `avdtp_disconnect()` を呼びます。role単位stopとpeer全体切断の契約を定義し、検証する必要があります。
5. SDK 1.6.2は着信途中の既存接続を `avdtp_connect()` が見つけると、Sink開始処理で未設定outgoing CIDを使い得ます。新facadeは既存SDK接続があればこの経路を拒否します。併用統合には検証済みの代替経路が必要で、チェック削除では済みません。
6. 未完発信の取消はSDKで `SIGNALING_CONNECTION_ESTABLISHED(error)` により完了し、必ずしもRELEASEDが来ません。新SinkはCID/peerを検証して取消を終端させ、別CID/peerの負例もテストします。
7. SDKの `avdtp_connection_t` は単一の `configuration_state` と `local_seid` を持ち、initiator/acceptorとも同peer接続への二つ目の設定を拒否します。同peer同時streamにはアプリdispatchだけでなくSDK層の変更と実SDK lifecycleの試験が必要です。
8. profile deinitは共有A2DP/AVDTP deinitを呼び、他roleのcallback/endpointも消し得ます。role切替・破棄・再初期化はdeinitを公開するだけでなく共有所有管理が必要です。

## 今後完了すべき受入マトリクス

| 領域 | 必要な検証 |
|---|---|
| roleと接続方向 | Source/Sinkそれぞれ発信・着信。実central/peripheralは別に記録 |
| role併用 | 同peer/別peer Source+Sink、登録順、独立stream操作、共有signaling切断、SPP/AVRCP共存 |
| stream lifecycle | configure/open/start、local/remote pause/resume、再設定、拒否/失敗、media/signaling release、未完取消、再接続、role切替 |
| SBC | 実装する全rate/channel/block/subband/allocation/bitpool、交渉上限、fragment組立、不正packet/CRC拒否 |
| 他codec | MPEG Audio、AAC、ATRAC、vendor/other設定。アプリへのencoded-byte境界と実encoder/decoderの有無を区別 |
| endpoint管理 | 複数SEP、capability、独自validator、finalize、active session中の安全な生存期間 |
| media I/O | RTP payload/raw packet API、MTU/buffer所有権、send-credit競合/backpressure、PCM所有権/損失/session境界 |
| Sink開始判断 | explicit-start option有無、requested/accept/reject、古い応答の保護 |
| 資源/障害 | pool不足、queue溢れ、不正/別peer/旧event、共有runtime障害、他profileの意図しない切断防止 |
| 実機 | 双方向の合成既知信号、hostでの完全一致/codec考慮の照合、実際の送受role経路、有限時間の共存負荷。無断録音なし |

host PASS、link成功、旧probe PASS、SDK関数の存在を、この全体の保証に読み替えません。保証には構成、fixture、コードrevision、結果証拠が必要です。

## SDK公開関数

以下は調査したSDK宣言であり、**新facadeの公開済みメソッド一覧ではありません**。初期化/登録/送受信などは必要に応じ内部利用していますが、汎用codec選択、endpoint lifecycle、raw media、両role管理は公開・検証が未完です。Sinkのexplicit-start関数2つは `ENABLE_AVDTP_ACCEPTOR_EXPLICIT_START_STREAM_CONFIRMATION` 条件付きです。

### Source

```cpp
void a2dp_source_create_sdp_record(uint8_t * service, uint32_t service_record_handle, uint16_t supported_features, const char * service_name, const char * service_provider_name);
void a2dp_source_init(void);
avdtp_stream_endpoint_t * a2dp_source_create_stream_endpoint(avdtp_media_type_t media_type, avdtp_media_codec_type_t media_codec_type, const uint8_t *codec_capabilities, uint16_t codec_capabilities_len, uint8_t * codec_configuration, uint16_t codec_configuration_len);
void a2dp_source_finalize_stream_endpoint(avdtp_stream_endpoint_t * stream_endpoint);
void a2dp_source_register_packet_handler(btstack_packet_handler_t callback);
uint8_t a2dp_source_establish_stream(bd_addr_t remote_addr, uint16_t *avdtp_cid);
uint8_t a2dp_source_reconfigure_stream_sampling_frequency(uint16_t a2dp_cid, uint32_t sampling_frequency);
uint8_t a2dp_source_start_stream(uint16_t a2dp_cid, uint8_t local_seid);
uint8_t a2dp_source_pause_stream(uint16_t a2dp_cid, uint8_t local_seid);
uint8_t a2dp_source_disconnect(uint16_t a2dp_cid);
void a2dp_source_stream_endpoint_request_can_send_now(uint16_t a2dp_cid, uint8_t local_seid);
int a2dp_max_media_payload_size(uint16_t a2dp_cid, uint8_t local_seid);
uint8_t a2dp_source_stream_send_media_payload_rtp(uint16_t a2dp_cid, uint8_t local_seid, uint8_t marker, uint32_t timestamp, uint8_t *payload, uint16_t payload_size);
uint8_t a2dp_source_stream_send_media_packet(uint16_t a2dp_cid, uint8_t local_seid, const uint8_t * packet, uint16_t size);
uint8_t a2dp_source_set_config_sbc(uint16_t a2dp_cid, uint8_t local_seid, uint8_t remote_seid, const avdtp_configuration_sbc_t * configuration);
uint8_t a2dp_source_set_config_mpeg_audio(uint16_t a2dp_cid, uint8_t local_seid, uint8_t remote_seid, const avdtp_configuration_mpeg_audio_t * configuration);
uint8_t a2dp_source_set_config_mpeg_aac(uint16_t a2dp_cid, uint8_t local_seid, uint8_t remote_seid, const avdtp_configuration_mpeg_aac_t * configuration);
uint8_t a2dp_source_set_config_atrac(uint16_t a2dp_cid, uint8_t local_seid, uint8_t remote_seid, const avdtp_configuration_atrac_t * configuration);
uint8_t a2dp_source_set_config_other(uint16_t a2dp_cid, uint8_t local_seid, uint8_t remote_seid, const uint8_t * media_codec_information, uint8_t media_codec_information_len);
void a2dp_source_register_media_config_validator(uint8_t (*callback)(const avdtp_stream_endpoint_t * stream_endpoint, const uint8_t * event, uint16_t size));
void a2dp_source_deinit(void);
```

### Sink

```cpp
void a2dp_sink_create_sdp_record(uint8_t * service, uint32_t service_record_handle, uint16_t supported_features, const char * service_name, const char * service_provider_name);
void a2dp_sink_init(void);
avdtp_stream_endpoint_t * a2dp_sink_create_stream_endpoint(avdtp_media_type_t media_type, avdtp_media_codec_type_t media_codec_type, const uint8_t *codec_capabilities, uint16_t codec_capabilities_len, uint8_t * codec_configuration, uint16_t codec_configuration_len);
void a2dp_sink_finalize_stream_endpoint(avdtp_stream_endpoint_t * stream_endpoint);
void a2dp_sink_register_packet_handler(btstack_packet_handler_t callback);
void a2dp_sink_register_media_handler(void (*callback)(uint8_t local_seid, uint8_t *packet, uint16_t size));
uint8_t a2dp_sink_establish_stream(bd_addr_t remote, uint16_t * out_a2dp_cid);
uint8_t a2dp_sink_start_stream_accept(uint16_t a2dp_cid, uint8_t local_seid);
uint8_t a2dp_sink_start_stream_reject(uint16_t a2dp_cid, uint8_t local_seid);
void a2dp_sink_disconnect(uint16_t a2dp_cid);
uint8_t a2dp_sink_set_config_sbc(uint16_t a2dp_cid, uint8_t local_seid, uint8_t remote_seid, const avdtp_configuration_sbc_t * configuration);
uint8_t a2dp_sink_set_config_mpeg_audio(uint16_t a2dp_cid, uint8_t local_seid, uint8_t remote_seid, const avdtp_configuration_mpeg_audio_t * configuration);
uint8_t a2dp_sink_set_config_mpeg_aac(uint16_t a2dp_cid, uint8_t local_seid, uint8_t remote_seid, const avdtp_configuration_mpeg_aac_t * configuration);
uint8_t a2dp_sink_set_config_atrac(uint16_t a2dp_cid, uint8_t local_seid, uint8_t remote_seid, const avdtp_configuration_atrac_t * configuration);
uint8_t a2dp_sink_set_config_other(uint16_t a2dp_cid, uint8_t local_seid, uint8_t remote_seid, const uint8_t * media_codec_information, uint8_t media_codec_information_len);
void a2dp_sink_register_media_config_validator(uint8_t (*callback)(const avdtp_stream_endpoint_t * stream_endpoint, const uint8_t * event, uint16_t size));
void a2dp_sink_deinit(void);
```
