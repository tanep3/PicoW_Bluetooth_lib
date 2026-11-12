#!/usr/bin/env python3
"""Hash-pinned Sink-only acceptor guard. See tools/NOTICE.md for BTstack excerpts.
Installed SDK remains unchanged; retain its complete notice in generated output.
"""
import hashlib
from pathlib import Path
import sys

EXPECTED = 'c0627c7d95107c9303e046c108069ddfe8e52c22fe913a07f6ef4fbf1e27dd38'
GUARD = r'''
// PicoW public Sink: inspect complete raw configuration before SDK unpacking.
// Source endpoints deliberately retain the original SDK behavior.
static uint8_t picow_sink_raw_sbc(const uint8_t *p){
    const uint8_t masks[] = { (uint8_t)(p[0] & 0xf0), (uint8_t)(p[0] & 0x0f),
        (uint8_t)(p[1] & 0xf0), (uint8_t)(p[1] & 0x0c), (uint8_t)(p[1] & 3) };
    const uint8_t errors[] = { CODEC_SPECIFIC_ERROR_CODE_INVALID_SAMPLING_FREQUENCY,
        CODEC_SPECIFIC_ERROR_CODE_INVALID_CHANNEL_MODE, CODEC_SPECIFIC_ERROR_CODE_INVALID_BLOCK_LENGTH,
        CODEC_SPECIFIC_ERROR_CODE_INVALID_SUBBANDS, CODEC_SPECIFIC_ERROR_CODE_INVALID_ALLOCATION_METHOD };
    unsigned i;
    for (i = 0; i < sizeof(masks); ++i){
        if (masks[i] == 0 || (masks[i] & (masks[i] - 1)) != 0) return errors[i];
    }
    if ((p[0] & 0x30) == 0) return CODEC_SPECIFIC_ERROR_CODE_NOT_SUPPORTED_SAMPLING_FREQUENCY;
    if (p[2] < 2 || p[2] > 250) return CODEC_SPECIFIC_ERROR_CODE_INVALID_MINIMUM_BITPOOL_VALUE;
    if (p[3] < 2 || p[3] > 250 || p[2] > p[3]) return CODEC_SPECIFIC_ERROR_CODE_INVALID_MAXIMUM_BITPOOL_VALUE;
    if (p[2] > 86 || p[3] > 86) return AVDTP_ERROR_CODE_UNSUPPORTED_CONFIGURATION;
    // Match the public decoder's exact byte-rounded minimum-pool feasibility.
    const unsigned rate = (p[0] & 0x20) ? 44100u : 48000u;
    const unsigned mode = p[0] & 15, bands = (p[1] & 8) ? 4u : 8u;
    const unsigned blocks = (p[1] & 0x80) ? 4u : (p[1] & 0x40) ? 8u : (p[1] & 0x20) ? 12u : 16u;
    const unsigned channels = mode == 8 ? 1u : 2u;
    const unsigned ceiling = (mode == 8 || mode == 4) ? 16u * bands : 32u * bands;
    const unsigned bits = (mode == 8 || mode == 4) ? blocks * channels * p[2] : blocks * p[2] + (mode == 1 ? bands : 0u);
    const unsigned bytes = 4u + bands * channels / 2u + (bits + 7u) / 8u;
    if (p[2] > ceiling || bytes * 8u * rate > (channels == 1 ? 320000u : 512000u) * blocks * bands)
        return AVDTP_ERROR_CODE_UNSUPPORTED_CONFIGURATION;
    return 0;
}

static bool picow_sink_raw_configuration(avdtp_connection_t *connection, avdtp_stream_endpoint_t *endpoint,
        int offset, uint16_t size, bool reconfigure){
    if (endpoint->sep.type != AVDTP_SINK) return true;
    uint8_t error = 0, category = AVDTP_MEDIA_CODEC;
    uint16_t seen = 0;
    const uint8_t *command = connection->acceptor_signaling_packet.command;
    if (offset < 0 || offset > size || size > sizeof(connection->acceptor_signaling_packet.command)){
        error = AVDTP_ERROR_CODE_BAD_LENGTH;
    }
    while (error == 0 && offset < size){
        category = command[offset];
        if (size - offset < 2){ error = AVDTP_ERROR_CODE_BAD_LENGTH; break; }
        const unsigned length = command[offset + 1];
        offset += 2;
        if (length > (unsigned)(size - offset)){ error = AVDTP_ERROR_CODE_BAD_LENGTH; break; }
        if (category == 0 || category > AVDTP_DELAY_REPORTING){ error = AVDTP_ERROR_CODE_BAD_SERV_CATEGORY; break; }
        if (seen & (1u << category)){ error = AVDTP_ERROR_CODE_INVALID_CAPABILITIES; break; }
        seen |= 1u << category;
        if (category == AVDTP_MEDIA_CODEC){
            if (length != 6){ error = AVDTP_ERROR_CODE_BAD_LENGTH; break; }
            // Media type is upper nibble; reserved low nibble is ignored per SDK.
            if ((command[offset] >> 4) != AVDTP_AUDIO){ error = AVDTP_ERROR_CODE_UNSUPPORTED_CONFIGURATION; break; }
            if (command[offset + 1] != AVDTP_CODEC_SBC){ error = CODEC_SPECIFIC_ERROR_CODE_NOT_SUPPORTED_CODEC_TYPE; break; }
            error = picow_sink_raw_sbc(command + offset + 2);
        } else if (category == AVDTP_MEDIA_TRANSPORT || category == AVDTP_DELAY_REPORTING){
            if (reconfigure){ error = AVDTP_ERROR_CODE_INVALID_CAPABILITIES; break; }
            if (length != 0){
                error = category == AVDTP_MEDIA_TRANSPORT ? AVDTP_ERROR_CODE_BAD_MEDIA_TRANSPORT_FORMAT : AVDTP_ERROR_CODE_BAD_LENGTH;
                break;
            }
        } else {
            // Public Sink advertises only transport, SBC and optional delay reporting.
            // Do not expose SDK unpackers for unsupported recovery/mux/CP categories.
            error = AVDTP_ERROR_CODE_UNSUPPORTED_CONFIGURATION;
        }
        offset += length;
    }
    if (error == 0 && !(seen & (1u << AVDTP_MEDIA_CODEC))){
        category = AVDTP_MEDIA_CODEC; error = AVDTP_ERROR_CODE_INVALID_CAPABILITIES;
    }
    if (error == 0 && !reconfigure && !(seen & (1u << AVDTP_MEDIA_TRANSPORT))){
        category = AVDTP_MEDIA_TRANSPORT; error = AVDTP_ERROR_CODE_INVALID_CAPABILITIES;
    }
    if (error == 0) return true;
    connection->error_code = error;
    connection->reject_service_category = category;
    connection->reject_signal_identifier = connection->acceptor_signaling_packet.signal_identifier;
    connection->acceptor_connection_state = AVDTP_SIGNALING_CONNECTION_ACCEPTOR_W2_REJECT_CATEGORY_WITH_ERROR_CODE;
    return false;
}
'''

def replace_once(source, old, new):
    if source.count(old) != 1:
        raise SystemExit('Pinned SDK source layout mismatch; review required')
    return source.replace(old, new, 1)

def generate(source):
    if hashlib.sha256(source).hexdigest() != EXPECTED:
        raise SystemExit('Unsupported avdtp_acceptor.c SHA256; installed SDK must not be modified')
    text = source.decode()
    # Optional public START support is target-local and never gates Source.
    declarations = """
#if defined(ENABLE_AVDTP_ACCEPTOR_EXPLICIT_START_STREAM_CONFIRMATION) && !defined(PICOW_A2DP_SINK_START_DECISIONS)
#error "Use picow_enable_sink_start_decisions; global explicit START changes Source behavior"
#endif
#ifdef PICOW_A2DP_SINK_START_DECISIONS
extern bool picow_sink_start_confirmation_required(avdtp_connection_t *, avdtp_stream_endpoint_t *);
#endif
"""
    text = replace_once(text, 'static int avdtp_acceptor_send_accept_response', declarations + '\nstatic int avdtp_acceptor_send_accept_response')
    old = '                    stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W4_USER_CONFIRM_START_STREAM;'
    text = replace_once(text, old, """                    stream_endpoint->acceptor_config_state =
                        (stream_endpoint->sep.type == AVDTP_SINK &&
                         picow_sink_start_confirmation_required(connection, stream_endpoint)) ?
                            AVDTP_ACCEPTOR_W4_USER_CONFIRM_START_STREAM : AVDTP_ACCEPTOR_W2_ACCEPT_START_STREAM;""")
    old = '    int request_to_send = 1;\n    switch (stream_endpoint->acceptor_config_state){'
    text = replace_once(text, old, """#ifdef PICOW_A2DP_SINK_START_DECISIONS
    // A normal CLOSE/ABORT may cancel a Sink START awaiting application input.
    // Reuse only the SDK's existing OPENED cancellation branches; no other
    // pending state, endpoint owner, role or signal gains admission here.
    if (stream_endpoint->sep.type == AVDTP_SINK && stream_endpoint->connection == connection &&
        stream_endpoint->state == AVDTP_STREAM_ENDPOINT_OPENED &&
        stream_endpoint->acceptor_config_state == AVDTP_ACCEPTOR_W4_USER_CONFIRM_START_STREAM &&
        (connection->acceptor_signaling_packet.signal_identifier == AVDTP_SI_CLOSE ||
         connection->acceptor_signaling_packet.signal_identifier == AVDTP_SI_ABORT)){
        stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_STREAM_CONFIG_IDLE;
    }
#endif
""" + old)
    old = '            avdtp_acceptor_send_response_reject(cid, AVDTP_SI_START, trid);'
    text = replace_once(text, old, """            if (stream_endpoint->sep.type == AVDTP_SINK){
                // AVDTP 1.3 8.13.3: failed ACP SEID and error, not bare reject.
                avdtp_acceptor_send_response_reject_service_category(cid, AVDTP_SI_START,
                    (uint8_t)(avdtp_local_seid(stream_endpoint) << 2), AVDTP_ERROR_CODE_BAD_STATE, trid);
            } else {
""" + old + """
            }""")
    text = replace_once(text, 'static int avdtp_acceptor_send_accept_response', GUARD + '\nstatic int avdtp_acceptor_send_accept_response')
    old = '                        case AVDTP_CONFIGURATION_STATE_IDLE:\n                            avdtp_acceptor_handle_configuration_command'
    text = replace_once(text, old, '                        case AVDTP_CONFIGURATION_STATE_IDLE:\n                            if (!picow_sink_raw_configuration(connection, stream_endpoint, offset + 1, packet_size, false)) break;\n                            avdtp_acceptor_handle_configuration_command')
    old = '                case AVDTP_SI_RECONFIGURE:{\n                    stream_endpoint->acceptor_config_state'
    text = replace_once(text, old, '                case AVDTP_SI_RECONFIGURE:{\n                    if (!picow_sink_raw_configuration(connection, stream_endpoint, offset, packet_size, true)) break;\n                    stream_endpoint->acceptor_config_state')
    old = '                    // find sep or raise error'
    addition = '''                    // SDK originally omitted application validation on RECONFIGURE.
                    // Only Sink changes; Source retains its original callback behavior.
                    if (stream_endpoint->sep.type == AVDTP_SINK &&
                        (sep.configured_service_categories & (1u << AVDTP_MEDIA_CODEC)) != 0){
                        uint8_t error = avdtp_validate_media_configuration(stream_endpoint, connection->avdtp_cid, 1, &sep.configuration.media_codec);
                        if (error != 0){
                            connection->error_code = error;
                            connection->reject_service_category = AVDTP_MEDIA_CODEC;
                            connection->reject_signal_identifier = connection->acceptor_signaling_packet.signal_identifier;
                            stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W2_REJECT_CATEGORY_WITH_ERROR_CODE;
                            break;
                        }
                    }

'''
    text = replace_once(text, old, addition + old)
    # Recover only a normalized application veto, not arbitrary SDK errors.
    begin = text.index('static void\navdtp_acceptor_handle_configuration_command')
    end = text.index('\nvoid avdtp_acceptor_stream_config_subsm(', begin)
    handler = text[begin:end].replace('static void\n', 'static bool\n', 1)
    handler = handler.replace('        return;', '        return false;')
    closing = handler.rfind('}')
    handler = handler[:closing] + '    return false;\n' + handler[closing:]
    text = text[:begin] + handler + text[end:]
    old = '    stream_endpoint->state = AVDTP_STREAM_ENDPOINT_CONFIGURATION_SUBSTATEMACHINE;'
    text = replace_once(text, old, """    const avdtp_stream_endpoint_state_t previous_state = stream_endpoint->state;
    avdtp_connection_t * const previous_connection = stream_endpoint->connection;
""" + old)
    old = '                connection->error_code              = error_code;'
    text = replace_once(text, old, old + """
                if (stream_endpoint->sep.type == AVDTP_SINK){
                    // The validator saw the SDK's normal temporary ownership.
                    // Restore only SDK lifecycle writes; stored configuration has
                    // not been committed yet. Keep rejection on the requesting
                    // connection, even when the prior endpoint owner is another peer.
                    stream_endpoint->state = previous_state;
                    stream_endpoint->connection = previous_connection;
                    connection->reject_signal_identifier = connection->acceptor_signaling_packet.signal_identifier;
                    stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W2_REJECT_CATEGORY_WITH_ERROR_CODE;
                    return true;
                }""")
    old = '                            avdtp_acceptor_handle_configuration_command(connection, offset, packet_size,\n                                                                        stream_endpoint);'
    text = replace_once(text, old, '                            if (avdtp_acceptor_handle_configuration_command(connection, offset, packet_size,\n                                                                        stream_endpoint)) break;')
    old = '    avdtp_acceptor_stream_endpoint_state_t acceptor_config_state = stream_endpoint->acceptor_config_state;'
    text = replace_once(text, old, """    // A restored Sink owner must not redirect this SET rejection to another peer.
    // Retain endpoint rejection dispatch and its normal CID/SEID notification.
    if (stream_endpoint->sep.type == AVDTP_SINK &&
        stream_endpoint->acceptor_config_state == AVDTP_ACCEPTOR_W2_REJECT_CATEGORY_WITH_ERROR_CODE &&
        reject_signal_identifier == AVDTP_SI_SET_CONFIGURATION){
        cid = connection->l2cap_signaling_cid;
        trid = connection->acceptor_transaction_label;
    }
""" + old)
    old = '                            stream_endpoint->sep.configuration.media_codec = stream_endpoint->remote_configuration.media_codec;'
    return replace_once(text, old, old + "\n                            if (stream_endpoint->sep.type == AVDTP_SINK){\n                                stream_endpoint->remote_sep.configuration.media_codec.media_codec_information = stream_endpoint->media_codec_configuration_info;\n                            }")

if __name__ == '__main__':
    source, output = map(Path, sys.argv[1:])
    result = generate(source.read_bytes())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(result)
