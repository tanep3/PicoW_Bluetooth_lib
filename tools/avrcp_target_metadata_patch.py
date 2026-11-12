"""Target metadata continuation edits applied after the pinned SDK overlay.

The generated source remains derived from BTstack and retains its license.
No SDK struct/ABI change or separate connection-state allocation is required.
"""

def patch_metadata(source):
    def replace(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise SystemExit('Metadata overlay anchor missing/ambiguous: ' + old[:80])
        source = source.replace(old, new)

    replace('static void avrcp_send_response_with_avctp_fragmentation(avrcp_connection_t * connection){', r'''
/* The AV/C 512-byte limit excludes AVCTP. Metadata can span multiple vendor
 * responses, each independently fragmented by AVCTP. data_offset remains the
 * global attribute-stream cursor. avrcp_frame_bytes_sent counts AV/C bytes in
 * the current vendor response only; REQUEST_CONTINUING_RESPONSE resets it.
 * Retained metadata pointers stay owned by the caller until finish or abort. */
static void picow_avrcp_send_metadata(avrcp_connection_t * connection){
    const uint16_t vendor_header = 10;
    const uint16_t vendor_capacity = AVRCP_MAX_AV_C_MESSAGE_FRAME_SIZE - vendor_header;
    const uint16_t sent = connection->avrcp_frame_bytes_sent;
    const uint16_t params_sent = sent == 0 ? 0 : sent - vendor_header;
    const uint16_t response_start = connection->data_offset - params_sent;
    const uint16_t remaining = connection->data_len - response_start;
    const uint16_t params = btstack_min(remaining, vendor_capacity);
    const uint16_t avc_size = vendor_header + params;
    const uint16_t mtu = btstack_min(connection->l2cap_mtu, AVRCP_MAX_AV_C_MESSAGE_FRAME_SIZE);
    const bool fragmented = avc_size + 3u > mtu;
    btstack_assert(mtu >= 48u && sent < avc_size);
    avctp_packet_type_t transport;
    if (!fragmented) transport = AVCTP_SINGLE_PACKET;
    else if (sent == 0) transport = AVCTP_START_PACKET;
    else transport = avc_size - sent <= mtu - 1u ? AVCTP_END_PACKET : AVCTP_CONTINUE_PACKET;
    connection->avctp_packet_type = transport;
    if (sent == 0){
        if (response_start == 0) connection->avrcp_packet_type = remaining <= vendor_capacity ? AVRCP_SINGLE_PACKET : AVRCP_START_PACKET;
        else connection->avrcp_packet_type = remaining <= vendor_capacity ? AVRCP_END_PACKET : AVRCP_CONTINUE_PACKET;
    }
    l2cap_reserve_packet_buffer();
    uint8_t *packet = l2cap_get_outgoing_buffer();
    uint16_t pos = 0;
    packet[pos++] = (connection->transaction_id << 4) | (transport << 2) | (AVRCP_RESPONSE_FRAME << 1);
    if (transport == AVCTP_START_PACKET){
        const uint16_t first = mtu - 4u;
        packet[pos++] = 1u + (avc_size - first + mtu - 2u) / (mtu - 1u);
    }
    if (sent == 0){
        packet[pos++] = 0x11; packet[pos++] = 0x0e;
        packet[pos++] = connection->command_type;
        packet[pos++] = (connection->subunit_type << 3) | connection->subunit_id;
        packet[pos++] = AVRCP_CMD_OPCODE_VENDOR_DEPENDENT;
        big_endian_store_24(packet, pos, connection->company_id); pos += 3;
        packet[pos++] = AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES;
        packet[pos++] = connection->avrcp_packet_type;
        big_endian_store_16(packet, pos, params); pos += 2;
        connection->avrcp_frame_bytes_sent = vendor_header;
    }
    uint16_t capacity = btstack_min(mtu - pos, params - params_sent);
    if (connection->data_offset == 0){
        packet[pos++] = count_set_bits_uint32(connection->target_now_playing_info_attr_bitmap);
        --capacity; ++connection->data_offset; ++connection->avrcp_frame_bytes_sent;
    }
    uint16_t copied = avrcp_store_avctp_now_playing_info_fragment(connection, capacity, packet + pos);
    connection->avrcp_frame_bytes_sent += copied;
    l2cap_send_prepared(connection->l2cap_signaling_cid, pos + copied);
}

static void avrcp_send_response_with_avctp_fragmentation(avrcp_connection_t * connection){
    if (connection->command_opcode == AVRCP_CMD_OPCODE_VENDOR_DEPENDENT &&
        connection->pdu_id == AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES &&
        connection->command_type == AVRCP_CTYPE_RESPONSE_IMPLEMENTED_STABLE){
        picow_avrcp_send_metadata(connection);
        return;
    }''')

    replace('    if (packet == NULL || size < 6u) return;', '    if (connection == NULL || packet == NULL || size < 6u) return;')

    # GET_ELEMENT_ATTRIBUTES rejection is an ordinary one-byte status response,
    # not an attribute stream. Successful metadata returns through the new path.
    replace('                        case AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES:\n                            packet[pos++]', '                        case AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES:\n                            if (connection->command_type != AVRCP_CTYPE_RESPONSE_IMPLEMENTED_STABLE) break;\n                            packet[pos++]')

    # Ignore unrelated traffic while retaining metadata, before changing the
    # transaction label or serializer state. A continuing/abort request can only
    # be consumed after the current AVCTP message has finished. Truncated or
    # mismatched requests during this interval must not destroy the stream.
    replace('    uint8_t avctp_header = packet[0];', r'''
    if (connection == NULL) return;
    if (connection->target_now_playing_info_response ||
        (connection->pdu_id == AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES && connection->command_type == AVRCP_CTYPE_RESPONSE_IMPLEMENTED_STABLE && connection->data_offset < connection->data_len)){
        if (connection->state != AVCTP_CONNECTION_OPENED || size != 14u ||
            (packet[0] & 0x0fu) != 0 || packet[1] != 0x11 || packet[2] != 0x0e ||
            packet[3] != AVRCP_CTYPE_CONTROL || packet[4] != 0x48 ||
            packet[5] != AVRCP_CMD_OPCODE_VENDOR_DEPENDENT ||
            packet[6] != 0 || packet[7] != 0x19 || packet[8] != 0x58 ||
            (packet[9] != AVRCP_PDU_ID_REQUEST_CONTINUING_RESPONSE && packet[9] != AVRCP_PDU_ID_REQUEST_ABORT_CONTINUING_RESPONSE) ||
            packet[10] != 0 || packet[11] != 0 || packet[12] != 1 ||
            packet[13] != AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES) return;
    }
    uint8_t avctp_header = packet[0];''')
    a = source.index('                case AVRCP_PDU_ID_REQUEST_ABORT_CONTINUING_RESPONSE:')
    b = source.index('                case AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES:{', a)
    source = source[:a] + r'''                case AVRCP_PDU_ID_REQUEST_ABORT_CONTINUING_RESPONSE:
                case AVRCP_PDU_ID_REQUEST_CONTINUING_RESPONSE:
                    if (length != 1 || packet[pos] != AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES ||
                        connection->pdu_id != AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES ||
                        connection->data_offset >= connection->data_len){
                        avrcp_target_response_vendor_dependent_reject(connection, pdu_id, AVRCP_STATUS_INVALID_PARAMETER);
                        return;
                    }
                    if (pdu_id == AVRCP_PDU_ID_REQUEST_ABORT_CONTINUING_RESPONSE){
                        connection->target_abort_continue_response = true;
                    } else {
                        connection->avrcp_frame_bytes_sent = 0;
                        connection->target_continue_response = true;
                        connection->target_now_playing_info_response = true;
                    }
                    connection->state = AVCTP_W2_SEND_RESPONSE;
                    avrcp_request_can_send_now(connection, connection->l2cap_signaling_cid);
                    break;
''' + source[b:]
    replace('''                    if ((pos + 9) > size) return;
                    uint8_t play_identifier[8];''', '''                    if (length < 9 || length != 9u + 4u * packet[pos + 8]) {
                        avrcp_target_response_vendor_dependent_reject(connection, pdu_id, AVRCP_STATUS_INVALID_PARAMETER);
                        return;
                    }
                    uint8_t play_identifier[8];''')
    replace('''                    uint8_t attribute_count = packet[pos++];
                    connection->next_attr_id''','''                    uint8_t attribute_count = packet[pos++];
                    connection->attribute_value_offset = 0;
                    connection->attribute_value_len = 0;
                    connection->parser_attribute_header_pos = 0;
                    connection->next_attr_id''')
    replace('''                    connection->target_abort_continue_response = false;
                    avrcp_target_vendor_dependent_response_data_init''','''                    connection->target_abort_continue_response = false;
                    connection->target_continue_response = false;
                    connection->target_now_playing_info_response = false;
                    connection->attribute_value_offset = 0;
                    connection->attribute_value_len = 0;
                    connection->parser_attribute_header_pos = 0;
                    avrcp_target_vendor_dependent_response_data_init''')
    # Reject metadata replacement at the raw SDK API too. The facade already
    # owns/copies text and has an equivalent guard before changing its storage.
    replace('''    if (!current_track){
        return ERROR_CODE_COMMAND_DISALLOWED;
    }''','''    if (!current_track || connection->state != AVCTP_CONNECTION_OPENED ||
        connection->target_now_playing_info_response ||
        (connection->pdu_id == AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES && connection->command_type == AVRCP_CTYPE_RESPONSE_IMPLEMENTED_STABLE && connection->data_offset < connection->data_len)){
        return ERROR_CODE_COMMAND_DISALLOWED;
    }''')
    replace('''            // Notifications

            if (connection->target_track_changed)''','''            // Defer notifications until the retained metadata response ends.
            if (connection->pdu_id == AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES &&
                connection->command_type == AVRCP_CTYPE_RESPONSE_IMPLEMENTED_STABLE &&
                connection->data_offset < connection->data_len) return;

            // Notifications

            if (connection->target_track_changed)''')
    # Finishing or aborting resumes flags whose original CAN_SEND_NOW event was
    # consumed while waiting for REQUEST_CONTINUING_RESPONSE.
    replace('''    avrcp_request_next_avctp_segment(connection);
}''','''    avrcp_request_next_avctp_segment(connection);
    if (connection->state == AVCTP_CONNECTION_OPENED &&
        (connection->pdu_id != AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES || connection->data_offset == connection->data_len) &&
        (connection->target_track_changed || connection->target_playback_status_changed ||
         connection->target_playing_content_changed || connection->target_battery_status_changed ||
         connection->target_notify_absolute_volume_changed || connection->target_addressed_player_changed ||
         connection->target_uids_changed)){
        avrcp_request_can_send_now(connection, connection->l2cap_signaling_cid);
    }
}''')
    return source
