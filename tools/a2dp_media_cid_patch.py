#!/usr/bin/env python3
"""Host-only experiment: correlate media channels by their reserved local CID.

The importer must validate the original SDK source hash before applying this
fragment. The standalone CLI does that check. Not enabled in production CMake.
Generated source retains its original BTstack notices and license restrictions.
"""
import hashlib
import pathlib
import sys

EXPECTED = {
    "avdtp.c": "03fa7da1fd9245141bffcaf02e6aae47e567e6635216aba41924112b7b4d051f",
    "avdtp_initiator.c": "e69cf88403f7d6e11fc88de42fb79531f4dd7b50bf17c714de0f819516dc810e",
    "avdtp_acceptor.c": "c0627c7d95107c9303e046c108069ddfe8e52c22fe913a07f6ef4fbf1e27dd38",
}

def patch_media(name: str, text: str) -> str:
    def replace(old, new):
        nonlocal text
        if text.count(old) != 1:
            raise ValueError("Media CID overlay anchor missing or ambiguous: " + old[:80])
        text = text.replace(old, new)

    if name == "avdtp.c":
        start = text.index("static avdtp_stream_endpoint_t * avdtp_get_stream_endpoint_for_signaling_cid(")
        end = text.index("uint8_t avdtp_get_next_transaction_label", start)
        text = text[:start] + '''// An incoming media L2CAP request carries no SEID. Accept it only when
// precisely one remote OPEN is waiting for an unassigned channel.
static avdtp_stream_endpoint_t * avdtp_get_pending_incoming_media_endpoint(avdtp_connection_t * connection){
    avdtp_stream_endpoint_t * pending = NULL;
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, avdtp_get_stream_endpoints());
    while (btstack_linked_list_iterator_has_next(&it)){
        avdtp_stream_endpoint_t * endpoint = (avdtp_stream_endpoint_t *) btstack_linked_list_iterator_next(&it);
        if (endpoint->connection != connection || endpoint->l2cap_media_cid != 0 ||
            endpoint->state != AVDTP_STREAM_ENDPOINT_W4_L2CAP_FOR_MEDIA_CONNECTED) continue;
        if (pending != NULL) return NULL;
        pending = endpoint;
    }
    return pending;
}

''' + text[end:]
        replace('''                        if ((connection == NULL) || (connection->configuration_state != AVDTP_CONFIGURATION_STATE_REMOTE_CONFIGURED)) {
                            decline_connection = true;
                        } else {
                            // now, we're only dealing with media connections that are created by remote side - we're acceptor here
                            stream_endpoint = avdtp_get_stream_endpoint_for_seid(connection->acceptor_local_seid);
                            if ((stream_endpoint == NULL) || (stream_endpoint->l2cap_media_cid != 0) ) {
                                decline_connection = true;
                            }
                        }
''', '''                        stream_endpoint = avdtp_get_pending_incoming_media_endpoint(connection);
                        if (stream_endpoint == NULL || local_cid == 0){
                            decline_connection = true;
                        } else {
                            // Reserve before accepting. Later commands may change
                            // acceptor_local_seid; they must not change this mapping.
                            stream_endpoint->l2cap_media_cid = local_cid;
                        }
''')
        replace('''                            stream_endpoint = avdtp_get_stream_endpoint_for_signaling_cid(connection->l2cap_signaling_cid);
                            if (!stream_endpoint){
                                log_info("L2CAP_EVENT_CHANNEL_OPENED: stream_endpoint not found for signaling cid 0x%02x", connection->l2cap_signaling_cid);
                                return;
                            }
''', '''                            stream_endpoint = avdtp_get_stream_endpoint_for_l2cap_cid(local_cid);
                            if (local_cid == 0 || !stream_endpoint || stream_endpoint->connection != connection ||
                                stream_endpoint->l2cap_media_cid != local_cid){
                                log_info("L2CAP_EVENT_CHANNEL_OPENED: no reserved media endpoint for cid 0x%02x", local_cid);
                                if (status == ERROR_CODE_SUCCESS && local_cid != 0 && local_cid != connection->l2cap_signaling_cid) l2cap_disconnect(local_cid);
                                return;
                            }
''')
        replace('''                                stream_endpoint->state = AVDTP_STREAM_ENDPOINT_IDLE;
                                avdtp_streaming_emit_connection_established(stream_endpoint, status);
''', '''                                stream_endpoint->state = AVDTP_STREAM_ENDPOINT_CONFIGURED;
                                stream_endpoint->l2cap_media_cid = 0;
                                avdtp_streaming_emit_connection_established(stream_endpoint, status);
''')
    elif name == "avdtp_initiator.c":
        replace('''                    l2cap_create_channel(avdtp_packet_handler, connection->remote_addr, BLUETOOTH_PSM_AVDTP, AVDTP_L2CAP_MTU, NULL);
                    return;
''', '''                    // Reserve the pending media CID for this exact endpoint.
                    // L2CAP assigns it before it can report CHANNEL_OPENED.
                    stream_endpoint->l2cap_media_cid = 0;
                    uint8_t media_status = l2cap_create_channel(avdtp_packet_handler, connection->remote_addr,
                        BLUETOOTH_PSM_AVDTP, AVDTP_L2CAP_MTU, &stream_endpoint->l2cap_media_cid);
                    if (media_status != ERROR_CODE_SUCCESS){
                        stream_endpoint->l2cap_media_cid = 0;
                        stream_endpoint->state = AVDTP_STREAM_ENDPOINT_CONFIGURED;
                        avdtp_streaming_emit_connection_established(stream_endpoint, media_status);
                    }
                    return;
''')
    elif name == "avdtp_acceptor.c":
        replace('''                    log_info("AVDTP_STREAM_ENDPOINT_W2_ANSWER_OPEN_STREAM");
                    stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W2_ACCEPT_OPEN_STREAM;
''', '''                    // Media L2CAP has no SEID. Keep only one remote OPEN
                    // without a reserved CID; otherwise its channel is ambiguous.
                    bool media_open_pending = false;
                    btstack_linked_list_iterator_t media_it;
                    btstack_linked_list_iterator_init(&media_it, avdtp_get_stream_endpoints());
                    while (btstack_linked_list_iterator_has_next(&media_it)){
                        avdtp_stream_endpoint_t * other = (avdtp_stream_endpoint_t *) btstack_linked_list_iterator_next(&media_it);
                        if (other != stream_endpoint && other->connection == connection &&
                            other->state == AVDTP_STREAM_ENDPOINT_W4_L2CAP_FOR_MEDIA_CONNECTED && other->l2cap_media_cid == 0){
                            media_open_pending = true;
                            break;
                        }
                    }
                    if (media_open_pending){
                        stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W2_REJECT_WITH_ERROR_CODE;
                        connection->error_code = AVDTP_ERROR_CODE_BAD_STATE;
                        connection->reject_signal_identifier = connection->acceptor_signaling_packet.signal_identifier;
                        break;
                    }
                    log_info("AVDTP_STREAM_ENDPOINT_W2_ANSWER_OPEN_STREAM");
                    stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W2_ACCEPT_OPEN_STREAM;
''')
    else:
        raise ValueError("Unsupported media CID source: " + name)
    return text

if __name__ == "__main__":
    source, destination = map(pathlib.Path, sys.argv[1:])
    raw = source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXPECTED.get(source.name):
        raise SystemExit("Unrecognized media CID source SHA256; review overlay first")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(patch_media(source.name, raw.decode()))
