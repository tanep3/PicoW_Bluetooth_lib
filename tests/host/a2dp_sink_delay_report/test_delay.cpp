#include <memory>
#include <array>
#define private public
#include "A2dpSinkReceiver.h"
#include "BluetoothRuntime.h"
#include "A2dpShared.h"
#include "btstack.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
static bool claim_allowed=true, has_connection=false;
static int init_result=0, radio_polls=0, connects=0, connectable=0;
static uint16_t disconnected=0;
static BluetoothRuntime::Link link{{{1,2,3,4,5,6}},1,0x40,true};
static unsigned reports=0; static uint16_t sent_delay=0; static uint8_t sdk_result=0;
static A2dpSinkReceiver *reenter=nullptr;
static HCI_STATE radio_state=HCI_STATE_WORKING;
static btstack_packet_handler_t handler=nullptr;
static avdtp_stream_endpoint_t endpoint{};
static avdtp_connection_t connection{};
BluetoothRuntime &BluetoothRuntime::instance(){ static BluetoothRuntime r;return r; }
bool BluetoothRuntime::claim(Profile){return claim_allowed;}
int BluetoothRuntime::initialize(){return init_result;}
int BluetoothRuntime::powerOn(){return 0;}
void BluetoothRuntime::poll(){++radio_polls;}
void BluetoothRuntime::setHandler(Profile,btstack_packet_handler_t p){handler=p;}
void BluetoothRuntime::setPairingPolicy(Profile,Address,bool){}
void BluetoothRuntime::setSecurityLevel(Profile,uint8_t){}
BluetoothRuntime::Link BluetoothRuntime::linkForPeer(const Address &peer)const{return peer==link.peer?link:Link{};}
extern "C" {
void btstack_assert_failed(const char*f,uint16_t n){fprintf(stderr,"assert %s:%u\n",f,n);abort();}
HCI_STATE hci_get_state(){return radio_state;}
void gap_connectable_control(uint8_t n){connectable=n;}
void a2dp_sink_init(){}
avdtp_stream_endpoint_t *avdtp_get_stream_endpoint_for_seid(uint16_t){return &endpoint;}
uint8_t avdtp_sink_delay_report(uint16_t cid,uint8_t seid,uint16_t delay){
  ++reports; assert(cid==7 && seid==1); sent_delay=delay;
  if(reenter){assert(reenter->reportDelay100us(1,1).status==A2dpSinkReceiver::DelayReportStatus::NotReady);assert(!reenter->listen());assert(!reenter->connect());reenter->disconnect();}
  if(connection.initiator_connection_state!=AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE)return ERROR_CODE_COMMAND_DISALLOWED;
  return sdk_result;
}
void avdtp_sink_register_packet_handler(btstack_packet_handler_t){}
void a2dp_config_process_avdtp_event_handler(avdtp_role_t,uint8_t*,uint16_t){}
void a2dp_config_process_ready_for_sep_discovery(avdtp_role_t,avdtp_connection_t*){}
avdtp_connection_t *avdtp_get_connection_for_avdtp_cid(uint16_t){return has_connection?&connection:nullptr;}
void a2dp_sink_register_packet_handler(btstack_packet_handler_t){}
void a2dp_sink_register_media_handler(void(*)(uint8_t,uint8_t*,uint16_t)){}
void a2dp_sink_register_media_config_validator(uint8_t(*)(const avdtp_stream_endpoint_t*,const uint8_t*,uint16_t)){}
avdtp_stream_endpoint_t *a2dp_sink_create_stream_endpoint(avdtp_media_type_t,avdtp_media_codec_type_t,const uint8_t*,uint16_t,uint8_t*,uint16_t){return &endpoint;}
uint8_t avdtp_local_seid(const avdtp_stream_endpoint_t*){return 1;}
void a2dp_sink_create_sdp_record(uint8_t*,uint32_t,uint16_t,const char*,const char*){}
uint32_t sdp_create_service_record_handle(){return 1;}
uint32_t de_get_len(const uint8_t*){return 100;}
uint8_t sdp_register_service(const uint8_t*){return 0;}
avdtp_connection_t *avdtp_get_connection_for_bd_addr(bd_addr_t){return has_connection?&connection:nullptr;}
uint8_t a2dp_sink_establish_stream(bd_addr_t,uint16_t*cid){++connects;*cid=7;return 0;}
void a2dp_sink_disconnect(uint16_t cid){disconnected=cid;}
uint8_t avdtp_disconnect(uint16_t cid){disconnected=cid;return 0;}
}

#include "A2dpSinkReceiver.cpp"
#undef private
#include "../../../examples/A2DPSinkReceiver/DelayReportExample.h"
struct DelayOutput : IAudioSink {
  A2dpSinkReceiver *owner=nullptr;
  size_t write(const int16_t*,size_t n,uint32_t,uint8_t)override{return n;}
  void reset() override { if(owner) assert(owner->reportDelay100us(1,1).status==A2dpSinkReceiver::DelayReportStatus::NotReady); }
};
int main(){
  using S=A2dpSinkReceiver::DelayReportStatus;
  DelayOutput output; A2dpSinkReceiver::Settings settings; settings.peer=link.peer;
  A2dpSinkReceiver r(output,settings);output.owner=&r;
  assert(r.reportDelay100us(0,1).status==S::NotReady);
  assert(r.begin()==A2dpSinkReceiver::BeginResult::Ok);
  auto &p=r.impl_->profile;
  auto setup=[&]{
    p.working=p.signaling=p.configured=p.allow_connections=true;
    p.connecting=p.closing=p.failed=false;p.cid=p.callback_cid_=7;p.seid=1;
    p.callback_handle_=link.handle;p.callback_acl_epoch_=link.epoch;
    p.callback_generation_=p.active_generation_=1;p.events.clear();
    has_connection=true;connection={};connection.avdtp_cid=7;connection.con_handle=link.handle;
    memcpy(connection.remote_addr,link.peer.data(),6);connection.state=AVDTP_SIGNALING_CONNECTION_OPENED;
    endpoint={};endpoint.connection=&connection;endpoint.sep.seid=1;endpoint.sep.in_use=1;
    endpoint.sep.type=AVDTP_SINK;endpoint.sep.media_type=AVDTP_AUDIO;endpoint.remote_sep.seid=2;
    endpoint.remote_sep.configured_service_categories=1u<<AVDTP_DELAY_REPORTING;
    endpoint.state=AVDTP_STREAM_ENDPOINT_OPENED;sdk_result=0;
  };
  auto reject=[&](S status,uint32_t value=123,uint32_t gen=1){auto n=reports;assert(r.reportDelay100us(value,gen).status==status);assert(reports==n);};
  setup();reenter=&r;
  for(auto value:{0u,1u,256u,65535u}){auto old=r.snapshot();auto result=r.reportDelay100us(value,1);assert(result.status==S::Accepted && result.sdk_status==0 && sent_delay==value);assert(r.snapshot().generation==old.generation && r.snapshot().cid==old.cid && r.snapshot().last_operation_status==old.last_operation_status);}
  assert(reportMeasuredSinkLatency(r,100001,1).status==S::Accepted && sent_delay==1001);
  assert(reportMeasuredSinkLatency(r,6553500,1).status==S::Accepted && sent_delay==65535);
  auto example_calls=reports;
  assert(reportMeasuredSinkLatency(r,6553501,1).status==S::InvalidDelay);
  assert(reportMeasuredSinkLatency(r,UINT32_MAX,1).status==S::InvalidDelay && reports==example_calls);
  reject(S::InvalidDelay,65536);reject(S::InvalidDelay,UINT32_MAX);
  reject(S::StaleSession,1,0);reject(S::StaleSession,1,2);
  for(bool *flag:{&p.working,&p.signaling,&p.configured,&p.allow_connections}){*flag=false;reject(S::NotReady);*flag=true;}
  for(bool *flag:{&p.failed,&p.closing,&p.connecting,&p.identity_exhausted}){*flag=true;reject(S::NotReady);*flag=false;}
  BluetoothRuntime::instance().failed_=true;reject(S::NotReady);BluetoothRuntime::instance().failed_=false;
  p.events.count=1;reject(S::NotReady);p.events.count=0;
  p.events.overflow=1;reject(S::NotReady);p.events.overflow=0;
  p.events.oversize=1;reject(S::NotReady);p.events.oversize=0;
  p.reject_cid_=8;reject(S::NotReady);p.reject_cid_=0;
  p.callback_generation_=2;reject(S::StaleSession);p.callback_generation_=1;
  p.callback_cid_=8;reject(S::StaleSession);p.callback_cid_=7;
  ++link.epoch;reject(S::StaleSession);--link.epoch;
  ++link.handle;reject(S::StaleSession);--link.handle;
  link.active=false;reject(S::StaleSession);link.active=true;
  connection.remote_addr[0]++;reject(S::NotReady);connection.remote_addr[0]--;
  connection.con_handle++;reject(S::NotReady);connection.con_handle--;
  connection.avdtp_cid++;reject(S::NotReady);connection.avdtp_cid--;
  has_connection=false;reject(S::NotReady);has_connection=true;
  endpoint.connection=nullptr;reject(S::NotReady);endpoint.connection=&connection;
  p.endpoint_=nullptr;reject(S::NotReady);p.endpoint_=&endpoint;
  endpoint.sep.type=AVDTP_SOURCE;reject(S::NotReady);endpoint.sep.type=AVDTP_SINK;
  endpoint.sep.seid=2;reject(S::NotReady);endpoint.sep.seid=1;
  endpoint.sep.in_use=0;reject(S::NotReady);endpoint.sep.in_use=1;
  for(auto seid:{0,63}){endpoint.remote_sep.seid=seid;reject(S::NotReady);}endpoint.remote_sep.seid=2;
  for(auto state:{AVDTP_STREAM_ENDPOINT_IDLE,AVDTP_STREAM_ENDPOINT_CONFIGURATION_SUBSTATEMACHINE,AVDTP_STREAM_ENDPOINT_W2_REQUEST_OPEN_STREAM,AVDTP_STREAM_ENDPOINT_W4_ACCEPT_OPEN_STREAM,AVDTP_STREAM_ENDPOINT_W4_L2CAP_FOR_MEDIA_CONNECTED,AVDTP_STREAM_ENDPOINT_CLOSING,AVDTP_STREAM_ENDPOINT_ABORTING,AVDTP_STREAM_ENDPOINT_W4_L2CAP_FOR_MEDIA_DISCONNECTED}){endpoint.state=state;reject(S::NotReady);}
  for(auto state:{AVDTP_STREAM_ENDPOINT_CONFIGURED,AVDTP_STREAM_ENDPOINT_OPENED,AVDTP_STREAM_ENDPOINT_STREAMING}){endpoint.state=state;assert(r.reportDelay100us(1,1).status==S::Accepted);}
  endpoint.initiator_config_state=AVDTP_INITIATOR_W4_ANSWER;reject(S::NotReady);endpoint.initiator_config_state=AVDTP_INITIATOR_STREAM_CONFIG_IDLE;
  endpoint.acceptor_config_state=AVDTP_ACCEPTOR_W2_ACCEPT_CLOSE_STREAM;reject(S::NotReady);endpoint.acceptor_config_state=AVDTP_ACCEPTOR_STREAM_CONFIG_IDLE;
  connection.wait_to_send_acceptor=true;reject(S::NotReady);connection.wait_to_send_acceptor=false;
  connection.state=AVDTP_SIGNALING_CONNECTION_IDLE;reject(S::NotReady);connection.state=AVDTP_SIGNALING_CONNECTION_OPENED;
  connection.acceptor_connection_state=AVDTP_SIGNALING_CONNECTION_ACCEPTOR_W2_ANSWER_DISCOVER_SEPS;reject(S::NotReady);connection.acceptor_connection_state=AVDTP_SIGNALING_CONNECTION_ACCEPTOR_IDLE;
  endpoint.remote_sep.registered_service_categories=1u<<AVDTP_DELAY_REPORTING;
  endpoint.remote_sep.configured_service_categories=0;reject(S::NotNegotiated);endpoint.remote_sep.configured_service_categories=1u<<AVDTP_DELAY_REPORTING;
  connection.initiator_connection_state=AVDTP_SIGNALING_CONNECTION_INITIATOR_W4_ANSWER;
  auto before=reports;auto result=r.reportDelay100us(12,1);assert(result.status==S::SdkRejected && result.sdk_status==ERROR_CODE_COMMAND_DISALLOWED && reports==before+1);
  connection.initiator_connection_state=AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE;sdk_result=0x42;
  result=r.reportDelay100us(12,1);assert(result.status==S::SdkRejected && result.sdk_status==0x42);
  before=reports;r.poll();r.poll();assert(reports==before);sdk_result=0;
  auto &other=picow::a2dp_shared::owners[0];other.registered=true;other.peer=link.peer;reject(S::NotReady);other={};
  // Reconnect with reused numeric CID/handle must reject old measurement generation.
  p.callback_generation_=p.active_generation_=2;++link.epoch;p.callback_acl_epoch_=link.epoch;
  reject(S::StaleSession,1,1);assert(r.reportDelay100us(1,2).status==S::Accepted);
  r.disconnect();reject(S::NotReady);assert(disconnected==7);
  puts("PASS Sink delay public facade boundaries, lifecycle, identity, generation, negotiation, busy/error, reentry, no retries");
}
