/*
 * This file contains an updated implementation of the A2DPConnectionPlugin
 * that fixes issues in the original version.  In particular, it now
 * initializes the A2DP Source subsystem and registers a minimal SDP record
 * so that a remote A2DP Sink can discover our service.  In addition, we
 * provide reasonable SBC codec capabilities and a default configuration
 * instead of an all‑ones dummy array.  These changes align the code with
 * the recommendations from the official BTstack examples and API docs【36023639349170†L329-L348】【668422887415374†L140-L179】.
 */

#include "A2DPConnectionPlugin.h"

#include "btstack_debug.h"
#include "btstack_event.h"
#include "a2dp_source.h"
#include "btstack_run_loop.h"

// include SDP routines to register our A2DP Source service
#include "sdp_client.h"
#include "sdp_util.h"
#include "avdtp.h"
#include "DebugLog.h"
// DebugLog provides PWB_LOG_MSG / PWB_SLEEP_MS macros

// extern LGFX display;

// static instance pointer used by the packet handler
static A2DPConnectionPlugin* s_instance = nullptr;

A2DPConnectionPlugin::A2DPConnectionPlugin(BTStackConnectionPlugin* connection_plugin)
    : connection_plugin_(connection_plugin),
      a2dp_cid_(0),
      local_seid_(0),
      is_connected_(false) {
        s_instance = this;
        memset(remote_addr_, 0, sizeof(remote_addr_));
      }

A2DPConnectionPlugin::~A2DPConnectionPlugin() {
    if (s_instance == this) s_instance = nullptr;
}

void A2DPConnectionPlugin::setup() {
    // Initialise the A2DP Source module.  According to the BTstack examples
    // this must be called before any other A2DP API is used【36023639349170†L329-L348】.
    a2dp_source_init();

    // Register our static packet handler.  It will receive A2DP subevents
    // encapsulated in HCI_EVENT_A2DP_META events【668422887415374†L140-L179】.
    a2dp_source_register_packet_handler(&A2DPConnectionPlugin::packetHandler);

    // Provide SBC codec capabilities and a default configuration.  The first
    // byte encodes supported sampling frequencies and channel modes.  We
    // advertise support for 44.1 kHz/48 kHz and stereo channels.  The second
    // byte encodes block length, subbands and allocation method.  The final
    // two bytes provide the minimum and maximum bitpool values.  See
    // A2DP/AVDTP specification for details【724668210141160†L264-L339】.
    static const uint8_t sbc_capabilities[] = {
        (AVDTP_SBC_44100 | AVDTP_SBC_48000) /* sampling frequencies */ |
        ((uint8_t)AVDTP_SBC_STEREO),        /* channel modes */
        ((AVDTP_SBC_BLOCK_LENGTH_16 << 4) | /* block length */
         (AVDTP_SBC_SUBBANDS_8    << 2) | /* subbands */
         (AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS)), /* allocation method */
        2, 53                                  /* min/max bitpool */
    };

    // Use the same values for the default configuration.  While the API
    // accepts a separate configuration array, providing the capabilities as
    // configuration is acceptable for typical SBC endpoints.
    static const uint8_t sbc_configuration[] = {
        (AVDTP_SBC_44100 /* sampling frequency */ << 4) | AVDTP_SBC_STEREO,
        ((AVDTP_SBC_BLOCK_LENGTH_16 << 4) | (AVDTP_SBC_SUBBANDS_8 << 2) |
         AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS),
        2,
        53
    };

    // Create a local stream endpoint.  The A2DP Source can only send media
    // once at least one stream endpoint has been created【668422887415374†L140-L179】.
    avdtp_stream_endpoint_t* endpoint = a2dp_source_create_stream_endpoint(
        AVDTP_AUDIO, AVDTP_CODEC_SBC,
        sbc_capabilities, sizeof(sbc_capabilities),
        const_cast<uint8_t*>(sbc_configuration), sizeof(sbc_configuration));
    if (endpoint) {
        // Retrieve the local SEP identifier for this endpoint.  It is required
        // for subsequent A2DP API calls such as start/pause/release【36023639349170†L343-L349】.
        local_seid_ = avdtp_local_seid(endpoint);
    }

    // Optionally register delay reporting.  Not strictly necessary for basic
    // A2DP operation, but included here for completeness.  This call may be
    // omitted if your application does not need delay reporting.
    // avdtp_source_register_delay_reporting_category(local_seid_);

    // Initialise the local SDP service if not already done.  Without an SDP
    // record, remote sink devices will not discover our A2DP Source service.
    // We allocate a small buffer for the record and register it with the SDP
    // server.  The supported features are set to Player (0x0001) as a
    // reasonable default【36023639349170†L368-L376】.
    sdp_init();
    static uint8_t a2dp_service_buffer[150];
    memset(a2dp_service_buffer, 0, sizeof(a2dp_service_buffer));
    a2dp_source_create_sdp_record(a2dp_service_buffer,
                                  sdp_create_service_record_handle(),
                                  AVDTP_SOURCE_FEATURE_MASK_PLAYER,
                                  nullptr, nullptr);
    sdp_register_service(a2dp_service_buffer);
    PWB_LOG_MSG("A2DPのSetup完了");
}

void A2DPConnectionPlugin::connect() {
    // ダミー。必要なら何か、要らなければ空でOK
}

void A2DPConnectionPlugin::connect(bd_addr_t address) {
    // Ensure that a baseband connection exists.  If not, initiate one via
    // the BTStackConnectionPlugin.  Note: isConnected() returns true when
    // the underlying ACL link is up; A2DP_Signalling will be established
    // later by a2dp_source_establish_stream().
    PWB_LOG_MSG("A2DP コネクト");
    PWB_SLEEP_MS(2000);
    if (!connection_plugin_->isConnected(address)) {
        connection_plugin_->connectToAddress(address);
    }
    PWB_LOG_MSG("A2DP アドレスコピー");
    PWB_SLEEP_MS(2000);
    bd_addr_copy(remote_addr_, address);
    // Initiate A2DP signalling connection.  The returned a2dp_cid_ will be
    // updated via the signalling connection established subevent【668422887415374†L140-L179】.
    PWB_LOG_MSG("A2DP いまからつなげるよ。");
    PWB_SLEEP_MS(2000);
    uint8_t status = a2dp_source_establish_stream(remote_addr_, &a2dp_cid_);
    if (status != ERROR_CODE_SUCCESS) {
        char msg[64];
        snprintf(msg, sizeof(msg), "A2DP connect failed: %d\n", status);
        PWB_LOG_MSG(msg);
        PWB_SLEEP_MS(2000);
    } else {
        PWB_LOG_MSG("A2DP connect 完了");
        PWB_SLEEP_MS(2000);
    }
}

void A2DPConnectionPlugin::loop() {
    // ダミー。必要なら何か、要らなければ空でOK
}

void A2DPConnectionPlugin::disconnect() {
    // Release the A2DP stream if it is active.  This will disconnect the
    // signalling and media channels and notify the remote sink.  After
    // releasing, the a2dp_cid_ is reset to zero when the corresponding
    // signalling connection released subevent arrives【668422887415374†L176-L179】.
    if (a2dp_cid_ != 0) {
        a2dp_source_disconnect(a2dp_cid_);
    }
}

bool A2DPConnectionPlugin::isConnected() {
    return is_connected_;
}

void A2DPConnectionPlugin::setOnA2dpConnectedCallback(std::function<void(const A2DPConnectInfo&)> callback) {
    on_a2dp_connected_callback_ = std::move(callback);
}

void A2DPConnectionPlugin::setOnA2dpDisconnectedCallback(std::function<void(bd_addr_t)> callback) {
    on_a2dp_disconnected_callback_ = std::move(callback);
}

void A2DPConnectionPlugin::packetHandler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    char msg[64];
    PWB_LOG_MSG("hci_a2dp イベントキターーー！！");
    PWB_SLEEP_MS(2000);
    // Filter for A2DP meta events
    if (!s_instance ) {
        PWB_LOG_MSG("しかしインスタンスが無い。");
        PWB_SLEEP_MS(2000);
        return;
    } 
    if(packet_type != HCI_EVENT_PACKET) {
        PWB_LOG_MSG("しかし HCI_EVENT_PACKET ではない。");
        PWB_SLEEP_MS(2000);
        return;
    } 
    if(hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) {
        PWB_LOG_MSG("しかし HCI_EVENT_A2DP_META パケットでない。");
        PWB_SLEEP_MS(2000);
        return;
    }
    uint8_t status;
    bd_addr_t address;
    switch (hci_event_a2dp_meta_get_subevent_code(packet)) {
        case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED: {
            PWB_LOG_MSG("接続が確立 A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED");
            PWB_SLEEP_MS(2000);
            // 接続が確立された場合
            // Bluetoothデバイスとの接続が成功したことを示します。このイベントが発生すると、接続の詳細（デバイスのアドレス、接続IDなど）が表示されます。
            a2dp_subevent_signaling_connection_established_get_bd_addr(packet, address);
            s_instance->a2dp_cid_ = a2dp_subevent_signaling_connection_established_get_a2dp_cid(packet);
            status = a2dp_subevent_signaling_connection_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS)
            {
                snprintf(msg, sizeof(msg), "A2DP: failed, status 0x%02x, cid 0x%02x", status, s_instance->a2dp_cid_);
                PWB_LOG_MSG(msg);
                PWB_SLEEP_MS(2000);
                s_instance->a2dp_cid_ = 0;
                break;
            }
            break;
        }
        case A2DP_SUBEVENT_STREAM_ESTABLISHED: {
            // Stream established event: connection is up.  Extract BD addr
            // and update status.  a2dp_cid_ is set via the signalling
            // connection established subevent below.
            a2dp_subevent_stream_established_get_bd_addr(packet, address);
            status = a2dp_subevent_stream_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS)
            {
                snprintf(msg, sizeof(msg), "A2DP Source: Stream failed, status 0x%02x.", status);
                PWB_LOG_MSG(msg);
                PWB_SLEEP_MS(2000);
                break;
            }            
            s_instance->is_connected_ = true;
            bd_addr_copy(s_instance->remote_addr_, address);
            a2dp_source_stream_endpoint_request_can_send_now(s_instance->a2dp_cid_, s_instance->local_seid_);
            if (s_instance->on_a2dp_connected_callback_) {
                A2DPConnectInfo info;
                memcpy(info.remote_addr, address, sizeof(bd_addr_t));
                info.a2dp_cid = s_instance->a2dp_cid_;
                info.local_seid = s_instance->local_seid_;
                // 必要なら他フィールドもセット
                s_instance->on_a2dp_connected_callback_(info);
            }
            break;
        }
        // ★★ 追加：ストリーム開始時にも「送ってOK」を再要求すると滑らか
        case A2DP_SUBEVENT_STREAM_STARTED: {
            a2dp_source_stream_endpoint_request_can_send_now(s_instance->a2dp_cid_, s_instance->local_seid_);
            break;
        }
        // ★★ 追加：『今、メディア送信していいよ』の通知
        case A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW: {
            // Transport に送信可能フラグを立てる
            if (s_instance->transport_) {
                s_instance->transport_->onCanSendNow();
            }
            break;
        }
        case A2DP_SUBEVENT_STREAM_RELEASED: {
            // Stream released event: remote has closed the stream
            s_instance->is_connected_ = false;
            // 送信可能フラグを落としておく（メソッドがあるなら）
            if (s_instance->transport_) {
                s_instance->transport_->clearCanSend();
            }
            if (s_instance->on_a2dp_disconnected_callback_) {
                s_instance->on_a2dp_disconnected_callback_(s_instance->remote_addr_);
            }
            break;
        }
        case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED: {
            // Reset the cached A2DP CID.  At this point, no A2DP commands
            // should be sent until a new signalling connection is opened.
            s_instance->a2dp_cid_ = 0;
            break;
        }
        default:
            // Other A2DP subevents (e.g., capabilities, delay reporting) are
            // ignored in this simple plugin.
            break;
    }
}
