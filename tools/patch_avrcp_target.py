#!/usr/bin/env python3
"""Generate a pinned BTstack Target overlay without modifying the installed SDK.
Upstream excerpts/generated source retain BTstack terms; see docs/avrcp_sdk_overlay.md.
"""
import hashlib
import pathlib
import sys
source, destination = map(pathlib.Path, sys.argv[1:])
raw = source.read_bytes()
expected = '50e51a563da5fab33bbd8bc827534eab08c4c9110fbb59c4187ab3872f99e19f'
if hashlib.sha256(raw).hexdigest() != expected:
    raise SystemExit('Unrecognized avrcp_target.c SHA256; review overlay for this SDK first')
s = raw.decode()
def replace(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit('Target overlay anchor missing/ambiguous: '+old[:80])
    s = s.replace(old,new)
# Reject truncated transport input before reading even its first byte.
replace('    uint8_t avctp_header = packet[0];', '    if (packet == NULL || size < 6u) return;\n    if (packet[5] == AVRCP_CMD_OPCODE_PASS_THROUGH && (connection->state != AVCTP_CONNECTION_OPENED || connection->target_now_playing_info_response || (connection->pdu_id == AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES && connection->data_offset < connection->data_len))) return;\n    uint8_t avctp_header = packet[0];')
# Policy owns the only pass-through response; do not call the SDK's auto-ACK
# event path. The callback consumes a bounded copied request synchronously.
replace('static void avrcp_handle_l2cap_data_packet_for_signaling_connection(', """extern uint8_t picow_avrcp_target_pass_through(uint16_t cid, uint8_t operation,
    bool pressed, const uint8_t *operands, uint16_t size);

static void avrcp_handle_l2cap_data_packet_for_signaling_connection(""")
a = s.index('        case AVRCP_CMD_OPCODE_PASS_THROUGH:', s.index('static void avrcp_handle_l2cap_data_packet_for_signaling_connection('))
b = s.index('        case AVRCP_CMD_OPCODE_VENDOR_DEPENDENT:', a)
s = s[:a] + """        case AVRCP_CMD_OPCODE_PASS_THROUGH: {
            if (size < 8) return;
            const uint8_t raw_operation = packet[6];
            const uint8_t count = packet[7];
            uint8_t response = AVRCP_CTYPE_RESPONSE_REJECTED;
            const bool bounded = count <= 5 && size == (uint16_t)(8u + count);
            // CONTROL, panel subunit 0 only. Never pass malformed operands to app.
            if (bounded && (packet[0] & 3u) == 0 && packet[3] == AVRCP_CTYPE_CONTROL && packet[4] == 0x48) {
                const avrcp_operation_id_t op = (avrcp_operation_id_t)(raw_operation & 0x7f);
                response = avcrp_operation_id_is_valid(op) ?
                    picow_avrcp_target_pass_through(connection->avrcp_cid, (uint8_t)op,
                        (raw_operation & 0x80) == 0, packet + 8, count) :
                    AVRCP_CTYPE_RESPONSE_NOT_IMPLEMENTED;
                if (response != AVRCP_CTYPE_RESPONSE_ACCEPTED && response != AVRCP_CTYPE_RESPONSE_REJECTED &&
                    response != AVRCP_CTYPE_RESPONSE_NOT_IMPLEMENTED) response = AVRCP_CTYPE_RESPONSE_REJECTED;
            }
            avrcp_target_pass_through_command_data_init(connection, (avrcp_command_type_t)response,
                (avrcp_operation_id_t)raw_operation);
            // Echo only complete bounded operands, never copy an attacker length.
            if (bounded) { connection->data_len = count; memcpy(connection->data, packet + 8, count); }
            connection->state = AVCTP_W2_SEND_RESPONSE;
            avrcp_request_can_send_now(connection, connection->l2cap_signaling_cid);
            break;
        }

""" + s[b:]
replace('return avrcp_target_pass_through_response(avrcp_cid, AVRCP_CTYPE_RESPONSE_ACCEPTED, opid, operands_length, operand);\n}\n\nuint8_t avrcp_target_set_unit_info',
        'return avrcp_target_pass_through_response(avrcp_cid, AVRCP_CTYPE_RESPONSE_NOT_IMPLEMENTED, opid, operands_length, operand);\n}\n\nuint8_t avrcp_target_set_unit_info')
# Reserved IDs 0x76..0x7d are not vendor-unique panel operations.
replace('    if (operation_id < AVRCP_OPERATION_ID_RESERVED_7) return true;',
        '    if (operation_id == AVRCP_OPERATION_ID_VENDOR_UNIQUE) return true;')
# Pass-through has a one-byte operand length, with no two padding bytes.
replace("""                    packet[pos++] = (uint8_t) connection->data_len;
                    pos += 2;
                    break;""", """                    packet[pos++] = (uint8_t) connection->data_len;
                    break;""")
# The SDK helper had its (role, cid) lookup arguments reversed.
replace('avrcp_get_connection_for_avrcp_cid_for_role(avrcp_cid, AVRCP_TARGET)',
        'avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_TARGET, avrcp_cid)')
# Company IDs are network-order 24-bit values, like the Controller parser.
replace('little_endian_store_24(packet, pos,\n                                                               connection->target_supported_companies[i]);',
        'big_endian_store_24(packet, pos,\n                                                               connection->target_supported_companies[i]);')
# Delegate only recognized, bounded extensions to the application facade. The
# callback includes CID so the facade can resolve a live epoch/generation binding.
replace('static void avrcp_handle_l2cap_data_packet_for_signaling_connection(', '''extern uint8_t picow_avrcp_target_extension(uint16_t cid, uint8_t pdu,
    const uint8_t *params, uint16_t size, uint8_t *response, uint16_t *response_size);

static void avrcp_handle_l2cap_data_packet_for_signaling_connection(''')
replace('            // pos = 13\n            switch (pdu_id){', '''            // Validate declared vendor parameters before every PDU-specific read.
            if (length != (uint16_t)(size - pos)) {
                avrcp_target_response_vendor_dependent_reject(connection, pdu_id, AVRCP_STATUS_INVALID_PARAMETER);
                return;
            }
            switch (pdu_id){
                case AVRCP_PDU_ID_LIST_PLAYER_APPLICATION_SETTING_ATTRIBUTES:
                case AVRCP_PDU_ID_LIST_PLAYER_APPLICATION_SETTING_VALUES:
                case AVRCP_PDU_ID_GET_CURRENT_PLAYER_APPLICATION_SETTING_VALUE:
                case AVRCP_PDU_ID_SET_PLAYER_APPLICATION_SETTING_VALUE:
                case AVRCP_PDU_ID_GET_PLAYER_APPLICATION_SETTING_ATTRIBUTE_TEXT:
                case AVRCP_PDU_ID_GET_PLAYER_APPLICATION_SETTING_VALUE_TEXT: {
                    uint8_t response[128];
                    uint16_t response_size = sizeof(response);
                    uint8_t status = picow_avrcp_target_extension(connection->avrcp_cid, pdu_id,
                        packet + pos, length, response, &response_size);
                    if (status != AVRCP_STATUS_SUCCESS || response_size > sizeof(response)) {
                        avrcp_target_response_vendor_dependent_reject(connection, pdu_id,
                            status == AVRCP_STATUS_SUCCESS ? AVRCP_STATUS_INTERNAL_ERROR : status);
                        return;
                    }
                    avrcp_target_vendor_dependent_response_data_init(connection,
                        pdu_id == AVRCP_PDU_ID_SET_PLAYER_APPLICATION_SETTING_VALUE ?
                        AVRCP_CTYPE_RESPONSE_ACCEPTED : AVRCP_CTYPE_RESPONSE_IMPLEMENTED_STABLE, pdu_id);
                    connection->data_len = response_size;
                    memcpy(connection->data, response, response_size);
                    connection->state = AVCTP_W2_SEND_RESPONSE;
                    avrcp_request_can_send_now(connection, connection->l2cap_signaling_cid);
                    break;
                }''')
a=s.index('                case AVRCP_PDU_ID_SET_ADDRESSED_PLAYER:{')
b=s.index('                case AVRCP_PDU_ID_GET_CAPABILITIES:{',a)
s=s[:a]+'''                case AVRCP_PDU_ID_SET_ADDRESSED_PLAYER:{
                    uint8_t response[1]; uint16_t response_size = sizeof(response);
                    uint8_t status = length == 2 ? picow_avrcp_target_extension(connection->avrcp_cid,
                        pdu_id, packet + pos, length, response, &response_size) : AVRCP_STATUS_INVALID_PARAMETER;
                    if (status == AVRCP_STATUS_SUCCESS) {
                        connection->target_addressed_player_id = big_endian_read_16(packet, pos);
                        avrcp_target_vendor_dependent_response_accept(connection, pdu_id, status);
                    } else {
                        avrcp_target_response_vendor_dependent_reject(connection, pdu_id, status);
                    }
                    break;
                }
'''+s[b:]
replace('''                        case AVRCP_NOTIFICATION_EVENT_MAX_VALUE:
                            avrcp_target_response_vendor_dependent_not_implemented(connection, pdu_id, event_id);
                            return;''','''                        {
                            uint8_t response[128]; uint16_t response_size = sizeof(response);
                            uint8_t status = length == 5 ? picow_avrcp_target_extension(connection->avrcp_cid,
                                pdu_id, packet + pos, length, response, &response_size) : AVRCP_STATUS_INVALID_PARAMETER;
                            if (status != AVRCP_STATUS_SUCCESS || response_size > sizeof(response)) {
                                avrcp_target_response_vendor_dependent_reject(connection, pdu_id,
                                    status == AVRCP_STATUS_SUCCESS ? AVRCP_STATUS_INTERNAL_ERROR : status);
                                return;
                            }
                            connection->notifications_enabled |= (1 << event_id);
                            avrcp_target_response_vendor_dependent_interim(connection, pdu_id, event_id, response, response_size);
                            return;
                        }
                        case AVRCP_NOTIFICATION_EVENT_MAX_VALUE:
                            avrcp_target_response_vendor_dependent_not_implemented(connection, pdu_id, event_id);
                            return;''')
# Validate event before indexing its transaction slot or shifting its mask.
replace("""                    if ((pos + 1) > size) return;
                    avrcp_notification_event_id_t event_id = (avrcp_notification_event_id_t) packet[pos];

                    avrcp_target_set_transaction_label_for_notification(connection, event_id, connection->transaction_id);""", """                    if (length != 5) { avrcp_target_response_vendor_dependent_reject(connection, pdu_id, AVRCP_STATUS_INVALID_PARAMETER); return; }
                    avrcp_notification_event_id_t event_id = (avrcp_notification_event_id_t) packet[pos];
                    if (event_id < 1 || event_id >= AVRCP_NOTIFICATION_EVENT_MAX_VALUE ||
                        !(connection->notifications_supported_by_target & (1u << event_id))) {
                        avrcp_target_response_vendor_dependent_reject(connection, pdu_id, AVRCP_STATUS_INVALID_PARAMETER); return;
                    }
                    avrcp_target_set_transaction_label_for_notification(connection, event_id, connection->transaction_id);""")
# Protect zero-length queries and out-of-range shifts from untrusted PDUs.
replace('''                case AVRCP_PDU_ID_GET_CAPABILITIES:{
                    avrcp_capability_id_t''','''                case AVRCP_PDU_ID_GET_CAPABILITIES:{
                    if (length != 1) { avrcp_target_response_vendor_dependent_reject(connection, pdu_id, AVRCP_STATUS_INVALID_PARAMETER); return; }
                    avrcp_capability_id_t''')
replace('''                            connection->target_now_playing_info_attr_bitmap |= (1 << attr_id);''','''                            if (attr_id < AVRCP_MEDIA_ATTR_TITLE || attr_id > AVRCP_MEDIA_ATTR_SONG_LENGTH_MS) {
                                avrcp_target_response_vendor_dependent_reject(connection, pdu_id, AVRCP_STATUS_INVALID_PARAMETER); return;
                            }
                            connection->target_now_playing_info_attr_bitmap |= (1u << attr_id);''')
s+='''
// Repository extension: send a copied changed payload only on an idle target
// channel and after a matching registration; never overwrite a pending reply.
uint8_t picow_avrcp_target_notify(uint16_t cid, uint8_t event, const uint8_t *value, uint16_t size) {
    avrcp_connection_t *connection = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_TARGET, cid);
    if (!connection) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    if (event < 1 || event >= AVRCP_NOTIFICATION_EVENT_MAX_VALUE || size > 128 || (size && !value))
        return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    if (!(connection->notifications_enabled & (1u << event))) return ERROR_CODE_SUCCESS;
    if (connection->state != AVCTP_CONNECTION_OPENED || connection->target_now_playing_info_response ||
        (connection->pdu_id == AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES && connection->data_offset < connection->data_len))
        return ERROR_CODE_COMMAND_DISALLOWED;
    avrcp_target_notification_init(connection, (avrcp_notification_event_id_t)event, (uint8_t*)value, size);
    avrcp_target_reset_notification(connection, (avrcp_notification_event_id_t)event);
    connection->state = AVCTP_W2_SEND_RESPONSE;
    avrcp_request_can_send_now(connection, connection->l2cap_signaling_cid);
    return ERROR_CODE_SUCCESS;
}
'''
replace('btstack_min(connection->data_len - connection->data_offset, max_payload_size - pos)', 'btstack_min(connection->data_len - connection->data_offset, max_payload_size)')
replace('                            max_payload_size--;', '                            max_payload_size--;\n                            connection->data_offset++;')
from avrcp_target_metadata_patch import patch_metadata
s = patch_metadata(s)
destination.parent.mkdir(parents=True,exist_ok=True)
destination.write_text(s)
