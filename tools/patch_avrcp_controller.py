#!/usr/bin/env python3
"""Generate a reviewed AVRCP Controller overlay; never edit the installed SDK.

The input retains the original BlueKitchen copyright/license in full. This
script is repository-authored modification logic, not a replacement license.
Unknown upstream bytes fail closed, including modified-but-same-version inputs.
"""
import hashlib
from avrcp_controller_fragments import apply as add_fragments
from avrcp_controller_custom import apply as add_custom
from pathlib import Path
import sys

EXPECTED_SHA256 = "ae62338f2c7837b37dca1fb10ccd593f1e245e372160febdb2fa18af1f52bbd7"
REVISION = "picow-avrcp-controller-overlay-v6"

HELPERS = r'''
/* PicoW controller overlay v1. Original upstream license remains above.
 * Local extension subevent 0xf0: cid LE16, ctype, pdu, total LE16,
 * offset LE16, then a <=246-byte payload chunk. Header is 11 bytes.
 * Fields within the retained AVRCP payload remain in network byte order.
 */
#define PICOW_AVRCP_SETTINGS_RESPONSE 0xf0u
static bool picow_avrcp_settings_pdu(uint8_t pdu){
    switch (pdu){
        case AVRCP_PDU_ID_LIST_PLAYER_APPLICATION_SETTING_ATTRIBUTES:
        case AVRCP_PDU_ID_LIST_PLAYER_APPLICATION_SETTING_VALUES:
        case AVRCP_PDU_ID_GET_CURRENT_PLAYER_APPLICATION_SETTING_VALUE:
        case AVRCP_PDU_ID_GET_PLAYER_APPLICATION_SETTING_ATTRIBUTE_TEXT:
        case AVRCP_PDU_ID_GET_PLAYER_APPLICATION_SETTING_VALUE_TEXT:
        case AVRCP_PDU_ID_INFORM_DISPLAYABLE_CHARACTERSET:
        case AVRCP_PDU_ID_INFORM_BATTERY_STATUS_OF_CT: return true;
        default: return false;
    }
}
static bool picow_avrcp_settings_payload_valid(uint8_t pdu, uint8_t ctype,
                                               const uint8_t *p, uint16_t n){
    if (ctype == AVRCP_CTYPE_RESPONSE_REJECTED ||
        ctype == AVRCP_CTYPE_RESPONSE_NOT_IMPLEMENTED) return n <= 1;
    if (pdu == AVRCP_PDU_ID_INFORM_DISPLAYABLE_CHARACTERSET ||
        pdu == AVRCP_PDU_ID_INFORM_BATTERY_STATUS_OF_CT)
        return ctype == AVRCP_CTYPE_RESPONSE_ACCEPTED && n == 0;
    if (ctype != AVRCP_CTYPE_RESPONSE_IMPLEMENTED_STABLE || n < 1) return false;
    if (pdu == AVRCP_PDU_ID_LIST_PLAYER_APPLICATION_SETTING_ATTRIBUTES ||
        pdu == AVRCP_PDU_ID_LIST_PLAYER_APPLICATION_SETTING_VALUES)
        return n == (uint16_t)(1u + p[0]);
    if (pdu == AVRCP_PDU_ID_GET_CURRENT_PLAYER_APPLICATION_SETTING_VALUE)
        return n == (uint16_t)(1u + 2u * p[0]);
    uint32_t pos = 1;
    for (unsigned i = 0; i < p[0]; ++i){
        if (pos + 4u > n) return false;
        const uint8_t length = p[pos + 3u];
        pos += 4u;
        if (pos + length > n) return false;
        pos += length;
    }
    return pos == n;
}
static void picow_avrcp_emit_settings_response(avrcp_connection_t *connection,
                                               uint8_t ctype, uint8_t pdu,
                                               const uint8_t *payload, uint16_t length){
    uint32_t offset = 0;
    do {
        uint8_t event[257];
        const uint16_t chunk = (uint16_t)btstack_min((uint32_t)246, length - offset);
        event[0] = HCI_EVENT_AVRCP_META;
        event[1] = (uint8_t)(9u + chunk);
        event[2] = PICOW_AVRCP_SETTINGS_RESPONSE;
        little_endian_store_16(event, 3, connection->avrcp_cid);
        event[5] = ctype;
        event[6] = pdu;
        little_endian_store_16(event, 7, length);
        little_endian_store_16(event, 9, (uint16_t)offset);
        if (chunk) memcpy(event + 11, payload + offset, chunk);
        (*avrcp_controller_context.avrcp_callback)(HCI_EVENT_PACKET, 0, event,
                                                                 (uint16_t)(11u + chunk));
        offset += chunk;
    } while (offset < length);
}
'''

CHARSET = r'''uint8_t avrcp_controller_inform_displayable_characterset(uint16_t avrcp_cid, uint8_t character_set_num, uint16_t * character_set){
    /* The fixed SDK command buffer is 11 bytes: count + at most five BE16 IDs. */
    if (!character_set || !character_set_num ||
        character_set_num > (AVRCP_MAX_COMMAND_PARAMETER_LENGTH - 1u) / 2u)
        return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    avrcp_connection_t *connection = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER, avrcp_cid);
    if (!connection) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    if (connection->state != AVCTP_CONNECTION_OPENED) return ERROR_CODE_COMMAND_DISALLOWED;
    connection->state = AVCTP_W2_SEND_COMMAND;
    avrcp_controller_vendor_dependent_command_data_init(connection, AVRCP_CTYPE_CONTROL,
        AVRCP_PDU_ID_INFORM_DISPLAYABLE_CHARACTERSET, true);
    connection->data_len = (uint16_t)(1u + 2u * character_set_num);
    connection->data[0] = character_set_num;
    for (unsigned i = 0; i < character_set_num; ++i)
        big_endian_store_16(connection->data, 1u + 2u * i, character_set[i]);
    avrcp_request_can_send_now(connection, connection->l2cap_signaling_cid);
    return ERROR_CODE_SUCCESS;
}

'''

EXTENSIONS = r'''
/* Explicit, bounded setting queries supplement the SDK's no-argument helpers.
 * All parameter bytes are copied synchronously into SDK-owned message_body. */
static uint8_t picow_avrcp_settings_request(uint16_t cid, avrcp_pdu_id_t pdu,
                                           avrcp_command_type_t type,
                                           const uint8_t *data, uint8_t length){
    if (!data || length > AVRCP_MAX_COMMAND_PARAMETER_LENGTH)
        return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    avrcp_connection_t *connection = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER, cid);
    if (!connection) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    if (connection->state != AVCTP_CONNECTION_OPENED) return ERROR_CODE_COMMAND_DISALLOWED;
    connection->state = AVCTP_W2_SEND_COMMAND;
    avrcp_controller_vendor_dependent_command_data_init(connection, type, pdu, true);
    memcpy(connection->data, data, length);
    connection->data_len = length;
    avrcp_request_can_send_now(connection, connection->l2cap_signaling_cid);
    return ERROR_CODE_SUCCESS;
}
uint8_t picow_avrcp_controller_setting_attribute_text(uint16_t cid, uint8_t count, const uint8_t *ids){
    if (!ids || !count || count > 10) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    uint8_t data[11]; data[0] = count;
    for (unsigned i = 0; i < count; ++i){
        if (!ids[i]) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
        data[1u+i] = ids[i];
    }
    return picow_avrcp_settings_request(cid, AVRCP_PDU_ID_GET_PLAYER_APPLICATION_SETTING_ATTRIBUTE_TEXT,
                                        AVRCP_CTYPE_STATUS, data, (uint8_t)(1u+count));
}
uint8_t picow_avrcp_controller_setting_value_text(uint16_t cid, uint8_t attribute, uint8_t count, const uint8_t *values){
    if (!attribute || !values || !count || count > 9) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    uint8_t data[11]; data[0] = attribute; data[1] = count;
    for (unsigned i = 0; i < count; ++i){
        if (!values[i]) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
        data[2u+i] = values[i];
    }
    return picow_avrcp_settings_request(cid, AVRCP_PDU_ID_GET_PLAYER_APPLICATION_SETTING_VALUE_TEXT,
                                        AVRCP_CTYPE_STATUS, data, (uint8_t)(2u+count));
}
uint8_t picow_avrcp_controller_current_settings(uint16_t cid, uint8_t count, const uint8_t *ids){
    if (!ids || !count || count > 10) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    uint8_t data[11]; data[0] = count;
    for (unsigned i = 0; i < count; ++i){
        if (!ids[i]) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
        data[1u+i] = ids[i];
    }
    return picow_avrcp_settings_request(cid, AVRCP_PDU_ID_GET_CURRENT_PLAYER_APPLICATION_SETTING_VALUE,
                                        AVRCP_CTYPE_STATUS, data, (uint8_t)(1u+count));
}
uint8_t picow_avrcp_controller_set_settings(uint16_t cid, uint8_t count, const uint8_t *ids, const uint8_t *values){
    if (!ids || !values || !count || count > 5) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    uint8_t data[11]; data[0] = count;
    for (unsigned i = 0; i < count; ++i){
        if (!ids[i] || !values[i]) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
        data[1u+2u*i] = ids[i]; data[2u+2u*i] = values[i];
    }
    return picow_avrcp_settings_request(cid, AVRCP_PDU_ID_SET_PLAYER_APPLICATION_SETTING_VALUE,
                                        AVRCP_CTYPE_CONTROL, data, (uint8_t)(1u+2u*count));
}
'''

def generate(source: str) -> str:
    def replace(old: str, new: str) -> None:
        nonlocal source
        if source.count(old) != 1:
            raise ValueError(f"patch anchor not unique: {old[:80]!r}")
        source = source.replace(old, new, 1)
    replace('static uint8_t avrcp_controller_calc_next_transaction_label', HELPERS + '\nstatic uint8_t avrcp_controller_calc_next_transaction_label')
    replace('connection->controller_last_confirmed_transaction_id = packet[pos] >> 4;', 'uint8_t received_transaction_id = packet[pos] >> 4;')
    marker = '    switch (opcode){\n        case AVRCP_CMD_OPCODE_SUBUNIT_INFO:'
    replace(marker, r'''    /* Validate selected setting responses before changing connection state or
     * consuming the transaction label. AVRCP-level fragmented settings require
     * an explicit continuation implementation and are not accepted as complete. */
    if (opcode == AVRCP_CMD_OPCODE_VENDOR_DEPENDENT && size >= 10u &&
        picow_avrcp_settings_pdu(packet[9])){
        if (size < 13u || connection->state != AVCTP_W2_RECEIVE_RESPONSE ||
            received_transaction_id != connection->transaction_id ||
            packet[9] != connection->pdu_id || (packet[10] & 3u) != 0u) return;
        const uint16_t length = big_endian_read_16(packet, 11);
        if (length != size - 13u ||
            !picow_avrcp_settings_payload_valid(packet[9], ctype, packet + 13, length)) return;
    }
    /* Correlate pass-through before consuming a label or changing SDK state.
     * Press and release are distinct transactions, including their state bit.
     * Keep asynchronous notification packets on their existing vendor path. */
    const bool pass_wait = connection->command_opcode == AVRCP_CMD_OPCODE_PASS_THROUGH &&
        (connection->state == AVCTP_W2_RECEIVE_PRESS_RESPONSE ||
         connection->state == AVCTP_W2_RECEIVE_RESPONSE);
    if (pass_wait && opcode != AVRCP_CMD_OPCODE_PASS_THROUGH &&
        !(opcode == AVRCP_CMD_OPCODE_VENDOR_DEPENDENT && size >= 13u &&
          packet[9] == AVRCP_PDU_ID_REGISTER_NOTIFICATION)) return;
    if (opcode == AVRCP_CMD_OPCODE_PASS_THROUGH){
        if (!pass_wait || size < 8u || (packet[0] & 15u) != 2u ||
            packet[1] != 0x11 || packet[2] != 0x0e ||
            packet[4] != ((connection->subunit_type << 3) | connection->subunit_id) ||
            received_transaction_id != connection->transaction_id ||
            packet[6] != (uint8_t)connection->operation_id ||
            ((packet[6] & 0x80u) != 0) != (connection->state == AVCTP_W2_RECEIVE_RESPONSE) ||
            size != 8u + packet[7] || packet[7] != connection->data_len ||
            (packet[7] && memcmp(packet + 8, connection->data, packet[7])) ||
            (ctype != AVRCP_CTYPE_RESPONSE_ACCEPTED &&
             ctype != AVRCP_CTYPE_RESPONSE_REJECTED &&
             ctype != AVRCP_CTYPE_RESPONSE_NOT_IMPLEMENTED)) return;
    }
    connection->controller_last_confirmed_transaction_id = received_transaction_id;
''' + marker)
    start = source.index('                case AVRCP_PDU_ID_LIST_PLAYER_APPLICATION_SETTING_ATTRIBUTES:{')
    end = source.index('                case AVRCP_PDU_ID_SET_PLAYER_APPLICATION_SETTING_VALUE:{', start)
    source = source[:start] + r'''                case AVRCP_PDU_ID_LIST_PLAYER_APPLICATION_SETTING_ATTRIBUTES:
                case AVRCP_PDU_ID_LIST_PLAYER_APPLICATION_SETTING_VALUES:
                case AVRCP_PDU_ID_GET_PLAYER_APPLICATION_SETTING_ATTRIBUTE_TEXT:
                case AVRCP_PDU_ID_GET_PLAYER_APPLICATION_SETTING_VALUE_TEXT:
                case AVRCP_PDU_ID_INFORM_DISPLAYABLE_CHARACTERSET:
                case AVRCP_PDU_ID_INFORM_BATTERY_STATUS_OF_CT:
                    picow_avrcp_emit_settings_response(connection, ctype, pdu_id, packet + pos, param_length);
                    break;

''' + source[end:]
    replace('''                case AVRCP_PDU_ID_GET_CURRENT_PLAYER_APPLICATION_SETTING_VALUE:{
                    uint8_t num_attributes''', '''                case AVRCP_PDU_ID_GET_CURRENT_PLAYER_APPLICATION_SETTING_VALUE:{
                    picow_avrcp_emit_settings_response(connection, ctype, pdu_id, packet + pos, param_length);
                    if (ctype != AVRCP_CTYPE_RESPONSE_IMPLEMENTED_STABLE) break;
                    uint8_t num_attributes''')
    replace('connection->data[0]  = 2;\n    connection->data[1]  = attr_id;', 'connection->data[0]  = 1;\n    connection->data[1]  = attr_id;')
    start = source.index('uint8_t avrcp_controller_inform_displayable_characterset(')
    end = source.index('uint8_t avrcp_controller_inform_battery_status(', start)
    source = source[:start] + CHARSET + source[end:]
    # Preserve the no-argument API with a documented valid default: repeat modes
    # 1..4. The new explicit helper supports a caller-selected attribute/value set.
    start = source.index('uint8_t avrcp_controller_query_player_application_setting_value_text(')
    end = source.index('uint8_t avrcp_controller_inform_displayable_characterset(', start)
    source = source[:start] + r'''uint8_t picow_avrcp_controller_setting_value_text(uint16_t, uint8_t, uint8_t, const uint8_t*);
uint8_t avrcp_controller_query_player_application_setting_value_text(uint16_t avrcp_cid){
    static const uint8_t repeat_values[] = {1,2,3,4};
    return picow_avrcp_controller_setting_value_text(avrcp_cid,
        AVRCP_PLAYER_APPLICATION_SETTING_ATTRIBUTE_ID_REPEAT_MODE_STATUS, 4, repeat_values);
}

''' + source[end:]
    replace('btstack_min(connection->data_len - connection->data_offset, max_payload_size - pos)', 'btstack_min(connection->data_len - connection->data_offset, max_payload_size)')
    return add_custom(add_fragments(source + '\n' + EXTENSIONS))


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: patch_avrcp_controller.py INPUT OUTPUT")
    if Path(sys.argv[1]).resolve() == Path(sys.argv[2]).resolve():
        raise SystemExit("Input and output must differ; installed SDK originals are never edited.")
    raw = Path(sys.argv[1]).read_bytes()
    actual = hashlib.sha256(raw).hexdigest()
    if actual != EXPECTED_SHA256:
        raise SystemExit(f"Unsupported AVRCP Controller source SHA256 {actual}; expected {EXPECTED_SHA256}. No output written.")
    result = generate(raw.decode('utf-8'))
    output = Path(sys.argv[2])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(result, encoding='utf-8')
    print(f"{REVISION}: {actual} -> {hashlib.sha256(result.encode()).hexdigest()}")

if __name__ == '__main__':
    main()
