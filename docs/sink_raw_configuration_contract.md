# Public Sink raw SBC configuration contract / 生SBC設定の契約

## English

`picow_target_a2dp_sink` now selects a generated, consumer-specific `avdtp_acceptor.c` overlay. It checks incoming SINGLE_PACKET SET_CONFIGURATION and RECONFIGURE payloads before the installed SDK unpacks or normalizes SBC selections. The installed Pico SDK 2.1.1 / BTstack 501e6d2b86e6c92bfb9c390bcf55709938e25ac1 remains unchanged. The generator pins the original acceptor SHA256 and fails on other revisions. Manually compiling receiver sources does not install this guard; use the CMake helper or explicitly integrate the reviewed overlay.

Only local SINK endpoints are changed. Source endpoints inside an opted-in executable bypass the guard; Source-only sibling targets retain the original SDK acceptor. No AVRCP source is generated or modified by this overlay. This does not establish that original Source behavior is safe. The existing experimental same-peer AVDTP overlay and this overlay cannot coexist in one CMake configuration, even on different targets; either call order fails explicitly instead of silently selecting one. Independent AVRCP overlays operate on different source files, but this work makes no new AVRCP behavioral claim.

The public Sink accepts only its advertised categories: media transport, SBC media codec and optional delay reporting. Category TLVs must be complete, nonduplicated and exactly bounded, with no trailing byte. SET requires transport (length0) and codec; delay reporting has length0. RECONFIGURE requires codec and rejects other categories. Unsupported recovery, reporting, content protection, compression and multiplexing are rejected before the SDK parser. This is a public SBC Sink policy, not a general-purpose AVDTP capability parser.

The codec TLV must contain exactly 6 bytes: audio media type, SBC codec identifier and exactly 4 SBC configuration bytes. Each of frequency, mode, blocks, subbands and allocation must select exactly one bit. Zero/multiple selections return their codec-specific INVALID errors: C3/C5/DD/C7/C9. A one-bit 16/32kHz frequency returns NOT_SUPPORTED C4. The format set remains 128 combinations: 44.1/48kHz, all four channel modes,4/8/12/16blocks,4/8subbands and both allocations. Bitpool must be ordered, within the advertised 2..86 envelope, and its minimum must be decodable for that format under the exact byte-rounded 320/512kbit/s and syntax ceilings. An upper envelope above a format's frame-specific ceiling remains legal: the existing media decoder checks every frame's bitpool independently. Out-of-syntax min/max return CB/CD; a valid but unadvertised envelope or impossible minimum returns unsupported configuration 0x29.

Rejected raw settings do not reach the normalized callback, replace stored codec bytes or change the configured endpoint state. A valid same-session SET retry is tested after the reject is sent; RECONFIGURE evidence covers successful configuration followed by rejected input. RECONFIGURE additionally calls the existing normalized Sink validator before updating the endpoint, something the installed SDK omitted. Successful reconfiguration rebinds the remote codec-information pointer to the stable endpoint buffer, so a later signaling packet cannot overwrite the retained configuration through a borrowed command pointer.

[Validator rejection recovery](sink_validator_recovery_contract.md) restores the prior Sink state/owner for SET callback veto and preserves response routing and notifications. Incoming signaling fragmentation/reassembly is unchanged. This is not general malformed-signaling protection or proof of all AVDTP state rules. The raw-guard and subsequent lifecycle tests are separate: the later fixed-valid-input tests do not rerun the original raw corpus or establish exhaustive bitmap coverage. Host sessions and L2CAP transport are mocks; this guard has no dedicated radio qualification. See the development history for the sequence and test counts.

See new host/build evidence, [existing SBC frame contract](sink_sbc_configurations_contract.md), [overlay attribution](../tools/NOTICE.md) and [new manifest](../licenses/sink-raw-configuration-manifest.json). Specification reference: [A2DP1.4.1 §§4.3.2,5.1.3](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/a2dp_v1-4-1_1752513648/A2DP_v1.4.1/out/en/index-en.html). Existing historical evidence remains unchanged.

## 日本語

公開SinkのCMake helperは、SDKを直接編集せず、対象実行ファイルだけに生成済みacceptorを選択します。固定SDKハッシュと不一致なら構成を失敗させます。手動でreceiverソースだけを追加した場合、この検証は自動的には入りません。Source endpointは検証を迂回し、Source専用の兄弟targetは元SDKを使用します。AVRCPソースは変更しません。same-peer実験overlayとは、別targetであっても同じCMake構成内で共存できず、両方の呼び出し順で明示的に失敗します。

SET_CONFIGURATION/RECONFIGUREの生TLVを、SDKの解析・SBC正規化より前に検証します。不完全長、余分な末尾byte、重複category、SBC設定の4byte以外、各選択fieldの0bit/複数bitを拒否します。公開Sinkが広告するtransport/SBC/delayのみ対応し、SETではtransportとcodec、RECONFIGUREではcodecを必須にします。RECONFIGUREの他categoryは非対応です。媒体型audio、codec SBC、対応周波数、bitpool2..86と最小値の実現可能性も確認します。上限envelopeに含まれる各frameのbitpool適法性は引き続きdecoderで検証します。

生設定の拒否では既存codec、endpoint状態を維持し、SETでは拒否応答後に同じsessionで正常設定を再試行できることを試験しました。RECONFIGUREの証跡は成功した設定の後に不正入力を拒否し、設定を保持する範囲です。RECONFIGUREには元SDKが省略していたアプリvalidator呼び出しを追加し、成功後のcodec pointerを安定bufferへ付け替えます。後続packetによるcommand buffer上書きで、保持したcodec設定を壊さないためです。

[validator拒否後の回復](sink_validator_recovery_contract_jp.md)は、SET callback拒否時に以前のSink状態・ownerを戻し、応答先と通知を維持します。signaling分割再構築は変更しません。汎用的な不正signaling対策やAVDTP全状態の保証ではありません。raw guard試験と後続lifecycle試験は別です。後続の固定正常入力試験は元のraw入力全件の再実行でも全bitmap網羅でもありません。hostのsession/L2CAPはmockで、このguard単独の無線実機検証はありません。経緯と試験件数は開発履歴を参照してください。

[Validation scope](validation.md) / [Validation scope](validation.md)
