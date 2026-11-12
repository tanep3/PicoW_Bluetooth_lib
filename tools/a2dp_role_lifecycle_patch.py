"""Experimental role-only join/close/retire on an existing AVDTP signaling CID.
Applied only after the original a2dp.c hash is validated by its generator.
Generated code retains upstream BTstack licensing. Not production enabled.
"""
def patch_role_lifecycle(name, text):
    if name != "a2dp.c":
        return text
    if "picow_a2dp_join_existing_role" in text:
        raise ValueError("Role lifecycle fragment already applied")
    anchor = 'void a2dp_config_process_avdtp_event_handler(avdtp_role_t role, uint8_t *packet, uint16_t size) {'
    if text.count(anchor) != 1:
        raise ValueError('A2DP dispatcher anchor mismatch')
    prefix = r'''
typedef struct { uint16_t cid; hci_con_handle_t handle; bool active; } picow_a2dp_cancel_t;
static picow_a2dp_cancel_t picow_a2dp_cancels[2];
static bool picow_a2dp_cancel_event(avdtp_role_t role, const uint8_t *packet, uint16_t size);
'''
    text = text.replace(anchor, prefix + anchor + '\n    if (picow_a2dp_cancel_event(role, packet, size)) return;')
    return text + r'''

// Experimental shared-role API. The facade must validate its current runtime
// ACL binding and arm its Connecting state before join's synchronous callback.
static bool picow_a2dp_signaling_idle(avdtp_connection_t *connection) {
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED ||
        connection->initiator_connection_state != AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE ||
        connection->acceptor_connection_state != AVDTP_SIGNALING_CONNECTION_ACCEPTOR_IDLE ||
        connection->configuration_state == AVDTP_CONFIGURATION_STATE_LOCAL_INITIATED ||
        connection->configuration_state == AVDTP_CONFIGURATION_STATE_REMOTE_INITIATED) return false;
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, avdtp_get_stream_endpoints());
    while (btstack_linked_list_iterator_has_next(&it)) {
        avdtp_stream_endpoint_t *endpoint = (avdtp_stream_endpoint_t *)btstack_linked_list_iterator_next(&it);
        if (endpoint->connection != connection) continue;
        if (endpoint->initiator_config_state != AVDTP_INITIATOR_STREAM_CONFIG_IDLE ||
            endpoint->acceptor_config_state != AVDTP_ACCEPTOR_STREAM_CONFIG_IDLE ||
            endpoint->start_stream || endpoint->suspend_stream || endpoint->close_stream ||
            endpoint->abort_stream || endpoint->media_connect || endpoint->media_disconnect) return false;
    }
    return true;
}

uint8_t picow_a2dp_join_existing_role(avdtp_role_t role, uint16_t cid) {
    if (role != AVDTP_ROLE_SOURCE && role != AVDTP_ROLE_SINK) return ERROR_CODE_COMMAND_DISALLOWED;
    avdtp_connection_t *connection = avdtp_get_connection_for_avdtp_cid(cid);
    if (connection == NULL) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    if (!picow_a2dp_signaling_idle(connection)) return ERROR_CODE_COMMAND_DISALLOWED;
    if (picow_a2dp_cancels[role == AVDTP_ROLE_SOURCE ? 0u : 1u].active) return ERROR_CODE_COMMAND_DISALLOWED;
    a2dp_config_process_t *process = a2dp_config_process_for_role(role, connection);
    a2dp_config_process_t *other = a2dp_config_process_for_role(
        role == AVDTP_ROLE_SOURCE ? AVDTP_ROLE_SINK : AVDTP_ROLE_SOURCE, connection);
    if (process->outgoing_active || process->discover_seps || process->stream_endpoint_configured ||
        (process->state != A2DP_IDLE && process->state != A2DP_CONNECTED) ||
        other->discover_seps ||
        (other->state != A2DP_IDLE && other->state != A2DP_CONNECTED &&
         other->state != A2DP_CONFIGURED && other->state != A2DP_STREAMING_OPENED))
        return ERROR_CODE_COMMAND_DISALLOWED;
    if ((role == AVDTP_ROLE_SOURCE && a2dp_source_callback == NULL) ||
        (role == AVDTP_ROLE_SINK && a2dp_sink_callback == NULL)) return ERROR_CODE_COMMAND_DISALLOWED;
    process->outgoing_active = true;
    uint8_t event[14] = {HCI_EVENT_AVDTP_META, 12, AVDTP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED};
    little_endian_store_16(event, 3, cid);
    reverse_bd_addr(connection->remote_addr, &event[5]);
    little_endian_store_16(event, 11, connection->con_handle);
    // Role-local notification: never broadcast a duplicate connection event to
    // the already-running role on this same signaling channel.
    a2dp_config_process_avdtp_event_handler(role, event, sizeof(event));
    return ERROR_CODE_SUCCESS;
}

uint8_t picow_a2dp_close_role_media(avdtp_role_t role, uint16_t cid, uint8_t seid) {
    if (role != AVDTP_ROLE_SOURCE && role != AVDTP_ROLE_SINK) return ERROR_CODE_COMMAND_DISALLOWED;
    avdtp_connection_t *connection = avdtp_get_connection_for_avdtp_cid(cid);
    if (connection == NULL) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    if (!picow_a2dp_signaling_idle(connection)) return ERROR_CODE_COMMAND_DISALLOWED;
    a2dp_config_process_t *process = a2dp_config_process_for_role(role, connection);
    avdtp_stream_endpoint_t *endpoint = process->local_stream_endpoint;
    if (process->state != A2DP_STREAMING_OPENED || endpoint == NULL ||
        avdtp_local_seid(endpoint) != seid || endpoint->connection != connection ||
        (role == AVDTP_ROLE_SOURCE && endpoint->sep.type != AVDTP_SOURCE) ||
        (role == AVDTP_ROLE_SINK && endpoint->sep.type != AVDTP_SINK) ||
        endpoint->l2cap_media_cid == 0 ||
        (endpoint->state != AVDTP_STREAM_ENDPOINT_OPENED && endpoint->state != AVDTP_STREAM_ENDPOINT_STREAMING))
        return ERROR_CODE_COMMAND_DISALLOWED;
    return avdtp_stop_stream(cid, avdtp_local_seid(endpoint));
}

uint8_t picow_a2dp_retire_role(avdtp_role_t role, uint16_t cid, uint8_t seid) {
    if (role != AVDTP_ROLE_SOURCE && role != AVDTP_ROLE_SINK) return ERROR_CODE_COMMAND_DISALLOWED;
    avdtp_connection_t *connection = avdtp_get_connection_for_avdtp_cid(cid);
    if (connection == NULL) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) return ERROR_CODE_COMMAND_DISALLOWED;
    a2dp_config_process_t *process = a2dp_config_process_for_role(role, connection);
    if (process->outgoing_active || process->discover_seps ||
        (a2dp_config_process_sep_discovery_cid == cid && a2dp_config_process_role == role) ||
        (process->state != A2DP_CONFIGURED && process->state != A2DP_CONNECTED && process->state != A2DP_IDLE))
        return ERROR_CODE_COMMAND_DISALLOWED;
    avdtp_stream_endpoint_t *endpoint = process->local_stream_endpoint;
    avdtp_stream_endpoint_t *expected = avdtp_get_stream_endpoint_for_seid(seid);
    if (expected == NULL || (endpoint != NULL && endpoint != expected) ||
        (role == AVDTP_ROLE_SOURCE && expected->sep.type != AVDTP_SOURCE) ||
        (role == AVDTP_ROLE_SINK && expected->sep.type != AVDTP_SINK)) return ERROR_CODE_COMMAND_DISALLOWED;
    // STREAM_RELEASED is emitted before the SDK resets the SEP. Call retire on
    // a later main-loop tick, never from inside that release callback.
    if (expected->state != AVDTP_STREAM_ENDPOINT_IDLE || expected->sep.in_use ||
        expected->l2cap_media_cid || expected->connection != NULL ||
        connection->active_stream_endpoint == expected) return ERROR_CODE_COMMAND_DISALLOWED;
    memset(process, 0, sizeof(*process));
    process->state = A2DP_IDLE;
    return ERROR_CODE_SUCCESS;
}


static unsigned picow_a2dp_role_index(avdtp_role_t role) { return role == AVDTP_ROLE_SOURCE ? 0u : 1u; }
static bool picow_a2dp_cancel_event(avdtp_role_t role, const uint8_t *packet, uint16_t size) {
    if ((role != AVDTP_ROLE_SOURCE && role != AVDTP_ROLE_SINK) || size < 5) return false;
    picow_a2dp_cancel_t *cancel = &picow_a2dp_cancels[picow_a2dp_role_index(role)];
    if (!cancel->active || little_endian_read_16(packet, 3) != cancel->cid) return false;
    avdtp_connection_t *connection = avdtp_get_connection_for_avdtp_cid(cancel->cid);
    if (connection == NULL || connection->con_handle != cancel->handle ||
        packet[2] == AVDTP_SUBEVENT_SIGNALING_CONNECTION_RELEASED) {
        cancel->active = false;
        return false;
    }
    // Lower AVDTP still parses/completes the wire transaction. Only suppress
    // this role's automatic next negotiation stage; never reset the initiator.
    return true;
}

uint8_t picow_a2dp_cancel_role_discovery(avdtp_role_t role, uint16_t cid) {
    if (role != AVDTP_ROLE_SOURCE && role != AVDTP_ROLE_SINK) return ERROR_CODE_COMMAND_DISALLOWED;
    avdtp_connection_t *connection = avdtp_get_connection_for_avdtp_cid(cid);
    if (connection == NULL) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) return ERROR_CODE_COMMAND_DISALLOWED;
    picow_a2dp_cancel_t *cancel = &picow_a2dp_cancels[picow_a2dp_role_index(role)];
    if (cancel->active) return cancel->cid == cid && cancel->handle == connection->con_handle ?
        ERROR_CODE_SUCCESS : ERROR_CODE_COMMAND_DISALLOWED;
    a2dp_config_process_t *process = a2dp_config_process_for_role(role, connection);
    if (process->stream_endpoint_configured ||
        (process->state != A2DP_IDLE && process->state != A2DP_CONNECTED &&
         process->state != A2DP_DISCOVER_SEPS && process->state != A2DP_GET_CAPABILITIES &&
         process->state != A2DP_W2_GET_ALL_CAPABILITIES && process->state != A2DP_DISCOVERY_DONE))
        return ERROR_CODE_COMMAND_DISALLOWED;
    avdtp_stream_endpoint_t *endpoint = process->local_stream_endpoint;
    if (endpoint != NULL && (endpoint->connection != NULL || endpoint->sep.in_use ||
        endpoint->state != AVDTP_STREAM_ENDPOINT_IDLE)) return ERROR_CODE_COMMAND_DISALLOWED;
    cancel->cid = cid; cancel->handle = connection->con_handle; cancel->active = true;
    process->discover_seps = false;
    if (a2dp_config_process_sep_discovery_cid == cid && a2dp_config_process_role == role)
        a2dp_config_process_timer_stop();
    return ERROR_CODE_SUCCESS;
}

uint8_t picow_a2dp_poll_role_cancel(avdtp_role_t role, uint16_t cid) {
    if (role != AVDTP_ROLE_SOURCE && role != AVDTP_ROLE_SINK) return ERROR_CODE_COMMAND_DISALLOWED;
    picow_a2dp_cancel_t *cancel = &picow_a2dp_cancels[picow_a2dp_role_index(role)];
    if (!cancel->active || cancel->cid != cid) return ERROR_CODE_COMMAND_DISALLOWED;
    avdtp_connection_t *connection = avdtp_get_connection_for_avdtp_cid(cid);
    if (connection == NULL || connection->con_handle != cancel->handle) {
        cancel->active = false; return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    }
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) return ERROR_CODE_COMMAND_DISALLOWED;
    const bool owns_discovery = a2dp_config_process_sep_discovery_cid == cid && a2dp_config_process_role == role;
    // Wait for the real DISCOVER/capabilities transaction (including SDP) to
    // drain. There is no local reset of W2/W4, packet state, labels or timers.
    if (owns_discovery && connection->initiator_connection_state != AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE)
        return ERROR_CODE_COMMAND_DISALLOWED;
    a2dp_config_process_t *process = a2dp_config_process_for_role(role, connection);
    avdtp_stream_endpoint_t *endpoint = process->local_stream_endpoint;
    if (endpoint != NULL && (endpoint->connection != NULL || endpoint->sep.in_use ||
        endpoint->state != AVDTP_STREAM_ENDPOINT_IDLE)) return ERROR_CODE_COMMAND_DISALLOWED;
    if (owns_discovery) {
        a2dp_config_process_timer_stop();
        a2dp_config_process_sep_discovery_cid = 0;
        a2dp_config_process_sep_discovery_count = 0;
        a2dp_config_process_sep_discovery_index = 0;
    }
    memset(process, 0, sizeof(*process)); process->state = A2DP_IDLE;
    cancel->active = false;
    // Caller resumes admitted queued owners after this main-loop operation.
    return ERROR_CODE_SUCCESS;
}
'''
