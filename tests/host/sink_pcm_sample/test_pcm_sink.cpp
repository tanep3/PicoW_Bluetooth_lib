#include "examples/A2dpSinkPcmConsumer/BoundedPcmSink.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <deque>
#include <limits>
using sink_pcm_sample::BoundedPcmSink;
unsigned cases=0;
void pass(const char* name){++cases;std::printf("PASS %s\n",name);}
uint32_t randomState=0x5a17c09e;
uint32_t rnd(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
template<size_t Capacity>void randomMatrix(){
 BoundedPcmSink<Capacity> sink;
 std::deque<int16_t> expected;
 uint32_t rate=0;uint8_t channels=0;
 uint64_t accepted=0,rejected=0,drained=0,flushed=0;
 for(unsigned step=0;step<10000;++step){
  const auto op=rnd()%10;
  if(op<6){
   const size_t n=rnd()%34;const uint8_t ch=1+rnd()%2;const uint32_t hz=(rnd()%2)?44100:48000;
   std::array<int16_t,66> input{};for(auto &sample:input)sample=static_cast<int16_t>(rnd());
   if(n&& (rate!=hz||channels!=ch)){
    if(channels)flushed+=expected.size()/channels;
    expected.clear();rate=hz;channels=ch;
   }
   const size_t take=n?std::min(n,Capacity-expected.size()/channels):0;
   for(size_t i=0;i<take*ch;++i)expected.push_back(input[i]);
   assert(sink.write(input.data(),n,hz,ch)==take);accepted+=take;rejected+=n-take;
   input.fill(0); // Borrowed caller data is immediately reusable.
  }else if(op<9){
   const size_t samples=rnd()%68;std::array<int16_t,70> output;output.fill(0x3456);
   const size_t frames=channels?std::min(expected.size()/channels,samples/channels):0;
   const auto result=sink.pull(output.data()+1,samples);
   assert(result.frames==frames);
   if(frames){assert(result.sample_rate==rate&&result.channels==channels);}
   for(size_t i=0;i<frames*channels;++i){assert(output[1+i]==expected.front());expected.pop_front();}
   assert(output[0]==0x3456);
   for(size_t i=1+frames*channels;i<output.size();++i)assert(output[i]==0x3456);
   drained+=frames;
  }else{
   if(channels)flushed+=expected.size()/channels;
   expected.clear();rate=0;channels=0;sink.reset();
  }
  assert(sink.queuedFrames()==(channels?expected.size()/channels:0));
  assert(sink.sampleRate()==rate&&sink.channels()==channels);
  const auto stats=sink.statistics();
  assert(stats.accepted_frames==accepted&&stats.rejected_frames==rejected);
  assert(stats.drained_frames==drained&&stats.flushed_frames==flushed);
 }
 pass("10,000-step deque oracle: owned PCM, FIFO wrap, format/reset and whole-frame drains");
}
int main(){
 {
  BoundedPcmSink<8> s;assert(s.queuedFrames()==0&&s.sampleRate()==0&&s.channels()==0);
  std::array<int16_t,8> in{{-32768,32767,-1,1,22,-22,33,-33}};
  assert(s.write(in.data(),4,44100,2)==4);in.fill(0);
  std::array<int16_t,10> out{};auto r=s.pull(out.data(),3);
  assert(r.frames==1&&r.sample_rate==44100&&r.channels==2&&out[0]==-32768&&out[1]==32767);
  assert(s.queuedFrames()==3);r=s.pull(out.data(),out.size());
  const std::array<int16_t,6> rest{{-1,1,22,-22,33,-33}};
  assert(r.frames==3);for(size_t i=0;i<rest.size();++i)assert(out[i]==rest[i]);
  pass("borrowed stereo input copied; signed extrema and odd destination capacity preserve frame units");
 }
 {
  BoundedPcmSink<3> s;const int16_t a[]{1,11,2,22,3,33,4,44,5,55};int16_t out[8]{};
  assert(s.write(a,5,48000,2)==3);assert(s.statistics().rejected_frames==2);
  assert(s.pull(out,2).frames==1&&out[0]==1&&out[1]==11);
  assert(s.write(a+6,2,48000,2)==1);assert(s.write(a,1,48000,2)==0);
  assert(s.pull(out,8).frames==3);const int16_t expected[]{2,22,3,33,4,44};
  for(int i=0;i<6;++i)assert(out[i]==expected[i]);
  assert(s.pull(out,8).frames==0);pass("partial prefix acceptance, full FIFO zero return, wrap and no suffix retry");
 }
 {
  BoundedPcmSink<8> s;const int16_t input[]{1,2,3,4,5,6,7,8};int16_t output[8]{};
  assert(s.write(input,4,44100,2)==4);const auto epoch=s.epoch();s.reset();
  assert(s.queuedFrames()==0&&s.channels()==0&&s.sampleRate()==0&&s.epoch()!=epoch);
  assert(s.statistics().flushed_frames==4&&s.statistics().reset_calls==1);
  assert(s.write(input,4,44100,2)==4);const auto e2=s.epoch();
  assert(s.write(input,3,22050,1)==3&&s.epoch()!=e2);
  const auto r=s.pull(output,8);assert(r.frames==3&&r.channels==1&&r.sample_rate==22050);
  assert(output[0]==1&&output[1]==2&&output[2]==3&&s.statistics().flushed_frames==8);
  pass("reset and format changes discard old-session audio before mono acceptance");
 }
 {
  BoundedPcmSink<4> s;const int16_t input[]{1,2,3,4};int16_t output[4]{};
  assert(s.write(input,2,48000,2)==2);const auto epoch=s.epoch();
  assert(s.write(nullptr,0,0,0)==0&&s.write(input,0,22050,1)==0);
  assert(s.write(nullptr,1,48000,2)==0&&s.write(input,1,0,2)==0);
  assert(s.write(input,1,48000,0)==0&&s.write(input,1,48000,3)==0);
  assert(s.write(nullptr,std::numeric_limits<size_t>::max(),48000,2)==0);
  assert(s.queuedFrames()==2&&s.epoch()==epoch&&s.sampleRate()==48000&&s.channels()==2);
  assert(s.pull(nullptr,20).frames==0&&s.pull(output,0).frames==0&&s.pull(output,1).frames==0);
  assert(s.pull(output,4).frames==2&&output[0]==1&&output[3]==4);
  pass("zero/invalid/null/huge counts preserve existing format and queue without writes");
 }
 randomMatrix<1>();randomMatrix<2>();randomMatrix<7>();randomMatrix<128>();
 std::printf("%u PCM consumer scenarios PASS, including 40,000 oracle steps; no SDK or radio\n",cases);
}
