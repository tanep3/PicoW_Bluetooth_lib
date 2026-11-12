# A2DP SDK coverage and acceptance backlog

**Acceptance and coverage inventory, not a beginner setup guide.**

[日本語](a2dp_sdk_coverage_jp.md)

Scope: the user's requirement is library-wide Source **and** Sink support, all A2DP functionality available in the installed BTstack, including combined-role operation. BluePico is one consumer, not a limit on the library. This is a coverage inventory, **not a completion or hardware guarantee**.

Inspected: installed Pico SDK 2.1.1 / BTstack 1.6.2, `src/classic/a2dp_source.h`, `a2dp_sink.h`, `a2dp.c`, `avdtp.c` and the source/sink adapters. The complete public declarations from those two A2DP headers are listed below (21 Source and 17 Sink, including the shared payload-size query). Internal AVDTP features and build-option-dependent behavior need separate acceptance coverage; a generic codec configuration function does not supply a codec encoder/decoder.

## Implementation and verification levels

- `A2dpSourcePlayer` provides PCM→SBC/RTP transmission with [incoming, pause/resume and sample-rate controls](a2dp_source_lifecycle.md). [Whole-frame batching](source_mtu_batching_contract.md) selects one to four frames; [bounded Source fragmentation](source_sbc_fragmentation_contract.md) now sends a too-large supported frame as 2–15 fragments with separate physical-packet and completed-media accounting. Fixed real-codec host cases and offline ARM linking are evidence for those paths, not general codec/encoded-media APIs or hardware interoperability.
- New [A2dpSinkReceiver sample](../examples/A2DPSinkReceiver/README.md): fixed-peer SBC reception→application `IAudioSink`, incoming admission/outgoing request, bounded callbacks, strict decoding and explicit loss diagnostics. First facade host tests and cross-builds are separate from old probe hardware evidence. New facade hardware acceptance remains pending.
- Existing sink probe: useful receive/decoder/coexistence hardware evidence, but it consumes diagnostic PCM and does not by itself prove the new public API, DAC/I2S output or all codec/role combinations.
- The live Sink now implements [128 SBC formats](sink_sbc_configurations_contract.md): 44.1/48 kHz, mono/dual/stereo/joint, 4/8/12/16 blocks, 4/8 subbands, loudness/SNR and advertised bitpool2–86 with format-specific limits. Complete packets and [bounded fragmented SBC frames](a2dp_sink_fragments.md) have offline evidence. [Raw Sink configuration checks](sink_raw_configuration_contract.md) now precede SDK parsing for the supported SINGLE_PACKET policy, including exact selections and malformed TLVs. [Callback-veto recovery](sink_validator_recovery_contract.md) now has separate fixed-valid-input host/ARM evidence, preserving deferred rejection notifications and same-session use. Wider signaling lifecycle, signaling fragmentation, optional16/32 kHz, independent SBC conformance, other codecs and wireless/MTU interoperability remain open. This recovery work did not rerun the earlier raw-input suite or the blocked exhaustive bitmap test. The generated guard is selected by the normal Sink helper, not manual source inclusion, and conflicts explicitly with the same-peer experimental overlay in one CMake configuration.

[Optional Sink START decisions](sink_start_decisions.md) now expose the SDK acceptance/refusal capability with per-request identity, live-session checks and explicit application timing responsibility. Build and receiver opt-in are both required; defaults retain automatic START. Fixed normal SDK/facade tests and ARM builds are bounded evidence, not full lifecycle or real-peer acceptance.

Three joined normal START sessions additionally pass through the real shared dispatcher, SDK A2DP conversion and receiver. Connection/codec setup, transport and runtime/ACL epoch remain modeled; this does not establish negotiation, authentication, real epoch retirement or concurrent profiles. No production correction was needed.


Runtime retirement integration additionally joins the production ACL epoch implementation to the Sink request lifecycle for matching disconnect, unrelated disconnect, same-handle recreation and OFF notification. Hardware-facing boundaries and other-profile observers remain modeled; this does not qualify concurrent profiles or a real controller shutdown.


## Why combined roles require more than a second class

1. SDK `a2dp_init()` is empty; `avdtp_init()` guards shared L2CAP registration. SDK code explicitly anticipates both roles. This is not a blanket SDK prohibition.
2. `BluetoothRuntime` formerly had one A2dp claim/handler. A separate A2dpSink slot and shared ownership registry are added. Distinct-peer dual registration is under validation; same-peer registration is rejected. Removing the original guard alone would be unsafe.
3. SDK automatic SEP discovery runs after application signaling callbacks. Application filtering alone cannot protect the other role. `A2dpSharedSdk.h` gates raw AVDTP events by CID/peer before the SDK configuration process, retaining Source delay-report and send-credit forwarding. This shim is pinned to the installed SDK and needs regression checks on SDK upgrades.
4. Same-peer Source/Sink share an AVDTP signaling connection. `a2dp_sink_disconnect()` calls shared `avdtp_disconnect()`; role-local stop versus whole-peer disconnect must be a documented, tested contract.
5. SDK 1.6.2 sink establishment can use an unset outgoing CID when `avdtp_connect()` finds an already-pending incoming connection. The new facade rejects existing SDK connections before that path. Combined-role integration needs an explicit, tested alternative, not removal of the check.
6. Pending outgoing cancellation is completed by `SIGNALING_CONNECTION_ESTABLISHED(error)` in the SDK, not necessarily RELEASED. The new sink validates CID/peer and retires this cancellation; tests include wrong-CID/wrong-peer negatives.
7. SDK `avdtp_connection_t` has a single `configuration_state` and `local_seid`. Both initiator and acceptor reject a second configuration on the same peer connection. Same-peer simultaneous streams require SDK-level changes and actual SDK lifecycle tests, not just application dispatch.
8. Profile deinit calls shared A2DP/AVDTP deinit and can erase the other role's callbacks/endpoints. Role switching/destruction/reinitialization needs shared ownership, not a public wrapper around deinit.

## Acceptance matrix still to complete

| Area | Required coverage |
|---|---|
| Role and direction | Source and Sink, each initiating and accepting; actual central/peripheral recorded independently |
| Combined roles | Same-peer and different-peer Source+Sink; registration order; independent stream actions; shared signaling teardown; SPP and AVRCP coexistence |
| Stream lifecycle | Configure/open/start, local and remote pause/resume, reconfigure, reject/error, media release and signaling release, pending cancellation, reconnect and role switching |
| SBC | All implemented rate/channel/block/subband/allocation/bitpool combinations, negotiated limits, fragmentation/reassembly, malformed packet and CRC rejection |
| Other codecs | MPEG Audio, AAC, ATRAC and vendor/other codec configuration; encoded-byte application boundary and actual encoder/decoder availability kept distinct |
| Endpoint management | Multiple SEPs, capabilities, custom validators, endpoint finalize and safe lifetime across active sessions |
| Media I/O | Payload RTP and raw packet APIs, MTU/buffer ownership, send-credit races/backpressure, PCM ownership/loss and stream boundaries |
| Sink start decision | Build with/without explicit-start confirmation, requested/accept/reject transitions and stale-response protection |
| Resources/failures | Pool exhaustion, queue overrun, malformed/foreign/stale events, shared runtime failure, no unintended other-profile teardown |
| Hardware | Synthetic known signal in both directions, host exact/codec-aware checks, real receiver/source role paths and combined finite-duration load; no unannounced recording |

Do not turn “host tests passed,” “linked,” “old probe passed” or an SDK function declaration into a guarantee for this entire matrix. Every guarantee needs its configuration, fixture, code revision and result evidence.

## SDK public functions

The following are the inspected SDK declarations, **not methods newly promised by the application facade**. Setup/registration/send/receive operations are used internally where appropriate; general codec selection, endpoint lifecycle, raw media access and combined-role management are not fully exposed or validated yet. The two Sink explicit-start functions are conditional on `ENABLE_AVDTP_ACCEPTOR_EXPLICIT_START_STREAM_CONFIRMATION`.

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
