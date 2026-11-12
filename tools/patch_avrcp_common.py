#!/usr/bin/env python3
"""Generate a pinned AVRCP common cancellation overlay; leave SDK untouched."""
import hashlib
import sys
from pathlib import Path
if len(sys.argv) != 3:
    raise SystemExit('usage: patch_avrcp_common.py INPUT OUTPUT')
a, b = map(Path, sys.argv[1:])
if a.resolve() == b.resolve():
    raise SystemExit('Input and output must differ')
raw = a.read_bytes()
if hashlib.sha256(raw).hexdigest() != 'a2d8514044eee2f91cc4590560562ea9f6c7aa54e03a97c20dbaf47a29a56974':
    raise SystemExit('Unknown common SDK hash; output untouched')
s = raw.decode()
def replace(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit('Nonunique common patch anchor: ' + old[:100])
    s = s.replace(old, new)

# Payload capacity excludes transport and AVRCP headers. Both role serializers
# must consume this directly; streamed Target attributes already do.
replace("""    if (connection->l2cap_mtu >= AVRCP_MAX_AV_C_MESSAGE_FRAME_SIZE){
        return AVCTP_SINGLE_PACKET;
    }

""", '')
replace('            offset += 3;  // AVRCP message: operation id (1), param_len (2)',
        '            offset += 2;  // AVRCP message: operation id (1), operand length (1)')
# The AV/C size limit does not include the AVCTP transport header. In particular,
# adding START's packet-count byte must not change AVRCP SINGLE into START.
replace("""    uint16_t payload_offset = avctp_get_num_bytes_for_header(connection->avctp_packet_type) +
                              avrcp_get_num_bytes_for_header(connection->command_opcode, connection->avctp_packet_type);""",
        """    uint16_t payload_offset = avrcp_get_num_bytes_for_header(connection->command_opcode, AVCTP_SINGLE_PACKET);""")

# Private state values use the existing enum storage, not a fixed-capacity side
# table or a borrowed payload field. No public SDK layout changes are required.
replace('static void avrcp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);', '''/* PicoW: cancellation states are terminal for ordinary profile traffic. */
#define PICOW_CANCEL_SDP ((avctp_connection_state_t)0x70)
#define PICOW_CANCEL_OPEN ((avctp_connection_state_t)0x71)
#define PICOW_CANCEL_CLOSE ((avctp_connection_state_t)0x72)
#define PICOW_CANCEL_CLOSED_SDP ((avctp_connection_state_t)0x73)
#define PICOW_CANCEL_ERROR_BASE 0x100
static bool picow_avrcp_is_cancelled(const avrcp_connection_t * c){
    return c != NULL && (unsigned)c->state >= 0x70;
}
static void avrcp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);''')
anchor = 'uint16_t avrcp_sdp_query_browsing_l2cap_psm(void){'
replace(anchor, '''/* Finalize first: RELEASED means no SDP/transport callback owns the pair. */
static void picow_avrcp_release_pair(uint16_t cid){
    avrcp_connection_t * c = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER, cid);
    avrcp_connection_t * t = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_TARGET, cid);
    if (c != NULL) avrcp_finalize_connection(c);
    if (t != NULL) avrcp_finalize_connection(t);
    avrcp_emit_connection_closed(cid);
}

bool picow_avrcp_cancel_pending(uint16_t cid){
    return picow_avrcp_is_cancelled(avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER, cid));
}

uint8_t picow_avrcp_cancel_status(uint16_t cid){
    avrcp_connection_t * c = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER, cid);
    if (c == NULL) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    if ((unsigned)c->state >= PICOW_CANCEL_ERROR_BASE) return (uint8_t)((unsigned)c->state - PICOW_CANCEL_ERROR_BASE);
    return ERROR_CODE_SUCCESS;
}

static uint8_t picow_avrcp_cancel_transport(avrcp_connection_t * c, avrcp_connection_t * t){
    /* l2cap_run may synchronously call back. Never touch c/t after success. */
    uint16_t cid = c->avrcp_cid;
    uint16_t local_cid = c->l2cap_signaling_cid;
    c->state = t->state = PICOW_CANCEL_CLOSE;
    uint8_t status = l2cap_disconnect(local_cid);
    if (status != ERROR_CODE_SUCCESS){
        c = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER, cid);
        t = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_TARGET, cid);
        if (c != NULL && t != NULL){
            c->state = t->state = (avctp_connection_state_t)(PICOW_CANCEL_ERROR_BASE + status);
        }
    }
    return status;
}

/* Accepted cancellation can complete synchronously. While pending, the CID
 * remains owned and reconnect is rejected. A failed deferred disconnect stays
 * pending; cancel_status exposes the lower error and cancel retries it.
 * Browsing must finish first: this API never destroys attached ERTM buffers. */
uint8_t picow_avrcp_cancel(uint16_t cid){
    avrcp_connection_t * c = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER, cid);
    avrcp_connection_t * t = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_TARGET, cid);
    if (c == NULL || t == NULL) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    if (c->browsing_connection != NULL || t->browsing_connection != NULL) return ERROR_CODE_COMMAND_DISALLOWED;
    if (picow_avrcp_is_cancelled(c)){
        if ((unsigned)c->state >= PICOW_CANCEL_ERROR_BASE) return picow_avrcp_cancel_transport(c, t);
        return ERROR_CODE_SUCCESS;
    }
    avctp_connection_state_t state = c->state;
    c->trigger_sdp_query = t->trigger_sdp_query = false;
    btstack_run_loop_remove_timer(&c->retry_timer);
    btstack_run_loop_remove_timer(&t->retry_timer);
    btstack_run_loop_remove_timer(&c->controller_press_and_hold_cmd_timer);
    if (state == AVCTP_CONNECTION_W4_L2CAP_CONNECTED){
        c->state = t->state = PICOW_CANCEL_OPEN;
        return ERROR_CODE_SUCCESS;
    }
    if (state >= AVCTP_CONNECTION_OPENED) return picow_avrcp_cancel_transport(c, t);
    c->state = t->state = PICOW_CANCEL_SDP;
    if (avrcp_sdp_query_context.avrcp_cid != cid){
        picow_avrcp_release_pair(cid);
    }
    return ERROR_CODE_SUCCESS;
}

/* Called only once SDP no longer owns its shared callback/attribute buffer. */
static void picow_avrcp_cancel_sdp_done(avrcp_connection_t * c){
    uint16_t cid = c->avrcp_cid;
    bool release = c->state == PICOW_CANCEL_SDP || c->state == PICOW_CANCEL_CLOSED_SDP;
    avrcp_sdp_query_context.avrcp_cid = 0;
    if (release) picow_avrcp_release_pair(cid);
    avrcp_start_next_sdp_query();
}

''' + anchor)
# Do not reassign the shared registration or buffer until its actual owner is done.
replace('    uint8_t status;\n\n    switch (hci_event_packet_get_type(packet)){', '''    if (picow_avrcp_is_cancelled(avrcp_controller_connection)){
        if (hci_event_packet_get_type(packet) == SDP_EVENT_QUERY_COMPLETE){
            picow_avrcp_cancel_sdp_done(avrcp_controller_connection);
        }
        return;
    }
    uint8_t status;

    switch (hci_event_packet_get_type(packet)){''')
replace('    // prevent triggering SDP query twice (for each role once)', '''    if (picow_avrcp_is_cancelled(avrcp_controller_connection)){
        picow_avrcp_cancel_sdp_done(avrcp_controller_connection);
        return;
    }
    // prevent triggering SDP query twice (for each role once)''')
replace('        // connection seems to got finalized in the meantime, just trigger next query\n        avrcp_start_next_sdp_query();', '        // The registered callback has now been consumed.\n        avrcp_sdp_query_context.avrcp_cid = 0;\n        avrcp_start_next_sdp_query();')
replace('                    decline_connection = outoing_active || connection_already_established;', '''                    decline_connection = outoing_active || connection_already_established ||
                        picow_avrcp_is_cancelled(connection_controller) || picow_avrcp_is_cancelled(connection_target);''')
replace('                    switch (status){\n                        case ERROR_CODE_SUCCESS:', '''                    if (picow_avrcp_is_cancelled(connection_controller)){
                        if (connection_controller->state != PICOW_CANCEL_OPEN) break;
                        if (status == ERROR_CODE_SUCCESS){
                            connection_controller->l2cap_signaling_cid = local_cid;
                            connection_target->l2cap_signaling_cid = local_cid;
                            (void)picow_avrcp_cancel_transport(connection_controller, connection_target);
                        } else {
                            uint16_t cid = connection_controller->avrcp_cid;
                            if (avrcp_sdp_query_context.avrcp_cid == cid){
                                connection_controller->state = connection_target->state = PICOW_CANCEL_CLOSED_SDP;
                            } else {
                                picow_avrcp_release_pair(cid);
                            }
                        }
                        return;
                    }
                    switch (status){
                        case ERROR_CODE_SUCCESS:''')
replace('                    avrcp_emit_connection_closed(connection_controller->avrcp_cid);\n                    avrcp_finalize_connection(connection_controller);\n                    avrcp_finalize_connection(connection_target);', '''                    if (avrcp_sdp_query_context.avrcp_cid == connection_controller->avrcp_cid){
                        connection_controller->state = connection_target->state = PICOW_CANCEL_CLOSED_SDP;
                    } else {
                        picow_avrcp_release_pair(connection_controller->avrcp_cid);
                    }''')
replace('                    local_cid = l2cap_event_can_send_now_get_local_cid(packet);', '''                    local_cid = l2cap_event_can_send_now_get_local_cid(packet);
                    if (picow_avrcp_is_cancelled(avrcp_get_connection_for_l2cap_signaling_cid_for_role(AVRCP_CONTROLLER, local_cid))) break;''')
replace('        case L2CAP_DATA_PACKET:\n            switch (avrcp_get_frame_type(packet[0])){', '''        case L2CAP_DATA_PACKET:
            if (picow_avrcp_is_cancelled(avrcp_get_connection_for_l2cap_signaling_cid_for_role(AVRCP_CONTROLLER, channel))) break;
            if (size < 1u) break;
            switch (avrcp_get_frame_type(packet[0])){''')
replace('    if (setup_active){\n        return ERROR_CODE_SUCCESS;', '''    if (setup_active){
        if (avrcp_cid != NULL){
            *avrcp_cid = connection_controller != NULL ? connection_controller->avrcp_cid : connection_target->avrcp_cid;
        }
        return ERROR_CODE_SUCCESS;''')
replace('    connection_controller->trigger_sdp_query = true;', '''    if (picow_avrcp_is_cancelled(connection_controller) || picow_avrcp_is_cancelled(connection_target)) return;
    connection_controller->trigger_sdp_query = true;''')
# A synchronous allocation failure produces no L2CAP callback. Do not leave
# an unowned W4_L2CAP_CONNECTED pair that cancellation could never complete.
replace('static void avrcp_signaling_handle_sdp_query_complete(avrcp_connection_t * connection, uint8_t status){', '''static void picow_avrcp_create_signalling(avrcp_connection_t * c){
    uint16_t cid = c->avrcp_cid;
    bd_addr_t addr;
    memcpy(addr, c->remote_addr, sizeof(addr));
    hci_con_handle_t handle = c->con_handle;
    uint8_t status = l2cap_create_channel(&avrcp_packet_handler, addr, c->avrcp_l2cap_psm, l2cap_max_mtu(), NULL);
    if (status == ERROR_CODE_SUCCESS) return;
    c = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER, cid);
    avrcp_connection_t * t = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_TARGET, cid);
    if (c != NULL) avrcp_finalize_connection(c);
    if (t != NULL) avrcp_finalize_connection(t);
    avrcp_emit_connection_established(cid, addr, handle, status);
}

static void avrcp_signaling_handle_sdp_query_complete(avrcp_connection_t * connection, uint8_t status){''')
replace('            l2cap_create_channel(&avrcp_packet_handler, connection->remote_addr, connection->avrcp_l2cap_psm, l2cap_max_mtu(), NULL);', '            picow_avrcp_create_signalling(connection);')
replace('        l2cap_create_channel(&avrcp_packet_handler, connection_controller->remote_addr, connection_controller->avrcp_l2cap_psm, l2cap_max_mtu(), NULL);', '        picow_avrcp_create_signalling(connection_controller);')

# Keep the old public disconnect API, but make its lower errors observable.
replace('        l2cap_disconnect(connection_controller->browsing_connection->l2cap_browsing_cid);', '''        uint8_t status = l2cap_disconnect(connection_controller->browsing_connection->l2cap_browsing_cid);
        if (status != ERROR_CODE_SUCCESS) return status;''')
replace('    l2cap_disconnect(connection_controller->l2cap_signaling_cid);\n    return ERROR_CODE_SUCCESS;', '    return l2cap_disconnect(connection_controller->l2cap_signaling_cid);')
b.parent.mkdir(parents=True, exist_ok=True)
b.write_text(s)
print('Common overlay generated:', hashlib.sha256(s.encode()).hexdigest())
