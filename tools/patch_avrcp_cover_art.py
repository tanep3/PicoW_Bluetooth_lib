#!/usr/bin/env python3
"""Generate pinned Cover Art safety overlay; retain SDK source and its license.

Only this generated translation unit adds picow_avrcp_cover_art_client_cancel.
No upstream header/layout changes. W4_ABORT_COMPLETE denotes cancellation or
transport teardown; caller storage remains owned until CONNECTION_RELEASED.
"""
import hashlib
import sys
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit('usage: patch_avrcp_cover_art.py INPUT OUTPUT')
source, output = map(Path, sys.argv[1:])
if source.resolve() == output.resolve():
    raise SystemExit('Input and output must differ')
raw = source.read_bytes()
if hashlib.sha256(raw).hexdigest() != '3ced11b2e60d7dcd586d84a20aff3a5d2d3a80d03a0fbaa4a8713f95219cda8b':
    raise SystemExit('Unknown Cover Art Client SDK hash')
s = raw.decode()
def replace(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit('Nonunique Cover Art overlay anchor: ' + old[:80])
    s = s.replace(old, new)
def function(start, new):
    global s
    pos = s.index(start)
    end = s.index('\n}', pos) + 2
    s = s[:pos] + new + s[end:]
replace('''    memset(cover_art_client, 0, sizeof(avrcp_cover_art_client_t));
    btstack_linked_list_remove(&avrcp_cover_art_client_connections, (btstack_linked_item_t *) cover_art_client);''', '''    btstack_linked_list_remove(&avrcp_cover_art_client_connections, (btstack_linked_item_t *) cover_art_client);
    memset(cover_art_client, 0, sizeof(avrcp_cover_art_client_t));''')
anchor = 'static void avrcp_cover_art_client_parser_callback_connect'
helpers = '''/* PicoW overlay: no ownership release before transport terminal event. */
static void picow_cover_art_fail(avrcp_cover_art_client_t * client, uint8_t status){
    avrcp_cover_art_state_t previous = client->state;
    uint16_t cid = client->cover_art_cid;
    uint16_t goep_cid = client->goep_cid;
    client->state = AVRCP_COVER_ART_W4_ABORT_COMPLETE;
    client->obex_parser_waiting_for_response = false;
    if (previous < AVRCP_COVER_ART_CONNECTED){
        avrcp_cover_art_client_emit_connection_established(client->packet_handler, status,
                                                          client->addr, client->avrcp_cid, cid);
    } else if (previous == AVRCP_COVER_ART_W2_SEND_GET_OBJECT || previous == AVRCP_COVER_ART_W4_OBJECT){
        cover_art_client_emit_operation_complete_event(client, status);
    }
    if (avrcp_cover_art_client_for_cover_art_cid(cid) == client){
        goep_client_disconnect(goep_cid);
    }
}

static bool picow_cover_art_handle_valid(const char * handle){
    if (handle == NULL) return false;
    unsigned i;
    for (i = 0; i < 7; ++i){
        if (handle[i] < '0' || handle[i] > '9') return false;
    }
    return handle[7] == 0;
}

/* Includes packet prefix, optional connection ID, SRM, type, descriptor, and
 * UTF-16BE image handle with terminator. Preflight before any builder writes:
 * upstream unicode builder does not account for the current packet position. */
static uint8_t picow_cover_art_preflight(avrcp_cover_art_client_t * client,
        const char * type, const char * handle, const char * descriptor){
    if (!picow_cover_art_handle_valid(handle)) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    size_t descriptor_len = 0;
    if (descriptor != NULL){
        while (descriptor_len <= 256 && descriptor[descriptor_len] != 0) ++descriptor_len;
        if (descriptor_len > 256) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    }
    size_t needed = 3 + 19 + 4 + strlen(type);
    if (client->goep_client.obex_connection_id != OBEX_CONNECTION_ID_INVALID) needed += 5;
    if (!client->flow_control_enabled) needed += 2;
    if (descriptor != NULL) needed += 3 + descriptor_len;
    return needed <= goep_client_body_get_outgoing_buffer_len(client->goep_cid)
        ? ERROR_CODE_SUCCESS : ERROR_CODE_MEMORY_CAPACITY_EXCEEDED;
}

'''
replace(anchor, helpers + anchor)
# Ignore remaining body headers if a synchronous user callback requests cancel.
replace('''                default:
                    btstack_unreachable();
                    break;''', '''                default:
                    break; /* transport closing, ignore remaining body */''')
replace('''static void avrcp_cover_art_client_handle_can_send_now(avrcp_cover_art_client_t * cover_art_client){
    switch''', '''static void avrcp_cover_art_client_handle_can_send_now(avrcp_cover_art_client_t * cover_art_client){
    uint8_t status;
    switch''')
replace('''        case AVRCP_COVER_ART_W2_SEND_CONNECT_REQUEST:
            // prepare request''', '''        case AVRCP_COVER_ART_W2_SEND_CONNECT_REQUEST:
            if (goep_client_body_get_outgoing_buffer_len(cover_art_client->goep_cid) < 26){
                picow_cover_art_fail(cover_art_client, ERROR_CODE_MEMORY_CAPACITY_EXCEEDED);
                break;
            }
            // prepare request''')
replace('''        case AVRCP_COVER_ART_W2_SEND_GET_OBJECT:
            goep_client_request_create_get''', '''        case AVRCP_COVER_ART_W2_SEND_GET_OBJECT:
            if (cover_art_client->first_request){
                status = picow_cover_art_preflight(cover_art_client, cover_art_client->object_type,
                                                   cover_art_client->image_handle, cover_art_client->image_descriptor);
                if (status != ERROR_CODE_SUCCESS){
                    cover_art_client->state = AVRCP_COVER_ART_CONNECTED;
                    cover_art_client_emit_operation_complete_event(cover_art_client, status);
                    break;
                }
            }
            goep_client_request_create_get''')
replace('uint8_t image_handle_len = btstack_max(7, (uint16_t) strlen(cover_art_client->image_handle));', 'uint8_t image_handle_len = 7;')
old = '            goep_client_execute(cover_art_client->goep_cid);'
if s.count(old) != 3:
    raise SystemExit('Expected three Cover Art execute sites')
s = s.replace(old, '''            status = (uint8_t) goep_client_execute(cover_art_client->goep_cid);
            if (status != ERROR_CODE_SUCCESS) picow_cover_art_fail(cover_art_client, status);''')
function('static void avrcp_cover_art_goep_event_handler', '''static void avrcp_cover_art_goep_event_handler(const uint8_t *packet, uint16_t size) {
    if (size < 5 || packet[0] != HCI_EVENT_GOEP_META) return;
    uint16_t goep_cid = little_endian_read_16(packet, 3);
    avrcp_cover_art_client_t * client = avrcp_cover_art_client_for_goep_cid(goep_cid);
    if (client == NULL) return;
    btstack_packet_handler_t handler = client->packet_handler;
    uint16_t cid = client->cover_art_cid;
    uint16_t avrcp_cid = client->avrcp_cid;
    bd_addr_t addr;
    memcpy(addr, client->addr, sizeof(addr));
    switch (packet[2]){
        case GOEP_SUBEVENT_CONNECTION_OPENED: {
            if (size < 15) return;
            uint8_t status = goep_subevent_connection_opened_get_status(packet);
            bool cancelling = client->state == AVRCP_COVER_ART_W4_ABORT_COMPLETE;
            if (status != ERROR_CODE_SUCCESS){
                avrcp_cover_art_finalize_connection(client);
                if (cancelling){
                    avrcp_cover_art_client_emit_connection_released(handler, cid);
                } else {
                    avrcp_cover_art_client_emit_connection_established(handler, status, addr, avrcp_cid, cid);
                }
            } else if (cancelling){
                goep_client_disconnect(goep_cid);
            } else {
                client->state = AVRCP_COVER_ART_W2_SEND_CONNECT_REQUEST;
                goep_client_request_can_send_now(goep_cid);
            }
            break;
        }
        case GOEP_SUBEVENT_CONNECTION_CLOSED: {
            avrcp_cover_art_state_t state = client->state;
            /* Complete callbacks after unlink/clear, with IDs saved above. */
            avrcp_cover_art_finalize_connection(client);
            if (state < AVRCP_COVER_ART_CONNECTED){
                avrcp_cover_art_client_emit_connection_established(handler, OBEX_DISCONNECTED, addr, avrcp_cid, cid);
            } else if (state == AVRCP_COVER_ART_W2_SEND_GET_OBJECT || state == AVRCP_COVER_ART_W4_OBJECT){
                uint8_t buffer[20];
                uint16_t n = hci_event_create_from_template_and_arguments(buffer, sizeof(buffer),
                    &avrcp_cover_art_client_operation_complete, cid, OBEX_DISCONNECTED);
                handler(HCI_EVENT_PACKET, 0, buffer, n);
            }
            avrcp_cover_art_client_emit_connection_released(handler, cid);
            break;
        }
        case GOEP_SUBEVENT_CAN_SEND_NOW:
            avrcp_cover_art_client_handle_can_send_now(client);
            break;
        default:
            break;
    }
}''')
replace('''    btstack_assert(cover_art_client->obex_parser_waiting_for_response);

    obex_parser_object_state_t parser_state;
    parser_state = obex_parser_process_data(&cover_art_client->obex_parser, packet, size);''', '''    if (!cover_art_client->obex_parser_waiting_for_response || size == 0) return;

    obex_parser_object_state_t parser_state = OBEX_PARSER_OBJECT_STATE_INCOMPLETE;
    uint16_t offset = 0;
    /* Feed headers incrementally and body in bounded chunks. Never ask the
     * upstream parser to read past a declared packet or continue after error. */
    while (offset < size){
        obex_parser_t * parser = &cover_art_client->obex_parser;
        uint16_t chunk = 1;
        if (parser->state == OBEX_PARSER_STATE_W4_HEADER_VALUE){
            chunk = btstack_min(size - offset, parser->item_len - parser->item_pos);
            chunk = btstack_min(chunk, parser->packet_size - parser->packet_pos);
        }
        if (chunk == 0){
            parser_state = OBEX_PARSER_OBJECT_STATE_INVALID;
            break;
        }
        parser_state = obex_parser_process_data(parser, packet + offset, chunk);
        offset += chunk;
        if (!cover_art_client->obex_parser_waiting_for_response) return;
        if (parser_state != OBEX_PARSER_OBJECT_STATE_INCOMPLETE) break;
    }
    if (parser_state == OBEX_PARSER_OBJECT_STATE_COMPLETE && offset != size){
        parser_state = OBEX_PARSER_OBJECT_STATE_OVERRUN;
    }
    if (parser_state == OBEX_PARSER_OBJECT_STATE_INVALID || parser_state == OBEX_PARSER_OBJECT_STATE_OVERRUN){
        picow_cover_art_fail(cover_art_client, OBEX_UNKNOWN_ERROR);
        return;
    }''')
replace('''                    case OBEX_RESP_SUCCESS:
                        cover_art_client->state = AVRCP_COVER_ART_CONNECTED;
                        avrcp_cover_art_client_emit_connection_established''', '''                    case OBEX_RESP_SUCCESS:
                        if (op_info.max_packet_length < 255){
                            picow_cover_art_fail(cover_art_client, OBEX_CONNECT_FAILED);
                            break;
                        }
                        cover_art_client->goep_client.bearer_mtu = btstack_min(
                            cover_art_client->goep_client.bearer_mtu, op_info.max_packet_length);
                        cover_art_client->state = AVRCP_COVER_ART_CONNECTED;
                        avrcp_cover_art_client_emit_connection_established''')
replace('''                        cover_art_client->state = AVRCP_COVER_ART_INIT;
                        avrcp_cover_art_client_emit_connection_established(cover_art_client->packet_handler,
                                                                           OBEX_CONNECT_FAILED,
                                                                           cover_art_client->addr,
                                                                           cover_art_client->avrcp_cid,
                                                                           cover_art_client->cover_art_cid);''', '''                        picow_cover_art_fail(cover_art_client, OBEX_CONNECT_FAILED);''')
replace('''            cover_art_client = avrcp_cover_art_client_for_goep_cid(channel);
            btstack_assert(cover_art_client != NULL);
            avrcp_cover_art_client_goep_data_handler''', '''            cover_art_client = avrcp_cover_art_client_for_goep_cid(channel);
            if (cover_art_client == NULL) return;
            avrcp_cover_art_client_goep_data_handler''')
replace('''        return avrcp_cover_art_client_setup_connection(cover_art_client, connection_controller->cover_art_psm);''', '''        uint8_t status = avrcp_cover_art_client_setup_connection(cover_art_client, connection_controller->cover_art_psm);
        if (status != ERROR_CODE_SUCCESS){
            avrcp_cover_art_finalize_connection(cover_art_client);
            if (avrcp_cover_art_cid != NULL) *avrcp_cover_art_cid = 0;
        }
        return status;''')
replace('''    avrcp_connection_t * connection_controller = avrcp_get_connection_for_bd_addr_for_role(AVRCP_CONTROLLER, remote_addr);''', '''    if (cover_art_client == NULL || packet_handler == NULL || remote_addr == NULL ||
        ertm_buffer == NULL || ertm_config == NULL || ertm_buffer_size == 0 || ertm_buffer_size > UINT16_MAX){
        return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    }
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, &avrcp_cover_art_client_connections);
    while (btstack_linked_list_iterator_has_next(&it)){
        if (btstack_linked_list_iterator_next(&it) == (btstack_linked_item_t *) cover_art_client){
            return ERROR_CODE_COMMAND_DISALLOWED;
        }
    }
    avrcp_connection_t * connection_controller = avrcp_get_connection_for_bd_addr_for_role(AVRCP_CONTROLLER, remote_addr);''')
replace('''    cover_art_client->state = AVRCP_COVER_ART_W2_SEND_GET_OBJECT;
    cover_art_client->first_request = true;''', '''    uint8_t status = picow_cover_art_preflight(cover_art_client, object_type, image_handle, image_descriptor);
    if (status != ERROR_CODE_SUCCESS) return status;
    cover_art_client->state = AVRCP_COVER_ART_W2_SEND_GET_OBJECT;
    cover_art_client->first_request = true;''')
replace('''    if (cover_art_client->state < AVRCP_COVER_ART_CONNECTED){
        return ERROR_CODE_COMMAND_DISALLOWED;
    }
    cover_art_client->state = AVRCP_COVER_ART_W2_SEND_DISCONNECT_REQUEST;''', '''    if (cover_art_client->state != AVRCP_COVER_ART_CONNECTED){
        return ERROR_CODE_COMMAND_DISALLOWED;
    }
    cover_art_client->state = AVRCP_COVER_ART_W2_SEND_DISCONNECT_REQUEST;''')
# Do not lose pointers still retained by GOEP/ERTM when deinit is called early.
replace('''void avrcp_cover_art_client_deinit(void){
    avrcp_cover_art_client_connections = NULL;
}''', '''uint8_t picow_avrcp_cover_art_client_cancel(uint16_t cid){
    avrcp_cover_art_client_t * client = avrcp_cover_art_client_for_cover_art_cid(cid);
    if (client == NULL) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    if (client->state == AVRCP_COVER_ART_W4_ABORT_COMPLETE){
        return client->goep_client.state == GOEP_CLIENT_CONNECTED
            ? goep_client_disconnect(client->goep_cid) : ERROR_CODE_SUCCESS;
    }
    if (client->state == AVRCP_COVER_ART_W4_SDP_QUERY_COMPLETE){
        btstack_packet_handler_t handler = client->packet_handler;
        avrcp_cover_art_finalize_connection(client);
        avrcp_cover_art_client_emit_connection_released(handler, cid);
        return ERROR_CODE_SUCCESS;
    }
    avrcp_cover_art_state_t previous = client->state;
    bool waiting = client->obex_parser_waiting_for_response;
    bool opening = previous <= AVRCP_COVER_ART_W4_GOEP_CONNECTION;
    client->state = AVRCP_COVER_ART_W4_ABORT_COMPLETE;
    client->obex_parser_waiting_for_response = false;
    /* L2CAP channel establishment owns its config/buffer until OPEN/CLOSED.
     * Don't disconnect an unestablished channel using a remote CID of zero. */
    if (opening) return ERROR_CODE_SUCCESS;
    uint8_t status = goep_client_disconnect(client->goep_cid);
    if (status != ERROR_CODE_SUCCESS && avrcp_cover_art_client_for_cover_art_cid(cid) == client){
        client->state = previous;
        client->obex_parser_waiting_for_response = waiting;
    }
    return status;
}

void avrcp_cover_art_client_deinit(void){
    /* Facade cancels and waits for RELEASED before deinit. A premature call
     * must not discard live transport ownership or another client's links. */
    if (avrcp_cover_art_client_connections != NULL) return;
}''')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(s)
print('Cover Art Client overlay:', hashlib.sha256(s.encode()).hexdigest())
