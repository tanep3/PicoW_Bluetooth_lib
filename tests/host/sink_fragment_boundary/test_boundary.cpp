#include "BluetoothRuntime.h"
#include "SbcDecoder.h"
#include "btstack.h"
// Exercise private packet entrypoints without a radio. SDK event definitions,
// accessors and the real SBC encoder/decoder remain unchanged.
#define private public
#include "SinkProfile.h"
#undef private
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
static unsigned off_calls=0, disconnect_calls=0;
static BluetoothRuntime::Link test_link{{{0x2c,0xcf,0x67,0x11,0x72,0xf5}},1,0x40,true};
BluetoothRuntime::Link BluetoothRuntime::linkForPeer(const Address &peer) const { return peer == test_link.peer ? test_link : Link{}; }
BluetoothRuntime &BluetoothRuntime::instance(){static BluetoothRuntime r;return r;}
int BluetoothRuntime::powerOff(){++off_calls;return 0;}
extern "C" void a2dp_sink_disconnect(uint16_t){++disconnect_calls;}
extern "C" uint8_t gap_disconnect(hci_con_handle_t){++disconnect_calls;return 0;}
extern "C" void btstack_assert_failed(const char *file,uint16_t line){fprintf(stderr,"assert %s:%u\n",file,line);abort();}
struct Consumer : IAudioSink {
  size_t writes=0, resets=0;
  std::vector<int16_t> last_pcm;
  size_t write(const int16_t *p,size_t frames,uint32_t rate,uint8_t channels) override {
    assert(p && frames==128 && (rate==48000 || rate==44100) && channels==2); last_pcm.assign(p,p+frames*channels); ++writes; return frames;
  }
  void reset() override { ++resets; }
};
int main(){
  A2dpSinkProfile s({{0x2c,0xcf,0x67,0x11,0x72,0xf5}});A2dpSinkProfile::active_=&s;s.seid=1;Consumer consumer;s.decoder.output=&consumer;
  s.allow_connections=true;
  // Reject a foreign A2DP signaling peer without disconnecting its shared ACL.
  uint8_t foreign_open[14]{HCI_EVENT_A2DP_META,12,A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED,9,0,6,5,4,3,2,1,0x41,0,0};
  s.eventCallback(HCI_EVENT_PACKET,0,foreign_open,sizeof(foreign_open));
  assert(disconnect_calls==0);s.poll();assert(disconnect_calls==1 && !s.signaling && off_calls==0);
  uint8_t foreign_acl[13]{HCI_EVENT_CONNECTION_COMPLETE,11,0,0x41,0,6,5,4,3,2,1,1,0};
  s.eventCallback(HCI_EVENT_PACKET,0,foreign_acl,sizeof(foreign_acl));s.poll();
  assert(disconnect_calls==1); // A different profile may own that peer.
  uint8_t opened[14]{HCI_EVENT_A2DP_META,12,A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED,1,0,0xf5,0x72,0x11,0x67,0xcf,0x2c,0x40,0,0};
  s.eventCallback(HCI_EVENT_PACKET,0,opened,sizeof(opened));assert(!s.signaling);s.poll();assert(s.signaling && s.cid==1);
  uint8_t config[18]{HCI_EVENT_AVDTP_META,16,A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION,1,0,1,2,0,AVDTP_AUDIO,0x80,0xbb,AVDTP_CHANNEL_MODE_STEREO,2,16,8,1,2,53};
  for(unsigned n=0;n<18;++n)assert(s.validate(nullptr,config,n)!=0);
  assert(!s.validate(nullptr,config,sizeof(config))); // SDK callback retains AVDTP outer event code.
  config[13]=5;assert(s.validate(nullptr,config,sizeof(config))!=0);config[13]=16;
  config[0]=HCI_EVENT_A2DP_META;assert(!s.validate(nullptr,config,sizeof(config)));
  s.eventCallback(HCI_EVENT_PACKET,0,config,sizeof(config));assert(!s.configured);s.poll();assert(s.configured && s.config.rate==48000);
  uint8_t started[6]{HCI_EVENT_A2DP_META,4,A2DP_SUBEVENT_STREAM_STARTED,1,0,1};
  s.eventCallback(HCI_EVENT_PACKET,0,started,6);s.poll();assert(s.streaming && s.starts==1);
  btstack_sbc_encoder_bluedroid_t encoder{};auto *enc=btstack_sbc_encoder_bluedroid_init_instance(&encoder);
  assert(!enc->configure(&encoder,SBC_MODE_STANDARD,16,8,SBC_ALLOCATION_METHOD_LOUDNESS,48000,53,SBC_CHANNEL_MODE_STEREO));
  int16_t pcm[256]{};uint8_t encoded[256]{};enc->encode_signed_16(&encoder,pcm,encoded);unsigned len=enc->sbc_buffer_length(&encoder);
  std::vector<uint8_t> packet(13+len,0);packet[0]=0x80;packet[1]=96;packet[12]=1;std::memcpy(packet.data()+13,encoded,len);
  auto restart=[&] {
    started[2]=A2DP_SUBEVENT_STREAM_SUSPENDED;s.eventCallback(HCI_EVENT_PACKET,0,started,6);s.poll();
    started[2]=A2DP_SUBEVENT_STREAM_STARTED;s.eventCallback(HCI_EVENT_PACKET,0,started,6);s.poll();
  };
  auto fragment=[&](unsigned offset,unsigned amount,uint8_t flags,uint16_t seq,uint32_t ts=0) {
    std::vector<uint8_t> p(13+amount,0);p[0]=0x80;p[1]=96;p[2]=seq>>8;p[3]=seq;
    p[4]=ts>>24;p[5]=ts>>16;p[6]=ts>>8;p[7]=ts;p[12]=flags;
    std::memcpy(p.data()+13,encoded+offset,amount);return p;
  };
  auto feed=[&](std::vector<uint8_t> p) {
    s.mediaCallback(1,p.data(),p.size());std::fill(p.begin(),p.end(),0);s.poll();
  };
  auto good=[&] { return s.decoder.good; };
  // Positive two-fragment receive, deferred copies and exactly one PCM output.
  feed(fragment(0,60,0xc2,0));assert(good()==0 && s.decoder.attempts==0);
  feed(fragment(60,len-60,0xa1,1));assert(good()==1 && consumer.writes==1);
  assert(s.sequence.expected_timestamp==128 && s.sequence.missing==0);
  // 15 fragments, fixed nonfinal size and reserved bit ignored.
  restart();const auto before=good();unsigned offset=0;
  for(unsigned i=0;i<15;++i){unsigned amount=i==14?len-offset:8;
    feed(fragment(offset,amount,uint8_t(0x80|0x10|(i==0?0x40:0)|(i==14?0x20:0)|(15-i)),i));
    offset+=amount;if(i<14)assert(good()==before);
  }assert(good()==before+1);
  // Whole-frame and fragment PCM must match after equivalent decoder resets,
  // across both rates, channel modes and negotiated bitpool endpoints.
  for(unsigned rate:{44100u,48000u}) for(unsigned mode:{2u,3u}) for(unsigned pool:{2u,53u}) {
    config[9]=rate;config[10]=rate>>8;
    config[11]=mode==2?AVDTP_CHANNEL_MODE_STEREO:AVDTP_CHANNEL_MODE_JOINT_STEREO;
    s.eventCallback(HCI_EVENT_PACKET,0,config,sizeof(config));s.poll();restart();
    assert(!enc->configure(&encoder,SBC_MODE_STANDARD,16,8,SBC_ALLOCATION_METHOD_LOUDNESS,rate,pool,
      mode==2?SBC_CHANNEL_MODE_STEREO:SBC_CHANNEL_MODE_JOINT_STEREO));
    for(unsigned i=0;i<256;++i)pcm[i]=int16_t((i*251)%16000-8000);
    enc->encode_signed_16(&encoder,pcm,encoded);len=enc->sbc_buffer_length(&encoder);
    auto whole=fragment(0,len,1,0);auto total=good();feed(whole);assert(good()==total+1);
    const auto expected=consumer.last_pcm;
    for(unsigned chunk:pool==2?std::vector<unsigned>{2,3}:std::vector<unsigned>{60}) {
      restart();total=good();unsigned count=(len+chunk-1)/chunk;assert(count<=15);
      for(unsigned i=0;i<count;++i){unsigned amount=std::min(chunk,len-i*chunk);
        feed(fragment(i*chunk,amount,uint8_t(0x80|(i==0?0x40:0)|(i+1==count?0x20:0)|(count-i)),i));
        if(i+1<count)assert(good()==total);
      }if(good()!=total+1 || consumer.last_pcm!=expected)fprintf(stderr,"equivalence rate=%u mode=%u pool=%u chunk=%u good=%llu expected=%llu pcm=%d\n",rate,mode,pool,chunk,(unsigned long long)good(),(unsigned long long)(total+1),consumer.last_pcm==expected);assert(good()==total+1 && consumer.last_pcm==expected);
    }
  }
  // A one-byte nonfinal fragment cannot form even the minimum16-byte SBC
  // frame within the wire count limit15. Reject the incomplete final frame.
  restart();auto short_total=good();for(unsigned i=0;i<15;++i)
    feed(fragment(i,1,uint8_t(0x80|(i==0?0x40:0)|(i==14?0x20:0)|(15-i)),i));
  assert(good()==short_total);
  config[9]=0x80;config[10]=0xbb;config[11]=AVDTP_CHANNEL_MODE_STEREO;
  s.eventCallback(HCI_EVENT_PACKET,0,config,sizeof(config));s.poll();restart();
  assert(!enc->configure(&encoder,SBC_MODE_STANDARD,16,8,SBC_ALLOCATION_METHOD_LOUDNESS,48000,53,SBC_CHANNEL_MODE_STEREO));
  enc->encode_signed_16(&encoder,pcm,encoded);len=enc->sbc_buffer_length(&encoder);
  packet=fragment(0,len,1,0);
  // Sequence and timestamp wrap: both fragments share one timestamp.
  restart();feed(fragment(0,60,0xc2,65535,0xffffff80));feed(fragment(60,len-60,0xa1,0,0xffffff80));
  assert(s.sequence.sequence==0 && s.sequence.expected_timestamp==0);
  auto negative=[&](unsigned kind) {
    restart();const auto n=good();feed(fragment(0,60,0xc2,10));
    auto end=fragment(60,len-60,0xa1,11);
    switch(kind){
      case 0:end[3]=12;break; // loss
      case 1:end[3]=10;break; // duplicate
      case 2:end[3]=9;break; // reorder
      case 3:end[7]=1;break; // timestamp
      case 4:end[11]=1;break; // SSRC
      case 5:end[1]=97;break; // PT change
      case 6:end[12]=0xa2;break; // countdown/last mismatch
      case 7:end[12]=0xe1;break; // start+last
      case 8:end[12]=0xa0;break; // zero remaining
      case 9:end[1]=0x80|96;break; // M
      case 10:end[1]=95;break; // static PT
      case 11:end[0]=0x40;break; // RTP version
      case 12:end[0]|=0x10;break; // X
      case 13:end[0]|=0x20;break; // padding policy
      case 14:end.resize(13);break; // empty
      case 15:end.pop_back();break; // short SBC
      case 16:end.push_back(0);break; // trailing SBC
    }
    feed(end);assert(good()==n);
    // Failed group cannot retain a prefix for a later valid-looking tail.
    feed(fragment(60,len-60,0xa1,11));assert(good()==n);
    // A complete frame can recover without restarting the stream.
    auto full=packet;full[3]=20;full[7]=128;feed(full);assert(good()==n+1);
  };
  for(unsigned kind=0;kind<17;++kind)negative(kind);
  restart();auto n=good();feed(fragment(0,40,0xc3,0));feed(fragment(40,39,0x82,1));
  feed(fragment(79,len-79,0xa1,2));assert(good()==n); // unequal nonfinal
  restart();n=good();feed(fragment(0,60,0xc3,0));feed(fragment(0,60,0x82,1));
  feed(fragment(60,len-60,0xa1,2));assert(good()==n); // aggregate overflow
  restart();n=good();auto bad=fragment(0,60,0xc2,0);bad[16]^=1;feed(bad);
  const auto failures=s.decoder.failed;feed(fragment(60,len-60,0xa1,1));
  assert(good()==n && s.decoder.failed==failures+1); // strict CRC failure
  // Partial -> fresh complete frame, partial -> replacement START.
  restart();n=good();feed(fragment(0,60,0xc2,0));auto full=packet;full[3]=1;feed(full);assert(good()==n+1);
  restart();n=good();feed(fragment(0,60,0xc2,0));feed(fragment(0,60,0xc2,1));
  feed(fragment(60,len-60,0xa1,2));assert(good()==n+1);
  // CSRC lists are permitted, but must be identical within this group.
  auto csrc=[](std::vector<uint8_t> p,unsigned count){p[0]|=count;p.insert(p.begin()+12,4*count,0x34);return p;};
  for(unsigned count:{1u,15u}) {
    restart();n=good();feed(csrc(fragment(0,60,0xc2,0),count));
    feed(csrc(fragment(60,len-60,0xa1,1),count));assert(good()==n+1);
    restart();n=good();feed(csrc(fragment(0,60,0xc2,0),count));
    auto end=csrc(fragment(60,len-60,0xa1,1),count);end[12]^=1;feed(end);assert(good()==n);
    restart();n=good();feed(csrc(fragment(0,60,0xc2,0),count));
    end=csrc(fragment(60,len-60,0xa1,1),count);end.resize(12+4*count);feed(end);assert(good()==n);
  }
  restart();n=good();feed(fragment(0,50,0xc2,0));feed(fragment(50,len-50,0xa1,1));assert(good()==n); // final larger than first
  for(unsigned flag:{0x21u,0x42u,0x61u}){restart();n=good();feed(fragment(0,60,flag,0));assert(good()==n);}
  // Control and media in a single callback burst preserve exact ordering.
  restart();n=good();auto first=fragment(0,60,0xc2,0),end=fragment(60,len-60,0xa1,1);
  s.mediaCallback(1,first.data(),first.size());
  started[2]=A2DP_SUBEVENT_STREAM_SUSPENDED;s.eventCallback(HCI_EVENT_PACKET,0,started,6);
  started[2]=A2DP_SUBEVENT_STREAM_STARTED;s.eventCallback(HCI_EVENT_PACKET,0,started,6);
  s.mediaCallback(1,end.data(),end.size());while(s.media.count || s.events.count){s.poll();}assert(good()==n);
  // Stale copied packet/wrong endpoint cannot finish a current fragment group.
  restart();n=good();feed(first);s.media.push(end.data(),end.size(),1,s.active_generation_-1,++s.callback_order_,s.callback_media_loss_);s.poll();
  feed(end);assert(good()==n);
  restart();n=good();feed(first);s.mediaCallback(2,end.data(),end.size());s.poll();
  // Wrong-SEID callback is not part of this stream and must not decode.
  assert(good()==n);restart();
  // Every lifecycle boundary retires pending data.
  for(unsigned boundary=0;boundary<4;++boundary){restart();n=good();feed(fragment(0,60,0xc2,0));
    if(boundary==0)restart();
    if(boundary==1){s.eventCallback(HCI_EVENT_PACKET,0,config,sizeof(config));s.poll();}
    if(boundary==2){started[2]=A2DP_SUBEVENT_STREAM_STOPPED;s.eventCallback(HCI_EVENT_PACKET,0,started,6);s.poll();started[2]=A2DP_SUBEVENT_STREAM_STARTED;s.eventCallback(HCI_EVENT_PACKET,0,started,6);s.poll();}
    if(boundary==3){started[2]=A2DP_SUBEVENT_STREAM_RELEASED;s.eventCallback(HCI_EVENT_PACKET,0,started,6);s.poll();started[2]=A2DP_SUBEVENT_STREAM_STARTED;s.eventCallback(HCI_EVENT_PACKET,0,started,6);s.poll();}
    feed(fragment(60,len-60,0xa1,1));assert(good()==n);
  }
  restart();n=good();feed(fragment(0,60,0xc2,0));
  std::vector<uint8_t> oversized(1025,0);s.mediaCallback(1,oversized.data(),oversized.size());
  feed(fragment(60,len-60,0xa1,1));assert(good()==n && s.media.oversize==1);
  // Pre-loss queued START cannot rebuild across a later rejected callback.
  restart();n=good();for(unsigned i=0;i<8;++i){auto start=fragment(0,60,0xc2,i);s.mediaCallback(1,start.data(),start.size());}
  auto lost=fragment(60,len-60,0xa1,8);s.mediaCallback(1,lost.data(),lost.size());assert(s.media.overflow==1);
  while(s.media.count){s.poll();}feed(lost);assert(good()==n);
  restart();feed(fragment(0,60,0xc2,0));uint8_t closed[]{HCI_EVENT_A2DP_META,3,A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED,1,0};
  s.disconnect();assert(!s.streaming);
  feed(fragment(60,len-60,0xa1,1));assert(good()==n);
  s.eventCallback(HCI_EVENT_PACKET,0,closed,5);s.poll();assert(!s.configured && !s.sequence.valid);
  feed(fragment(60,len-60,0xa1,1));assert(good()==n);
  // HCI-only loss and HALTING also retire a live fragment prefix.
  for(unsigned halt=0;halt<2;++halt){
    ++test_link.epoch;test_link.active=true;s.allow_connections=true;
    s.eventCallback(HCI_EVENT_PACKET,0,opened,sizeof(opened));s.eventCallback(HCI_EVENT_PACKET,0,config,sizeof(config));
    started[2]=A2DP_SUBEVENT_STREAM_STARTED;s.eventCallback(HCI_EVENT_PACKET,0,started,6);s.poll();
    assert(s.streaming);feed(fragment(0,60,0xc2,0));
    if(halt){uint8_t event[]{BTSTACK_EVENT_STATE,1,HCI_STATE_HALTING};s.eventCallback(HCI_EVENT_PACKET,0,event,3);}
    else {uint8_t event[]{HCI_EVENT_DISCONNECTION_COMPLETE,4,0,0x40,0,0x13};s.eventCallback(HCI_EVENT_PACKET,0,event,6);test_link.active=false;}
    s.poll();assert(!s.streaming);feed(fragment(60,len-60,0xa1,1));assert(good()==n);
  }
  puts("PASS production Sink fragmented SBC: 2/15 fragments, owned copies, wrap, malformed/loss/CRC recovery, lifecycle and ordered queue loss barriers");
}
