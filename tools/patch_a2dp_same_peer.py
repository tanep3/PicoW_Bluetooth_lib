#!/usr/bin/env python3
"""Generate experimental pinned AVDTP source for host/ARM regression.
Only the explicit experimental CMake helper enables it; normal guards remain.
Generated source retains BTstack notices and license restrictions.
"""
import hashlib
import pathlib
import sys
source, destination = map(pathlib.Path, sys.argv[1:])
expected = {
    'a2dp.c': '6f12b99dc2f8d86c39781a3238a465ba1aa4f606be88c836065e2b7796102aa4',
    'avdtp_sink.c': '9b1da7637204aeabfce0757c290056228e9417f1d597afa47985d954a45dc4e2',
    'avdtp.c': '03fa7da1fd9245141bffcaf02e6aae47e567e6635216aba41924112b7b4d051f',
    'avdtp_acceptor.c': 'c0627c7d95107c9303e046c108069ddfe8e52c22fe913a07f6ef4fbf1e27dd38',
    'avdtp_initiator.c': 'e69cf88403f7d6e11fc88de42fb79531f4dd7b50bf17c714de0f819516dc810e',
}
raw = source.read_bytes()
if hashlib.sha256(raw).hexdigest() != expected.get(source.name):
    raise SystemExit('Unrecognized AVDTP SHA256; review same-peer overlay first')
s = raw.decode()
def replace(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit('AVDTP overlay anchor missing or ambiguous: '+old[:80])
    s = s.replace(old, new)
if source.name == 'a2dp.c':
    # The SDK's internal wake-up scan must acquire the singleton once, not
    # restart multiple queued roles and overwrite its discovery owner.
    for role_name in ('SOURCE', 'SINK'):
        line = f'a2dp_config_process_start_discovering_seps(AVDTP_ROLE_{role_name}, next_connection);'
        replace(line, line + '\n            return;')
    replace('if (a2dp_config_process_sep_discovery_cid == cid) {',
            'if (a2dp_config_process_sep_discovery_cid == cid && a2dp_config_process_role == role) {')
    replace('void a2dp_config_process_avdtp_event_handler(avdtp_role_t role, uint8_t *packet, uint16_t size) {', '''void a2dp_config_process_avdtp_event_handler(avdtp_role_t role, uint8_t *packet, uint16_t size) {
    // Discovery responses belong to the singleton owner, not a stale local
    // SEID left by an earlier media control command on this signaling CID.
    bool discovery_response = false;
    if (size == 8 && packet[6] &&
        (packet[2] == AVDTP_SUBEVENT_SIGNALING_ACCEPT ||
         packet[2] == AVDTP_SUBEVENT_SIGNALING_REJECT ||
         packet[2] == AVDTP_SUBEVENT_SIGNALING_GENERAL_REJECT)) {
        discovery_response = packet[7] == AVDTP_SI_DISCOVER ||
            packet[7] == AVDTP_SI_GET_CAPABILITIES || packet[7] == AVDTP_SI_GET_ALL_CAPABILITIES;
        if (discovery_response && a2dp_config_process_sep_discovery_cid == little_endian_read_16(packet, 3) &&
            a2dp_config_process_role != role) return;
    }
    if (!discovery_response && size >= 6 && (packet[2] == AVDTP_SUBEVENT_SIGNALING_ACCEPT ||
                     packet[2] == AVDTP_SUBEVENT_SIGNALING_REJECT ||
                     packet[2] == AVDTP_SUBEVENT_SIGNALING_GENERAL_REJECT) && packet[5] != 0) {
        avdtp_stream_endpoint_t *endpoint = avdtp_get_stream_endpoint_for_seid(packet[5]);
        if (endpoint == NULL ||
            (role == AVDTP_ROLE_SOURCE && endpoint->sep.type != AVDTP_SOURCE) ||
            (role == AVDTP_ROLE_SINK && endpoint->sep.type != AVDTP_SINK)) return;
    }
''')
elif source.name == 'avdtp.c':
    replace('static void avdtp_handle_close_media_channel(', '''// Experimental transaction ownership; endpoint closure must not cancel a
// different SEP's configuration on the shared signaling connection.
void picow_avdtp_release_configuration_owner(avdtp_connection_t *connection,
                                            avdtp_stream_endpoint_t *endpoint) {
    if (connection->active_stream_endpoint != endpoint &&
        (connection->configuration_state == AVDTP_CONFIGURATION_STATE_LOCAL_INITIATED ||
         connection->configuration_state == AVDTP_CONFIGURATION_STATE_REMOTE_INITIATED)) return;
    connection->configuration_state = AVDTP_CONFIGURATION_STATE_IDLE;
    if (connection->active_stream_endpoint == endpoint) connection->active_stream_endpoint = NULL;
}

static void avdtp_handle_close_media_channel(''')
    replace('''    avdtp_reset_stream_endpoint(stream_endpoint);
    connection->configuration_state = AVDTP_CONFIGURATION_STATE_IDLE;
''', '''    avdtp_reset_stream_endpoint(stream_endpoint);
    picow_avdtp_release_configuration_owner(connection, stream_endpoint);
''')
    replace('''    if (connection->configuration_state != AVDTP_CONFIGURATION_STATE_IDLE){
        log_info("configuration already started, config state %u", connection->configuration_state);
        return ERROR_CODE_COMMAND_DISALLOWED;
    }
''', '''    // Experimental: serialize active transactions, not configured endpoints.
    if (connection->configuration_state == AVDTP_CONFIGURATION_STATE_LOCAL_INITIATED ||
        connection->configuration_state == AVDTP_CONFIGURATION_STATE_REMOTE_INITIATED){
        log_info("configuration transaction active, state %u", connection->configuration_state);
        return ERROR_CODE_COMMAND_DISALLOWED;
    }
''')
elif source.name == 'avdtp_acceptor.c':
    replace('#include "l2cap.h"', '''#include "l2cap.h"
extern void picow_avdtp_release_configuration_owner(avdtp_connection_t *, avdtp_stream_endpoint_t *);''')
    start = s.index('                    switch (connection->configuration_state){')
    end = s.index('                    break;\n                }\n                case AVDTP_SI_RECONFIGURE:', start)
    s = s[:start]+'''                    // Experimental: another configured SEP does not own the
                    // entire signaling connection. Keep transactions serialized.
                    if (connection->configuration_state == AVDTP_CONFIGURATION_STATE_LOCAL_INITIATED ||
                        connection->configuration_state == AVDTP_CONFIGURATION_STATE_REMOTE_INITIATED) {
                        connection->error_code = AVDTP_ERROR_CODE_BAD_STATE;
                    } else if (stream_endpoint->state != AVDTP_STREAM_ENDPOINT_IDLE || stream_endpoint->sep.in_use) {
                        connection->error_code = AVDTP_ERROR_CODE_SEP_IN_USE;
                    } else {
                        avdtp_connection_t *previous_connection = stream_endpoint->connection;
                        avdtp_acceptor_handle_configuration_command(connection, offset, packet_size, stream_endpoint);
                        if (connection->error_code == 0) {
                            connection->active_stream_endpoint = stream_endpoint;
                            connection->configuration_state = AVDTP_CONFIGURATION_STATE_REMOTE_INITIATED;
                        } else {
                            // Validator/parser rejection must not consume a free
                            // endpoint or overwrite another completed transaction.
                            stream_endpoint->state = AVDTP_STREAM_ENDPOINT_IDLE;
                            stream_endpoint->connection = previous_connection;
                        }
                        break;
                    }
                    connection->reject_signal_identifier = connection->acceptor_signaling_packet.signal_identifier;
                    connection->reject_service_category = 0;
                    stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W2_REJECT_CATEGORY_WITH_ERROR_CODE;
'''+s[end:]
    replace('''            connection->configuration_state = AVDTP_CONFIGURATION_STATE_REMOTE_CONFIGURED;''', '''            connection->configuration_state = AVDTP_CONFIGURATION_STATE_REMOTE_CONFIGURED;
            if (connection->active_stream_endpoint == stream_endpoint) connection->active_stream_endpoint = NULL;''')
    replace('''            connection->configuration_state = AVDTP_CONFIGURATION_STATE_IDLE;''', '''            picow_avdtp_release_configuration_owner(connection, stream_endpoint);''')
elif source.name == 'avdtp_initiator.c':
    replace('#include "classic/avdtp_initiator.h"', '''#include "classic/avdtp_initiator.h"
extern void picow_avdtp_release_configuration_owner(avdtp_connection_t *, avdtp_stream_endpoint_t *);''')
    replace('''                    connection->configuration_state = AVDTP_CONFIGURATION_STATE_LOCAL_CONFIGURED;''', '''                    connection->configuration_state = AVDTP_CONFIGURATION_STATE_LOCAL_CONFIGURED;
                    if (connection->active_stream_endpoint == stream_endpoint) connection->active_stream_endpoint = NULL;''')
    replace('''                    connection->configuration_state = AVDTP_CONFIGURATION_STATE_IDLE;''', '''                    picow_avdtp_release_configuration_owner(connection, stream_endpoint);''')
    replace('''                    sep.in_use = 0;''', '''                    if (stream_endpoint != NULL) avdtp_reset_stream_endpoint(stream_endpoint);''')
from a2dp_media_cid_patch import patch_media
if source.name in ('avdtp.c', 'avdtp_initiator.c', 'avdtp_acceptor.c'): s = patch_media(source.name, s)
from a2dp_role_lifecycle_patch import patch_role_lifecycle
s = patch_role_lifecycle(source.name, s)
from a2dp_transaction_patch import patch_transactions
s = patch_transactions(source.name, s)
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(s)
