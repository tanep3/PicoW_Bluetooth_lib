"""Repository-authored bounded settings continuation patch, applied after v1.
Generated source retains the upstream license. No SDK files are modified.
"""
STATE = r'''
#ifndef PICOW_AVRCP_SETTINGS_REASSEMBLY_BYTES
#define PICOW_AVRCP_SETTINGS_REASSEMBLY_BYTES 4096u
#endif
#if PICOW_AVRCP_SETTINGS_REASSEMBLY_BYTES < 1 || PICOW_AVRCP_SETTINGS_REASSEMBLY_BYTES > 65535
#error Invalid settings reassembly capacity
#endif
/* Two configured application peers; no heap allocation or borrowed fragments. */
typedef struct {
    avrcp_connection_t *owner;
    uint16_t cid, used, fragments;
    uint8_t pdu, ctype;
    uint8_t bytes[PICOW_AVRCP_SETTINGS_REASSEMBLY_BYTES];
} picow_settings_reassembly_t;
static picow_settings_reassembly_t picow_settings_reassembly[2];
void picow_avrcp_controller_forget_settings(uint16_t cid){
    for (unsigned i=0; i<2; ++i)
        if (picow_settings_reassembly[i].cid == cid)
            memset(&picow_settings_reassembly[i],0,sizeof(picow_settings_reassembly[i]));
}
static picow_settings_reassembly_t *picow_settings_find(avrcp_connection_t *c){
    for (unsigned i=0; i<2; ++i)
        if (picow_settings_reassembly[i].owner == c && picow_settings_reassembly[i].cid == c->avrcp_cid)
            return &picow_settings_reassembly[i];
    return NULL;
}
'''
HANDLER = r'''
/* Each continuation is a new AV/C transaction, referring to the original PDU.
 * This is distinct from AVCTP transport fragmentation. */
static void picow_settings_continue(avrcp_connection_t *c, uint8_t original, bool abort){
    c->state = AVCTP_W2_SEND_COMMAND;
    avrcp_controller_vendor_dependent_command_data_init(c, AVRCP_CTYPE_CONTROL,
        abort ? AVRCP_PDU_ID_REQUEST_ABORT_CONTINUING_RESPONSE : AVRCP_PDU_ID_REQUEST_CONTINUING_RESPONSE, false);
    c->transaction_id = avrcp_controller_calc_next_transaction_label(c->transaction_id_counter);
    c->transaction_id_counter = c->transaction_id;
    c->data[0] = original;
    c->data_len = 1;
    avrcp_request_can_send_now(c,c->l2cap_signaling_cid);
}
static bool picow_settings_fragment(avrcp_connection_t *c, uint8_t *packet, uint16_t size,
                                   uint8_t label, uint8_t ctype){
    if (size < 13u || c->state != AVCTP_W2_RECEIVE_RESPONSE || label != c->transaction_id) return true;
    const uint8_t pdu=packet[9], kind=packet[10]&3u;
    const uint16_t n=big_endian_read_16(packet,11);
    if (n != size-13u || !n || ctype != AVRCP_CTYPE_RESPONSE_IMPLEMENTED_STABLE) return true;
    picow_settings_reassembly_t *s=picow_settings_find(c);
    if (kind == AVRCP_START_PACKET){
        if (s || c->pdu_id != pdu) return true;
        for (unsigned i=0;i<2;++i) if (!picow_settings_reassembly[i].owner){s=&picow_settings_reassembly[i];break;}
        if (!s){picow_settings_continue(c,pdu,true);return true;}
        s->owner=c;s->cid=c->avrcp_cid;s->pdu=pdu;s->ctype=ctype;
    } else if (!s || s->pdu != pdu || s->ctype != ctype ||
               c->pdu_id != AVRCP_PDU_ID_REQUEST_CONTINUING_RESPONSE) return true;
    if (n > sizeof(s->bytes)-s->used || ++s->fragments > c->controller_max_num_fragments){
        picow_avrcp_controller_forget_settings(c->avrcp_cid);
        picow_settings_continue(c,pdu,true);return true;
    }
    memcpy(s->bytes+s->used,packet+13,n);s->used+=n;
    c->controller_last_confirmed_transaction_id=label;
    if (kind != AVRCP_END_PACKET){picow_settings_continue(c,pdu,false);return true;}
    if (!picow_avrcp_settings_payload_valid(pdu,ctype,s->bytes,s->used)){
        picow_avrcp_controller_forget_settings(c->avrcp_cid);
        picow_settings_continue(c,pdu,true);return true;
    }
    c->state=AVCTP_CONNECTION_OPENED;
    picow_avrcp_emit_settings_response(c,ctype,pdu,s->bytes,s->used);
    picow_avrcp_controller_forget_settings(c->avrcp_cid);
    if (c->controller_notifications_to_register) avrcp_request_can_send_now(c,c->l2cap_signaling_cid);
    return true;
}
'''
def apply(source):
    def replace(a,b):
        nonlocal source
        if source.count(a)!=1: raise ValueError('fragment patch anchor not unique: '+a[:80])
        source=source.replace(a,b,1)
    replace('static uint8_t avrcp_controller_calc_next_transaction_label',STATE+'\nstatic uint8_t avrcp_controller_calc_next_transaction_label')
    replace('    connection->transaction_id_counter = avrcp_controller_calc_next_transaction_label', '    picow_avrcp_controller_forget_settings(connection->avrcp_cid);\n    connection->transaction_id_counter = avrcp_controller_calc_next_transaction_label')
    replace('static void avrcp_handle_l2cap_data_packet_for_signaling_connection',HANDLER+'\nstatic void avrcp_handle_l2cap_data_packet_for_signaling_connection')
    replace('    /* Validate selected setting responses before changing connection state or', r'''    /* Settings transactions must not be consumed by unrelated vendor packets. */
    if (picow_avrcp_settings_pdu(connection->pdu_id) || picow_settings_find(connection)){
        if (opcode != AVRCP_CMD_OPCODE_VENDOR_DEPENDENT || size < 13u ||
            packet[1] != 0x11 || packet[2] != 0x0e ||
            packet[6] != 0 || packet[7] != 0x19 || packet[8] != 0x58 ||
            (!picow_avrcp_settings_pdu(packet[9]) &&
             packet[9] != AVRCP_PDU_ID_REGISTER_NOTIFICATION &&
             packet[9] != AVRCP_PDU_ID_REQUEST_ABORT_CONTINUING_RESPONSE &&
             packet[9] != AVRCP_PDU_ID_REQUEST_CONTINUING_RESPONSE)) return;
        picow_settings_reassembly_t *pending=picow_settings_find(connection);
        if (pending && packet[9] == AVRCP_PDU_ID_REQUEST_CONTINUING_RESPONSE){
            uint16_t n=big_endian_read_16(packet,11);
            if (connection->state != AVCTP_W2_RECEIVE_RESPONSE || received_transaction_id != connection->transaction_id ||
                packet[10] != AVRCP_SINGLE_PACKET || n != size-13u || n > 1u ||
                (ctype != AVRCP_CTYPE_RESPONSE_REJECTED && ctype != AVRCP_CTYPE_RESPONSE_NOT_IMPLEMENTED)) return;
            connection->state=AVCTP_CONNECTION_OPENED;
            connection->controller_last_confirmed_transaction_id=received_transaction_id;
            picow_avrcp_emit_settings_response(connection,ctype,pending->pdu,packet+13,n);
            picow_avrcp_controller_forget_settings(connection->avrcp_cid);
            return;
        }
    }
    /* Validate selected setting responses before changing connection state or''')

    replace('''        picow_avrcp_settings_pdu(packet[9])){
        if (size < 13u''','''        picow_avrcp_settings_pdu(packet[9])){
        if (size >= 13u && (packet[10] & 3u) != AVRCP_SINGLE_PACKET){
            picow_settings_fragment(connection,packet,size,received_transaction_id,ctype);
            return;
        }
        if (size < 13u''')
    replace('void avrcp_controller_init(void){','void avrcp_controller_init(void){\n    memset(picow_settings_reassembly,0,sizeof(picow_settings_reassembly));')
    replace('void avrcp_controller_deinit(void){','void avrcp_controller_deinit(void){\n    memset(picow_settings_reassembly,0,sizeof(picow_settings_reassembly));')
    return source
