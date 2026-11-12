#!/usr/bin/env python3
"""Pinned experimental AVDTP initiator ownership and response validation.

The composer must verify each original SDK hash before calling this fragment.
The standalone CLI pins the same sources. This deliberately rejects fragmented
responses without modifying the pending transaction: it is not a reassembler.
Original BTstack source notices and licenses remain in generated output.
"""
import hashlib
import pathlib
import sys

EXPECTED = {
    'avdtp.c': '03fa7da1fd9245141bffcaf02e6aae47e567e6635216aba41924112b7b4d051f',
    'avdtp_initiator.c': 'e69cf88403f7d6e11fc88de42fb79531f4dd7b50bf17c714de0f819516dc810e',
    'avdtp_sink.c': '9b1da7637204aeabfce0757c290056228e9417f1d597afa47985d954a45dc4e2',
    'avdtp_acceptor.c': 'c0627c7d95107c9303e046c108069ddfe8e52c22fe913a07f6ef4fbf1e27dd38',
}

BUSY = '''// Experimental: one signaling transaction owns the initiator's SEID,
// label and packet buffer until its exact response has drained. Include W2 and
// queued flags: a second API must not overwrite ownership before CAN_SEND_NOW.
bool picow_avdtp_initiator_busy(const avdtp_connection_t *connection){
    if (connection->initiator_connection_state != AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE) return true;
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, avdtp_get_stream_endpoints());
    while (btstack_linked_list_iterator_has_next(&it)){
        const avdtp_stream_endpoint_t *endpoint = (const avdtp_stream_endpoint_t *) btstack_linked_list_iterator_next(&it);
        if (endpoint->connection != connection && connection->active_stream_endpoint != endpoint) continue;
        if (endpoint->initiator_config_state != AVDTP_INITIATOR_STREAM_CONFIG_IDLE ||
            endpoint->start_stream || endpoint->close_stream || endpoint->abort_stream || endpoint->suspend_stream) return true;
    }
    return false;
}

// Validate before avdtp_read_signaling_header overwrites the stored command.
// Unsupported fragments leave W4 quarantined. No partial response may release
// ownership or be mistaken for a complete ACK of another role's command.
static bool picow_avdtp_expected_response(avdtp_connection_t *connection, const uint8_t *packet, uint16_t size){
    if (size < 2 || ((packet[0] >> 2) & 3) != AVDTP_SINGLE_PACKET || (packet[1] & 0xc0) != 0) return false;
    const uint8_t type = packet[0] & 3;
    const uint8_t signal = packet[1] & 0x3f;
    if (type == AVDTP_CMD_MSG || (packet[0] >> 4) != (connection->initiator_transaction_label & 15) ||
        signal != connection->initiator_signaling_packet.signal_identifier) return false;
    if (connection->initiator_connection_state != AVDTP_SIGNALING_CONNECTION_INITIATOR_W4_ANSWER){
        if (connection->initiator_connection_state != AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE) return false;
        avdtp_stream_endpoint_t *endpoint = avdtp_get_stream_endpoint_for_seid(connection->initiator_local_seid);
        if (endpoint == NULL || endpoint->initiator_config_state != AVDTP_INITIATOR_W4_ANSWER ||
            (endpoint->connection != connection && connection->active_stream_endpoint != endpoint)) return false;
    }
    if (type == AVDTP_GENERAL_REJECT_MSG) return size == 2;
    if (type == AVDTP_RESPONSE_REJECT_MSG){
        const bool has_category_or_seid = signal == AVDTP_SI_SET_CONFIGURATION || signal == AVDTP_SI_RECONFIGURE ||
                                         signal == AVDTP_SI_START || signal == AVDTP_SI_SUSPEND;
        return size == (has_category_or_seid ? 4 : 3);
    }
    if (type != AVDTP_RESPONSE_ACCEPT_MSG) return false;
    switch (signal){
        case AVDTP_SI_DISCOVER: return ((size - 2) & 1) == 0;
        case AVDTP_SI_GET_CAPABILITIES:
        case AVDTP_SI_GET_ALL_CAPABILITIES:
        case AVDTP_SI_GET_CONFIGURATION: {
            // The SDK consumes complete capability TLVs, never partial ones.
            uint16_t offset = 2;
            while (offset < size){
                if (size - offset < 2 || packet[offset + 1] > size - offset - 2) return false;
                const uint8_t category = packet[offset], length = packet[offset + 1];
                switch (category){
                    case AVDTP_MEDIA_TRANSPORT: case AVDTP_REPORTING: case AVDTP_DELAY_REPORTING:
                        if (length != 0) return false;
                        break;
                    case AVDTP_RECOVERY: if (length != 3) return false; break;
                    // Pinned SDK does not copy CP-specific value bytes before
                    // emitting them. Restrict this experiment to type-only CP.
                    case AVDTP_CONTENT_PROTECTION: if (length != 2) return false; break;
                    case AVDTP_HEADER_COMPRESSION: if (length != 1) return false; break;
                    // This SDK reads three TSID/TCID pairs, regardless of length.
                    case AVDTP_MULTIPLEXING: if (length != 7) return false; break;
                    case AVDTP_MEDIA_CODEC: {
                        if (length < 2) return false;
                        uint8_t minimum = 0;
                        switch (packet[offset + 3]){
                            case AVDTP_CODEC_SBC: minimum = 4; break;
                            // The pinned emitter reads info[4]. Standard MPEG
                            // audio's four-byte form is therefore unsupported.
                            case AVDTP_CODEC_MPEG_1_2_AUDIO: minimum = 5; break;
                            case AVDTP_CODEC_MPEG_2_4_AAC: minimum = 6; break;
                            case AVDTP_CODEC_ATRAC_FAMILY: minimum = 6; break;
                            case AVDTP_CODEC_MPEG_D_USAC: minimum = 7; break;
                            default: break;
                        }
                        if (length - 2 < minimum) return false;
                        break;
                    }
                    default: return false;
                }
                offset += 2 + length;
            }
            return true;
        }
        case AVDTP_SI_SET_CONFIGURATION: case AVDTP_SI_RECONFIGURE:
        case AVDTP_SI_OPEN: case AVDTP_SI_START: case AVDTP_SI_CLOSE:
        case AVDTP_SI_SUSPEND: case AVDTP_SI_ABORT: case AVDTP_SI_DELAYREPORT: return size == 2;
        default: return false;
    }
}

'''

def patch_transactions(name: str, text: str) -> str:
    def replace(old, new):
        nonlocal text
        if text.count(old) != 1:
            raise ValueError('Transaction overlay anchor missing or ambiguous: ' + old[:90])
        text = text.replace(old, new)

    if name == 'avdtp.c':
        replace('static void\nhandle_l2cap_data_packet_for_signaling_connection(', BUSY + 'static void\nhandle_l2cap_data_packet_for_signaling_connection(')
        replace('''        default:
                offset = avdtp_read_signaling_header(&connection->initiator_signaling_packet, packet, size);''', '''        default:
            if (!picow_avdtp_expected_response(connection, packet, size)) return;
            offset = avdtp_read_signaling_header(&connection->initiator_signaling_packet, packet, size);''')
        for name in ('open_stream', 'start_stream', 'stop_stream', 'abort_stream', 'suspend_stream',
                     'discover_stream_endpoints', 'get_capabilities', 'get_all_capabilities',
                     'get_configuration', 'set_configuration', 'reconfigure'):
            start = text.index('uint8_t avdtp_' + name + '(')
            position = text.index('    }', text.index('    if (!connection){', start)) + len('    }')
            text = text[:position] + '''
    if (picow_avdtp_initiator_busy(connection)) return ERROR_CODE_COMMAND_DISALLOWED;''' + text[position:]
    elif name == 'avdtp_initiator.c':
        replace('static int avdtp_initiator_send_signaling_cmd(', '''// Keep the expected signal until the wire parser validates the response.
static void picow_avdtp_expect_signal(uint16_t cid, avdtp_signal_identifier_t signal){
    avdtp_connection_t *connection = avdtp_get_connection_for_l2cap_signaling_cid(cid);
    if (connection != NULL) connection->initiator_signaling_packet.signal_identifier = signal;
}

static int avdtp_initiator_send_signaling_cmd(''')
        replace('''    uint8_t command[2];
    command[0]''', '''    picow_avdtp_expect_signal(cid, identifier);
    uint8_t command[2];
    command[0]''')
        replace('''    uint8_t command[3];
    command[0]''', '''    picow_avdtp_expect_signal(cid, identifier);
    uint8_t command[3];
    command[0]''')
        replace('''    uint8_t command[5];
    command[0]''', '''    picow_avdtp_expect_signal(cid, AVDTP_SI_DELAYREPORT);
    uint8_t command[5];
    command[0]''')
        replace('''void avdtp_initiator_stream_config_subsm_handle_can_send_now_signaling(avdtp_connection_t *connection) {
    bool sent;''', '''void avdtp_initiator_stream_config_subsm_handle_can_send_now_signaling(avdtp_connection_t *connection) {
    // run_signaling returns false while W4, which must NOT fall through and
    // start an endpoint command on the same signaling channel.
    if (connection->initiator_connection_state == AVDTP_SIGNALING_CONNECTION_INITIATOR_W4_ANSWER) return;
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, avdtp_get_stream_endpoints());
    while (btstack_linked_list_iterator_has_next(&it)){
        avdtp_stream_endpoint_t *endpoint = (avdtp_stream_endpoint_t *) btstack_linked_list_iterator_next(&it);
        if (endpoint->connection != connection && connection->active_stream_endpoint != endpoint) continue;
        if (endpoint->initiator_config_state == AVDTP_INITIATOR_W4_ANSWER) return;
        // The original owner may continue sending its fragmented command.
        if (endpoint->initiator_config_state == AVDTP_INITIATOR_FRAGMENTATED_COMMAND &&
            endpoint->sep.seid != connection->initiator_local_seid) return;
    }
    bool sent;''')
        # A callback may synchronously start and transmit the next command.
        # Every entry/send path already allocates its next label. Advancing it
        # after emitting ACCEPT corrupts a newly pending response token.
        replace("            connection->initiator_transaction_label = avdtp_get_next_transaction_label();\n            break;\n        case AVDTP_RESPONSE_REJECT_MSG:",
                "            break;\n        case AVDTP_RESPONSE_REJECT_MSG:")
        replace("        case AVDTP_GENERAL_REJECT_MSG:\n            log_info", '''        case AVDTP_GENERAL_REJECT_MSG:
            // A general rejection completes the same transaction as a typed
            // rejection. It must not leave a SET_CONFIGURATION owner reserved.
            if (stream_endpoint != NULL && connection->initiator_signaling_packet.signal_identifier == AVDTP_SI_SET_CONFIGURATION){
                if (connection->active_stream_endpoint == stream_endpoint){
                    connection->configuration_state = AVDTP_CONFIGURATION_STATE_IDLE;
                    connection->active_stream_endpoint = NULL;
                }
                avdtp_reset_stream_endpoint(stream_endpoint);
            } else if (stream_endpoint != NULL && connection->initiator_signaling_packet.signal_identifier == AVDTP_SI_RECONFIGURE){
                stream_endpoint->state = AVDTP_STREAM_ENDPOINT_OPENED;
            }
            log_info''')
    elif name == 'avdtp_sink.c':
        replace('uint8_t avdtp_sink_delay_report(', '''extern bool picow_avdtp_initiator_busy(const avdtp_connection_t *);

uint8_t avdtp_sink_delay_report(''')
        start = text.index('uint8_t avdtp_sink_delay_report(')
        position = text.index('    }', text.index('    if (!connection){', start)) + len('    }')
        text = text[:position] + '''
    if (picow_avdtp_initiator_busy(connection)) return ERROR_CODE_COMMAND_DISALLOWED;''' + text[position:]
    return text

if __name__ == '__main__':
    source, destination = map(pathlib.Path, sys.argv[1:])
    raw = source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXPECTED.get(source.name):
        raise SystemExit('Unrecognized transaction source SHA256; review overlay first')
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(patch_transactions(source.name, raw.decode()))
