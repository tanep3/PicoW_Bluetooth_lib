"""Owned custom framing, continuation and notification coexistence.
Generated SDK sources retain their original license. Installed files are unchanged.
"""
CODE = r'''
#define PICOW_CUSTOM_EVENT 0xf1u
#define PICOW_CUSTOM_ERROR 0xf2u
#define PICOW_CUSTOM_REQUEST_MAX 502u
#define PICOW_CUSTOM_RESPONSE_MAX 4096u
typedef struct {
    avrcp_connection_t *owner;
    uint16_t cid, used, tx_size, tx_offset, vendor_used, vendor_fragments, notifications;
    uint8_t label, remaining, tx_parts, original_pdu, vendor_ctype;
    uint8_t notification_labels[14];
    bool pending, sending, retry, continuing, aborting;
    uint8_t request[PICOW_CUSTOM_REQUEST_MAX+10];
    uint8_t response[PICOW_CUSTOM_RESPONSE_MAX];
    uint8_t vendor[PICOW_CUSTOM_RESPONSE_MAX];
} picow_custom_t;
static picow_custom_t picow_custom[2];
static picow_custom_t *picow_custom_find(avrcp_connection_t *c){
    for (unsigned i=0;i<2;++i)
        if (picow_custom[i].owner==c && picow_custom[i].cid==c->avrcp_cid) return &picow_custom[i];
    return NULL;
}
static picow_custom_t *picow_custom_slot(avrcp_connection_t *c){
    picow_custom_t *s=picow_custom_find(c);
    if(s)return s;
    for(unsigned i=0;i<2;++i)if(!picow_custom[i].owner){
        s=&picow_custom[i];s->owner=c;s->cid=c->avrcp_cid;return s;
    }
    return NULL;
}
void picow_avrcp_controller_forget_custom(uint16_t cid){
    for(unsigned i=0;i<2;++i)if(picow_custom[i].cid==cid){
        /* Never erase bytes while a lower layer might still retain them. */
        picow_custom_t *s=&picow_custom[i];s->owner=NULL;s->cid=0;
        s->pending=s->sending=s->retry=s->continuing=s->aborting=false;
        s->remaining=0;s->used=s->vendor_used=s->notifications=0;
    }
}
/* Terminal notification responses retire the label on the ordinary path too. */
static void picow_custom_retire_notification(avrcp_connection_t *c,uint8_t event){
    picow_custom_t *s=picow_custom_find(c);
    if(s)s->notifications&=~(1u<<event);
}
static void picow_custom_track_notification(avrcp_connection_t *c,uint8_t event,uint8_t label){
    if(event<1 || event>13)return;
    picow_custom_t *s=picow_custom_slot(c);if(!s)return;
    s->notifications|=1u<<event;s->notification_labels[event]=label;
}
static uint8_t picow_custom_label(avrcp_connection_t *c,picow_custom_t *s){
    for(unsigned attempt=0;attempt<15;++attempt){
        uint8_t label=avrcp_controller_get_next_transaction_label(c);bool used=false;
        for(unsigned event=1;event<=13;++event)
            if((s->notifications&(1u<<event)) && s->notification_labels[event]==label)used=true;
        if(!used)return label;
    }
    return 0; /* At most 13 notification labels, so a custom label is available. */
}
static void picow_custom_error(picow_custom_t *s,uint8_t reason){
    uint8_t event[]={HCI_EVENT_AVRCP_META,5,PICOW_CUSTOM_ERROR,0,0,reason,s->original_pdu};
    little_endian_store_16(event,3,s->cid);
    avrcp_controller_context.avrcp_callback(HCI_EVENT_PACKET,0,event,sizeof(event));
}
static void picow_custom_emit(picow_custom_t *s,uint8_t response,const uint8_t *p,uint16_t total){
    for(uint16_t off=0;;){
        uint16_t take=btstack_min(246,total-off);uint8_t event[257];
        event[0]=HCI_EVENT_AVRCP_META;event[1]=9+take;event[2]=PICOW_CUSTOM_EVENT;
        little_endian_store_16(event,3,s->cid);event[5]=response;event[6]=s->original_pdu;
        little_endian_store_16(event,7,total);little_endian_store_16(event,9,off);
        if(take)memcpy(event+11,p+off,take);
        avrcp_controller_context.avrcp_callback(HCI_EVENT_PACKET,0,event,11+take);
        off+=take;if(off==total)break;
    }
}
static void picow_custom_prepare(avrcp_connection_t *c,picow_custom_t *s,uint8_t pdu,
                                 uint8_t command,const uint8_t *data,uint16_t n){
    s->label=picow_custom_label(c,s);c->transaction_id=s->label;
    c->state=AVCTP_W2_SEND_COMMAND;c->pdu_id=(avrcp_pdu_id_t)pdu;c->command_type=(avrcp_command_type_t)command;
    uint8_t *p=s->request;p[0]=command;p[1]=(c->subunit_type<<3)|c->subunit_id;
    p[2]=AVRCP_CMD_OPCODE_VENDOR_DEPENDENT;big_endian_store_24(p,3,c->company_id);
    p[6]=pdu;p[7]=0;big_endian_store_16(p,8,n);if(n)memcpy(p+10,data,n);
    s->tx_size=n+10;s->tx_offset=0;s->tx_parts=0;s->sending=true;s->retry=false;
    c->data=p+10;c->data_len=n;c->data_offset=0;
    avrcp_request_can_send_now(c,c->l2cap_signaling_cid);
}
/* Local-owned framer: SDK's legacy helper leaves max_payload_size undefined
 * at MTU>=512 and subtracts header capacity twice at smaller MTUs. */
static bool picow_custom_send(avrcp_connection_t *c){
    picow_custom_t *s=picow_custom_find(c);if(!s || !s->pending)return false;
    if(!s->sending)return true; // Do not let queued notification sends steal label/state.
    uint16_t mtu=btstack_min(c->l2cap_mtu,512);if(mtu<48)return true;
    uint8_t frame[512];unsigned header,type;
    if(!s->tx_offset){
        if(s->tx_size<=mtu-3){type=0;header=3;}
        else{type=1;header=4;s->tx_parts=1+(s->tx_size-(mtu-4)+(mtu-2))/(mtu-1);}
    }else{type=s->tx_size-s->tx_offset<=mtu-1?3:2;header=1;}
    frame[0]=(s->label<<4)|(type<<2);
    if(type<=1){if(type)frame[1]=s->tx_parts;frame[header-2]=0x11;frame[header-1]=0x0e;}
    uint16_t take=btstack_min(s->tx_size-s->tx_offset,mtu-header);
    memcpy(frame+header,s->request+s->tx_offset,take);
    uint8_t rc=l2cap_send(c->l2cap_signaling_cid,frame,header+take);
    if(rc){if(!s->retry)picow_custom_error(s,4);s->retry=true;return true;}
    s->retry=false;s->tx_offset+=take;
    if(s->tx_offset==s->tx_size){s->sending=false;c->state=AVCTP_W2_RECEIVE_RESPONSE;}
    else avrcp_request_can_send_now(c,c->l2cap_signaling_cid);
    return true;
}
void picow_avrcp_controller_custom_poll(uint16_t cid){
    avrcp_connection_t *c=avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER,cid);
    picow_custom_t *s=c?picow_custom_find(c):NULL;
    if(s && s->pending && s->sending && s->retry)avrcp_request_can_send_now(c,c->l2cap_signaling_cid);
}
static void picow_custom_continue(avrcp_connection_t *c,picow_custom_t *s,bool abort){
    uint8_t pdu=s->original_pdu;s->aborting=abort;s->continuing=!abort;
    picow_custom_prepare(c,s,abort?0x41:0x40,0,&pdu,1);
}
static bool picow_custom_notification(avrcp_connection_t *c,picow_custom_t *s,const uint8_t *p,uint16_t n){
    if(n<13 || (p[0]&15)!=2 || p[1]!=0x11 || p[2]!=0x0e || p[4]!=0x48 ||
       p[5]!=0 || big_endian_read_24(p,6)!=BT_SIG_COMPANY_ID || p[9]!=0x31 || p[10]!=0 ||
       big_endian_read_16(p,11)!=n-13)return false;
    uint8_t event=0,label=p[0]>>4;
    if((p[3]==13 || p[3]==15) && n>=14 && p[13]>=1 && p[13]<=13){
        uint8_t candidate=p[13];
        if((s->notifications&(1u<<candidate)) && s->notification_labels[candidate]==label)event=candidate;
    }else{
        for(unsigned i=1;i<=13;++i)if((s->notifications&(1u<<i))&&s->notification_labels[i]==label){
            if(event)return true; // Ambiguous rejection must not retire another subscription.
            event=i;
        }
    }
    if(!event)return false;
    if(p[3]==8 || p[3]==10){
        if(n!=14)return true;
        uint8_t payload=event;avrcp_controller_handle_notification(c,AVRCP_CTYPE_RESPONSE_REJECTED,&payload,1);
        s->notifications&=~(1u<<event);return true;
    }
    if((p[3]!=13 && p[3]!=15) || n<14 || p[13]!=event)return true;
    const uint8_t sizes[]={0,1,8,0,0,4,1,1,0,0,0,4,2,1};
    if(event==8){if(n<15 || n!=15u+2u*p[14])return true;}
    else if(n!=14u+sizes[event])return true;
    /* A valid existing subscription may change during custom work. Queue its
     * renewal, but never start a capabilities query over the live transaction. */
    uint8_t capabilities=c->remote_capabilities_state;
    if(capabilities==AVRCP_REMOTE_CAPABILITIES_NONE)
        c->remote_capabilities_state=AVRCP_REMOTE_CAPABILITIES_W4_QUERY_RESULT;
    avrcp_controller_handle_notification(c,(avrcp_command_type_t)p[3],(uint8_t*)p+13,n-13);
    c->remote_capabilities_state=capabilities;
    if(p[3]==13)s->notifications&=~(1u<<event);
    return true;
}
static bool picow_custom_receive(avrcp_connection_t *c,const uint8_t *p,uint16_t n){
    picow_custom_t *s=picow_custom_find(c);if(!s || !s->pending)return false;
    if(p && picow_custom_notification(c,s,p,n))return true;
    if(!p || !n || (p[0]&3)!=2 || (p[0]>>4)!=s->label || c->state!=AVCTP_W2_RECEIVE_RESPONSE)return true;
    unsigned type=(p[0]>>2)&3,header=1;
    if(type<=1){header=type?4:3;
        if(n<header || p[header-2]!=0x11 || p[header-1]!=0x0e || (type&&p[1]<2))return true;
        if(s->remaining)return true;s->used=0;s->remaining=type?p[1]:1;
    }else if(!s->remaining || (type==3?s->remaining!=1:s->remaining<=1))return true;
    if(n<=header){s->remaining=0;s->used=0;return true;}
    if(n-header>PICOW_CUSTOM_RESPONSE_MAX-s->used){s->remaining=0;s->used=0;picow_custom_error(s,2);picow_custom_continue(c,s,true);return true;}
    memcpy(s->response+s->used,p+header,n-header);s->used+=n-header;if(--s->remaining)return true;
    p=s->response;n=s->used;
    if(n<10 || p[0]<8 || p[0]>15 || p[0]==14 || p[1]!=(uint8_t)((c->subunit_type<<3)|c->subunit_id) ||
       p[2]!=0 || big_endian_read_24(p,3)!=c->company_id || p[7]>3 || big_endian_read_16(p,8)!=n-10)return true;
    const uint8_t response=p[0],kind=p[7],pdu=p[6];const uint16_t total=n-10;
    if(s->aborting){if(pdu!=0x41 || kind || total>1 || (response!=8&&response!=9&&response!=10))return true;
        s->pending=false;s->aborting=false;c->state=AVCTP_CONNECTION_OPENED;goto notify_queued;}
    if(s->continuing && pdu==0x40 && !kind && total<=1 && (response==8||response==10)){
        s->pending=false;s->continuing=false;c->state=AVCTP_CONNECTION_OPENED;picow_custom_emit(s,response,p+10,total);goto notify_queued;}
    if(pdu!=s->original_pdu)return true;
    c->controller_last_confirmed_transaction_id=s->label;
    if(kind){
        if(response!=12 || !total || (kind==1?s->continuing:!s->continuing))return true;
        if(kind==1){s->vendor_used=0;s->vendor_fragments=0;s->vendor_ctype=response;}
        if(response!=s->vendor_ctype)return true;
        if(total>sizeof(s->vendor)-s->vendor_used || ++s->vendor_fragments>c->controller_max_num_fragments){
            picow_custom_error(s,total>sizeof(s->vendor)-s->vendor_used?2:3);picow_custom_continue(c,s,true);return true;}
        memcpy(s->vendor+s->vendor_used,p+10,total);s->vendor_used+=total;
        if(kind!=3){picow_custom_continue(c,s,false);return true;}
        s->pending=false;s->continuing=false;c->state=AVCTP_CONNECTION_OPENED;
        picow_custom_emit(s,response,s->vendor,s->vendor_used);goto notify_queued;
    }
    if(s->continuing)return true;
    if(response!=15){s->pending=false;c->state=AVCTP_CONNECTION_OPENED;}
    picow_custom_emit(s,response,p+10,total);
notify_queued:
    if(!s->pending && c->controller_notifications_to_register)avrcp_request_can_send_now(c,c->l2cap_signaling_cid);
    return true;
}
uint8_t picow_avrcp_controller_custom(uint16_t cid,uint8_t command,uint8_t subunit,uint8_t subunit_id,
    uint8_t pdu,uint32_t company,const uint8_t *data,uint16_t n){
    avrcp_connection_t *c=avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER,cid);
    if(!c)return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    if(c->state!=AVCTP_CONNECTION_OPENED)return ERROR_CODE_COMMAND_DISALLOWED;
    if(command>4 || subunit>31 || subunit_id>7 || company>0xffffff || (!data&&n) ||
       n>PICOW_CUSTOM_REQUEST_MAX || c->l2cap_mtu<48)return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    picow_custom_t *s=picow_custom_slot(c);if(!s)return BTSTACK_MEMORY_ALLOC_FAILED;
    if(s->pending)return ERROR_CODE_COMMAND_DISALLOWED;
    s->used=s->remaining=s->vendor_used=s->vendor_fragments=0;s->continuing=s->aborting=false;
    s->pending=true;s->original_pdu=pdu;c->subunit_type=(avrcp_subunit_type_t)subunit;
    c->subunit_id=(avrcp_subunit_id_t)subunit_id;c->company_id=company;c->command_opcode=AVRCP_CMD_OPCODE_VENDOR_DEPENDENT;
    picow_custom_prepare(c,s,pdu,command,data,n);return ERROR_CODE_SUCCESS;
}
'''
def apply(source):
    def replace(a,b):
        nonlocal source
        if source.count(a)!=1:raise ValueError('custom patch anchor not unique: '+a[:80])
        source=source.replace(a,b,1)
    anchor='static void avrcp_handle_l2cap_data_packet_for_signaling_connection(avrcp_connection_t * connection, uint8_t *packet, uint16_t size){'
    replace(anchor,CODE+'\n'+anchor+'\n    if (picow_custom_receive(connection,packet,size)) return;\n')
    replace('static void avrcp_controller_handle_can_send_now(avrcp_connection_t * connection){','static void avrcp_controller_handle_can_send_now(avrcp_connection_t * connection){\n    if (picow_custom_send(connection)) return;')
    replace('static int avrcp_send_register_notification(', 'static void picow_custom_track_notification(avrcp_connection_t *, uint8_t, uint8_t);\nstatic int avrcp_send_register_notification(')
    replace('    return l2cap_send(connection->l2cap_signaling_cid, command, pos);', '    uint8_t result=l2cap_send(connection->l2cap_signaling_cid, command, pos);\n    if(!result)picow_custom_track_notification(connection,event_id,connection->transaction_id);\n    return result;')
    replace('static void avrcp_controller_handle_notification(',
            'static void picow_custom_retire_notification(avrcp_connection_t *, uint8_t);\nstatic void avrcp_controller_handle_notification(')
    replace('    uint16_t reset_event_mask = ~event_mask;\n',
            '    uint16_t reset_event_mask = ~event_mask;\n'
            '    if (ctype == AVRCP_CTYPE_RESPONSE_CHANGED_STABLE || ctype == AVRCP_CTYPE_RESPONSE_REJECTED)\n'
            '        picow_custom_retire_notification(connection, event_id);\n')
    replace('    if ((connection->notifications_enabled & (1 << event_id)) == 0){\n        return ERROR_CODE_SUCCESS;\n    }',
            '    if ((connection->notifications_enabled & (1 << event_id)) == 0){\n'
            '        /* No false cancellation success while registration awaits INTERIM. */\n'
            '        picow_custom_t *pending = picow_custom_find(connection);\n'
            '        if ((connection->controller_notifications_to_register & (1u << event_id)) ||\n'
            '            (pending && (pending->notifications & (1u << event_id))))\n'
            '            return ERROR_CODE_COMMAND_DISALLOWED;\n'
            '        return ERROR_CODE_SUCCESS;\n    }')
    return source
