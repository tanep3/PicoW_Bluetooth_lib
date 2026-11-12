#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "controller_overlay.c"
static avrcp_connection_t c;
static unsigned starts, completes, timer_active;
avrcp_connection_t *avrcp_get_connection_for_avrcp_cid_for_role(avrcp_role_t role,uint16_t id){return role==AVRCP_CONTROLLER && id==0x71 ? &c:NULL;}
void avrcp_request_can_send_now(avrcp_connection_t *p,uint16_t id){assert(p==&c && id==0x72);}
int btstack_run_loop_remove_timer(btstack_timer_source_t *p){(void)p;timer_active=0;return 1;}
void btstack_run_loop_set_timer(btstack_timer_source_t *p,uint32_t ms){(void)p;assert(ms==2000);}
void btstack_run_loop_set_timer_handler(btstack_timer_source_t *p,void (*fn)(btstack_timer_source_t *)){p->process=fn;}
void btstack_run_loop_set_timer_context(btstack_timer_source_t *p,void *ctx){p->context=ctx;}
void *btstack_run_loop_get_timer_context(btstack_timer_source_t *p){return p->context;}
void btstack_run_loop_add_timer(btstack_timer_source_t *p){(void)p;timer_active=1;}
static void event(uint8_t type,uint16_t channel,uint8_t *p,uint16_t n){
 (void)channel;assert(type==HCI_EVENT_PACKET && n==7);
 if(p[2]==AVRCP_SUBEVENT_OPERATION_START)++starts;
 else {assert(p[2]==AVRCP_SUBEVENT_OPERATION_COMPLETE);++completes;}
}
static void begin(void){memset(&c,0,sizeof(c));c.avrcp_cid=0x71;c.l2cap_signaling_cid=0x72;c.state=AVCTP_CONNECTION_OPENED;starts=completes=timer_active=0;avrcp_controller_context.avrcp_callback=event;}
static void receive(uint8_t *p,unsigned n){avrcp_handle_l2cap_data_packet_for_signaling_connection(&c,p,n);}
static void response(uint8_t *p,uint8_t type){const uint8_t b[]={0,0x11,0x0e,type,0x48,0x7c,0,0};memcpy(p,b,8);p[0]=(c.transaction_id<<4)|2;p[6]=c.operation_id;}
static void press(void){assert(!avrcp_controller_start_press_and_hold_cmd(0x71,AVRCP_OPERATION_ID_PLAY));assert(timer_active);c.state=AVCTP_W2_RECEIVE_PRESS_RESPONSE;}
static void ignored(uint8_t *p,unsigned n){avrcp_connection_t before=c;unsigned s=starts,f=completes;receive(p,n);assert(!memcmp(&before,&c,sizeof(c)) && starts==s && completes==f);}
int main(int argc,char **argv){
 (void)argv;uint8_t p[16],old[8];begin();press();response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);memcpy(old,p,8);receive(p,8);assert(starts==1 && c.state==AVCTP_W4_STOP);
 assert(!avrcp_controller_release_press_and_hold_cmd(0x71));assert(!timer_active);c.state=AVCTP_W2_RECEIVE_RESPONSE;
 if(argc>1){receive(old,8);assert(completes==1 && c.state==AVCTP_CONNECTION_OPENED);puts("REPRODUCED v5: duplicate old press incorrectly completes release");return 0;}
 ignored(old,8); // Original failing counterexample.
 response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);c.state=AVCTP_W2_SEND_RELEASE_COMMAND;ignored(p,8);c.state=AVCTP_W2_RECEIVE_RESPONSE;
 response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);p[6]&=0x7f;ignored(p,8); // Correct label, wrong state bit.
 response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);p[0]^=0x30;ignored(p,8); // Wrong label.
 response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);ignored(p,7); // Missing operand length.
 response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);p[8]=0;ignored(p,9); // Trailing byte.
 response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);p[7]=1;ignored(p,8); // Inconsistent length.
 response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);p[6]^=2;ignored(p,8); // Different operation.
 response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);p[3]=AVRCP_CTYPE_CONTROL;ignored(p,8);
 response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);p[1]=0;ignored(p,8);
 response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);p[4]=0;ignored(p,8);
 response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);p[5]=AVRCP_CMD_OPCODE_UNIT_INFO;ignored(p,8);
 response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);receive(p,8);assert(completes==1 && c.state==AVCTP_CONNECTION_OPENED);ignored(p,8);
 press();ignored(old,8);response(p,AVRCP_CTYPE_RESPONSE_REJECTED);receive(p,8);assert(starts==2 && c.state==AVCTP_W4_STOP);
 assert(!avrcp_controller_release_press_and_hold_cmd(0x71));c.state=AVCTP_W2_RECEIVE_RESPONSE;response(p,AVRCP_CTYPE_RESPONSE_NOT_IMPLEMENTED);receive(p,8);assert(completes==2 && c.state==AVCTP_CONNECTION_OPENED && !timer_active);
 // One-shot API still schedules its automatic release and completes it.
 assert(!avrcp_controller_pause(0x71));c.state=AVCTP_W2_RECEIVE_PRESS_RESPONSE;response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);receive(p,8);assert(c.state==AVCTP_W2_SEND_RELEASE_COMMAND);c.state=AVCTP_W2_RECEIVE_RESPONSE;response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);receive(p,8);assert(completes==3);
 // Vendor-unique group operands remain valid, not restricted to empty bodies.
 assert(!avrcp_controller_next_group(0x71));c.state=AVCTP_W2_RECEIVE_PRESS_RESPONSE;response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);p[7]=5;memcpy(p+8,c.data,5);receive(p,13);assert(c.state==AVCTP_W2_SEND_RELEASE_COMMAND);c.state=AVCTP_W2_RECEIVE_RESPONSE;response(p,AVRCP_CTYPE_RESPONSE_ACCEPTED);p[7]=5;memcpy(p+8,c.data,5);receive(p,13);assert(completes==4);
 puts("PASS fixed actual-generated-SDK pass-through correlation, duplicate/stale rejection, valid held/one-shot/group/rejected responses");
}
