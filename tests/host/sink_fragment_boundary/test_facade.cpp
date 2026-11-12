#include "A2dpSinkReceiver.h"
#include "BluetoothRuntime.h"
#include "A2dpShared.h"
#include "btstack.h"
#include "SbcDecoder.h"
#include <vector>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
static bool claim_allowed=true, has_connection=false;
static int init_result=0, radio_polls=0, connects=0, connectable=0;
static uint16_t disconnected=0;
static HCI_STATE radio_state=HCI_STATE_WORKING;
static btstack_packet_handler_t handler=nullptr;
static void(*media_handler)(uint8_t,uint8_t*,uint16_t)=nullptr;
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
BluetoothRuntime::Link BluetoothRuntime::linkForPeer(const Address&)const{return {{{1,2,3,4,5,6}},1,0x40,true};}
extern "C" {
void btstack_assert_failed(const char*f,uint16_t n){fprintf(stderr,"assert %s:%u\n",f,n);abort();}
HCI_STATE hci_get_state(){return radio_state;}
void gap_connectable_control(uint8_t n){connectable=n;}
void a2dp_sink_init(){}
avdtp_stream_endpoint_t *avdtp_get_stream_endpoint_for_seid(uint16_t){return &endpoint;}
uint8_t avdtp_sink_delay_report(uint16_t,uint8_t,uint16_t){return ERROR_CODE_COMMAND_DISALLOWED;}
void avdtp_sink_register_packet_handler(btstack_packet_handler_t){}
void a2dp_config_process_avdtp_event_handler(avdtp_role_t,uint8_t*,uint16_t){}
void a2dp_config_process_ready_for_sep_discovery(avdtp_role_t,avdtp_connection_t*){}
avdtp_connection_t *avdtp_get_connection_for_avdtp_cid(uint16_t){return has_connection?&connection:nullptr;}
void a2dp_sink_register_packet_handler(btstack_packet_handler_t){}
void a2dp_sink_register_media_handler(void(*p)(uint8_t,uint8_t*,uint16_t)){media_handler=p;}
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
struct FacadeOutput : IAudioSink {
  A2dpSinkReceiver *owner=nullptr;size_t resets=0;
  size_t write(const int16_t*,size_t n,uint32_t,uint8_t)override{return n;}
  void reset() override {++resets;if(owner){owner->disconnect();assert(!owner->connect());}}
};
int main(){
  FacadeOutput output;A2dpSinkReceiver::Settings cfg;cfg.peer={1,2,3,4,5,6};
  A2dpSinkReceiver receiver(output,cfg);assert(receiver.begin()==A2dpSinkReceiver::BeginResult::Ok);
  assert(receiver.listen());
  uint8_t opened[14]{HCI_EVENT_A2DP_META,12,A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED,1,0,6,5,4,3,2,1,0x40,0,0};
  uint8_t config[18]{HCI_EVENT_A2DP_META,16,A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION,1,0,1,2,0,AVDTP_AUDIO,0x80,0xbb,AVDTP_CHANNEL_MODE_STEREO,2,16,8,1,2,53};
  uint8_t started[6]{HCI_EVENT_A2DP_META,4,A2DP_SUBEVENT_STREAM_STARTED,1,0,1};
  handler(HCI_EVENT_PACKET,0,opened,14);handler(HCI_EVENT_PACKET,0,config,18);handler(HCI_EVENT_PACKET,0,started,6);receiver.poll();
  assert(receiver.snapshot().streaming && media_handler);
  btstack_sbc_encoder_bluedroid_t encoder{};auto *enc=btstack_sbc_encoder_bluedroid_init_instance(&encoder);
  assert(!enc->configure(&encoder,SBC_MODE_STANDARD,16,8,SBC_ALLOCATION_METHOD_LOUDNESS,48000,53,SBC_CHANNEL_MODE_STEREO));
  int16_t pcm[256]{};uint8_t encoded[128]{};enc->encode_signed_16(&encoder,pcm,encoded);
  unsigned length=enc->sbc_buffer_length(&encoder);assert(length==118);
  std::vector<uint8_t> first(73),last(71);first[0]=last[0]=0x80;first[1]=last[1]=96;first[12]=0xc2;last[12]=0xa1;last[3]=1;
  std::memcpy(first.data()+13,encoded,60);std::memcpy(last.data()+13,encoded+60,58);
  media_handler(1,first.data(),first.size());receiver.poll();auto d=receiver.diagnostics();
  assert(d.received_packets==1 && d.processed_packets==1 && d.decoded_frames==0 && d.pcm_frames==0);
  media_handler(1,last.data(),last.size());receiver.poll();d=receiver.diagnostics();
  assert(d.received_packets==2 && d.processed_packets==2 && d.decoded_frames==1 && d.pcm_frames==128 && d.delivered_frames==128 && !d.missing_packets && !d.timestamp_discontinuities);
  media_handler(1,last.data(),last.size());receiver.poll();d=receiver.diagnostics();
  assert(d.decoded_frames==1 && d.parse_errors[size_t(picow::sink::ParseError::Fragmented)]==1);
  receiver.disconnect();assert(!receiver.snapshot().streaming);
  puts("PASS public A2dpSinkReceiver diagnostics: fragments count as packets, completion as one frame/128 PCM frames; orphan tail rejected; explicit disconnect");
}
