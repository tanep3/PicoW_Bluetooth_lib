#include "examples/AvrcpMainLoopSample/CoverArtFlow.h"
#include "examples/AvrcpMainLoopSample/OwnedRecordQueue.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace avrcp_sample;
// SYNTHETIC host fixtures only. No SDK, OBEX, USB, radio or real metadata.
const SessionBinding signal{{2,0,0,0,0,1}, 7, 8, 9};
const SessionBinding browse{signal.peer, 7, 18, 19};
const ArtBinding cover{signal.peer, 7, 28, 29, 9};
ArtCommand command(ArtOperation op = ArtOperation::Image) {
  ArtCommand c; c.operation = op; c.handle = {{'1','2','3','4','5','6','7',0}}; return c;
}
using Flow = CoverArtFlow<16 * 1024>;
struct IO {
  Flow &flow;
  unsigned connects=0, gets=0, cancels=0;
  int connect_result=0, get_result=0, cancel_result=0;
  ArtOperation selected{};
  std::array<char,8> copied_handle{};
  int connect(const std::array<uint8_t,6> &p) {
    assert(p==signal.peer); assert(flow.state()==FlowState::Connecting); ++connects; return connect_result;
  }
  int get(const ArtBinding &b, ArtOperation op, const char *h) {
    assert(b==cover); assert(flow.state()==FlowState::Receiving);
    assert(flow.object().state()==ArtState::Armed); // Armed BEFORE API invocation.
    ++gets; selected=op; std::memcpy(copied_handle.data(),h,8); return get_result;
  }
  int cancel(const std::array<uint8_t,6> &p) { assert(p==signal.peer); ++cancels; return cancel_result; }
};
struct Rig {
  Flow flow; IO io{flow}; ArtLive live{signal,browse,{}};
  void ready() {
    assert(flow.signalConnected(signal,live)); assert(flow.takeBrowseAttempt(live)); flow.browseAdmission(0);
    assert(flow.browseConnected(browse,live)); assert(flow.state()==FlowState::Ready);
  }
  void connect(ArtOperation op=ArtOperation::Image, uint32_t now=10) {
    ready(); auto c=command(op); assert(flow.submit(c,live)==InputResult::Accepted);
    c.handle[0]='9'; // Prove owned intent.
    flow.tick(now,live,io); assert(io.connects==1 && io.gets==0);
  }
  void receiving(ArtOperation op=ArtOperation::Image, uint32_t now=10) {
    connect(op,now); live.cover=cover; flow.connected(cover,0,live);
    assert(io.gets==0); flow.tick(now+1,live,io); assert(io.gets==1);
    assert(io.selected==op && io.copied_handle[0]=='1');
  }
  void dead() {
    assert(flow.state()==FlowState::Failed || flow.state()==FlowState::Stopped);
    assert(!flow.object().data() && !flow.object().size() && !flow.object().received());
    for(unsigned i=0;i<5;++i) flow.tick(50000+i,live,io);
    assert(io.cancels<=1); assert(io.connects<=1 && io.gets<=1);
  }
};
void parser() {
  ArtCommandLine p; ArtCommand c;
  auto feed=[&](const std::string &s) {
    ArtCommandLine::Result r=ArtCommandLine::Result::None;
    for(char ch:s) r=p.feed(ch,c);
    return r;
  };
  assert(feed("art thumbnail 1234567\r\n")==ArtCommandLine::Result::Request);
  assert(c.operation==ArtOperation::Thumbnail && c.valid());
  assert(feed("art properties 1234567\n")==ArtCommandLine::Result::Request);
  assert(c.operation==ArtOperation::Properties);
  assert(feed("art image 1234567\n")==ArtCommandLine::Result::Request);
  assert(c.operation==ArtOperation::Image);
  for(const char *s : {"\n","art image 123456\n","art image 12345678\n",
      "art image 1234a67\n","art Image 1234567\n","art image 1234567 \n",
      " art image 1234567\n","art image \t1234567\n",
      "art image 12\r34567\n","art image 1234567\r\r\n",
      "\rart image 1234567\n"})
    assert(feed(s)==ArtCommandLine::Result::Invalid);
  assert(feed(std::string(33,'a')+"\n")==ArtCommandLine::Result::Invalid);
  assert(feed(std::string(32,'a')+"\n")==ArtCommandLine::Result::Invalid);
  assert(feed(std::string("art image 12\0",13)+"34567\n")==ArtCommandLine::Result::Invalid);
  assert(feed("cancel\n")==ArtCommandLine::Result::Cancel);
  for(char ch:std::string("art image 123")) assert(p.feed(ch,c,false)==ArtCommandLine::Result::None);
  assert(feed("4567\n")==ArtCommandLine::Result::NotReady);
  for(char ch:std::string("art image 1234567")) assert(p.feed(ch,c)==ArtCommandLine::Result::None);
  assert(p.feed('\n',c,false)==ArtCommandLine::Result::NotReady);
  assert(feed("art image 123")==ArtCommandLine::Result::None);
  assert(feed("4567\n")==ArtCommandLine::Result::Request);
}
void success() {
  std::array<uint8_t,512> bytes{}; bytes.fill(42);
  for(auto op : {ArtOperation::Thumbnail, ArtOperation::Image, ArtOperation::Properties}) {
    Rig r; r.receiving(op);
    for(uint32_t offset=0;offset<16384;offset+=512) {
      assert(r.flow.data(cover,op,123,offset,bytes.data(),bytes.size()));
      assert(!r.flow.object().data() && !r.flow.object().size());
    }
    bytes[0]=9;
    assert(r.flow.complete(cover,op,123,0,16384));
    assert(r.flow.object().size()==16384 && r.flow.object().data()[0]==42);
    assert(r.flow.submit(command(),r.live)==InputResult::Duplicate);
    r.flow.tick(60000,r.live,r.io); assert(r.io.gets==1 && r.io.cancels==0);
    r.flow.stop(); r.dead(); assert(r.io.cancels==1); bytes[0]=42;
  }
}
void admission() {
  Rig r; auto c=command(); assert(r.flow.submit(c,r.live)==InputResult::NotReady);
  assert(r.flow.signalConnected(signal,r.live));
  assert(r.flow.submit(c,r.live)==InputResult::NotReady); // Browsing SDP still pending.
  assert(r.flow.takeBrowseAttempt(r.live));
  assert(r.flow.browseConnected(browse,r.live));
  r.live.queue_empty=false; assert(r.flow.submit(c,r.live)==InputResult::NotReady);
  r.live.queue_empty=true;
  for(size_t i=0;i<8;++i) { auto bad=c; bad.handle[i]='x'; assert(r.flow.submit(bad,r.live)==InputResult::Invalid); }
  auto bad=c; bad.operation=static_cast<ArtOperation>(255);
  assert(r.flow.submit(bad,r.live)==InputResult::Invalid);
  assert(r.flow.submit(c,r.live)==InputResult::Accepted);
  assert(r.flow.submit(c,r.live)==InputResult::Duplicate);
  r.live.queue_empty=false; r.flow.tick(0,r.live,r.io); assert(r.io.connects==0);
  r.live.queue_empty=true; r.flow.tick(1,r.live,r.io); assert(r.io.connects==1);
  r.live.cover=cover; r.flow.tick(2,r.live,r.io); assert(r.io.gets==0); // Live binding alone insufficient.
  r.flow.connected(cover,0,r.live); r.live.queue_empty=false;
  r.flow.tick(3,r.live,r.io); assert(r.io.gets==0);
  r.live.queue_empty=true; r.flow.tick(4,r.live,r.io); assert(r.io.gets==1);
  for(unsigned variant=0;variant<4;++variant) {
    Rig x;
    if(variant==0) { x.ready(); x.io.connect_result=1; assert(x.flow.submit(c,x.live)==InputResult::Accepted); x.flow.tick(1,x.live,x.io); }
    if(variant==1) { x.connect(); x.io.get_result=1; x.live.cover=cover; x.flow.connected(cover,0,x.live); x.flow.tick(11,x.live,x.io); }
    if(variant==2) { assert(x.flow.signalConnected(signal,x.live)); x.flow.browseAdmission(1); }
    if(variant==3) { x.ready(); x.live.cover=cover; assert(x.flow.submit(c,x.live)==InputResult::NotReady); }
    x.dead();
  }
}
void deferred_browse() {
  // Model query callbacks before the next application service step.
  for(unsigned failure=0;failure<3;++failure) {
    Rig r; assert(r.flow.signalConnected(signal,r.live));
    r.live.queue_empty=false;
    assert(!r.flow.takeBrowseAttempt(r.live));
    if(failure==0) { r.live.queue_empty=true; assert(r.flow.takeBrowseAttempt(r.live));
      assert(!r.flow.takeBrowseAttempt(r.live)); }
    if(failure==1) { r.live.queue_lost=true; assert(!r.flow.takeBrowseAttempt(r.live)); r.dead(); }
    if(failure==2) { ++r.live.signal.generation; r.live.queue_empty=true;
      assert(!r.flow.takeBrowseAttempt(r.live)); r.dead(); }
  }
}
void sessions() {
  uint8_t byte=1;
  // Each full signalling identity field must stay unchanged even before input.
  for(unsigned field=0;field<9;++field) {
    Rig r; r.ready();
    if(field<6) ++r.live.signal.peer[field];
    else if(field==6) ++r.live.signal.epoch;
    else if(field==7) ++r.live.signal.generation;
    else ++r.live.signal.cid;
    r.flow.tick(1,r.live,r.io); r.dead();
    // Never rebind old/partial input to a reconnected signalling generation.
    assert(!r.flow.signalConnected(r.live.signal,r.live));
    assert(r.flow.submit(command(),r.live)==InputResult::Duplicate);
  }
  for(unsigned field=0;field<10;++field) {
    ArtBinding bad=cover;
    if(field<6) ++bad.peer[field]; else if(field==6) ++bad.epoch;
    else if(field==7) ++bad.generation; else if(field==8) ++bad.cid; else ++bad.avrcp_cid;
    { Rig r; r.connect(); r.live.cover=cover; r.flow.connected(bad,0,r.live); r.dead(); assert(r.io.gets==0); }
    { Rig r; r.receiving(); assert(!r.flow.data(bad,ArtOperation::Image,1,0,&byte,1)); r.dead(); }
    { Rig r; r.receiving(); assert(r.flow.data(cover,ArtOperation::Image,1,0,&byte,1));
      assert(!r.flow.complete(bad,ArtOperation::Image,1,0,1)); r.dead(); }
    { Rig r; r.receiving(); r.live.cover=bad; r.flow.tick(12,r.live,r.io); r.dead(); }
  }
  { Rig r; auto bad=signal; ++bad.generation; assert(!r.flow.signalConnected(bad,r.live)); r.dead(); }
  { Rig r; assert(r.flow.signalConnected(signal,r.live)); auto bad=browse; ++bad.generation;
    assert(!r.flow.browseConnected(bad,r.live)); r.dead(); }
  { Rig r; r.connect(); auto bad=cover; ++bad.avrcp_cid; r.live.cover=bad;
    r.flow.connected(bad,0,r.live); r.dead(); }
  { Rig r; assert(r.flow.signalConnected(signal,r.live)); assert(r.flow.takeBrowseAttempt(r.live));
    assert(!r.flow.browseConnected(browse,r.live,1)); r.dead(); }
  { Rig r; r.connect(); r.live.cover=cover; r.flow.connected(cover,1,r.live); r.dead(); }
  { Rig r; r.ready(); r.live.browse={}; r.flow.tick(1,r.live,r.io); r.dead(); }
  { Rig r; r.ready(); ArtCommandLine p; ArtCommand c;
    for(char ch:std::string("art image 123")) p.feed(ch,c,true);
    ++r.live.signal.generation; r.flow.tick(1,r.live,r.io);
    for(char ch:std::string("4567")) p.feed(ch,c,false);
    assert(p.feed('\n',c,false)==ArtCommandLine::Result::NotReady);
    assert(!r.flow.signalConnected(r.live.signal,r.live)); r.dead(); }
  { Rig r; r.receiving(); r.live.signal={}; r.flow.tick(12,r.live,r.io);
    assert(!r.flow.data(cover,ArtOperation::Image,1,0,&byte,1));
    assert(!r.flow.complete(cover,ArtOperation::Image,1,0,1)); r.dead(); }
}
void failures() {
  uint8_t bytes[2]={1,2};
  for(unsigned variant=0;variant<15;++variant) {
    Rig r; r.receiving(); const auto op=ArtOperation::Image;
    switch(variant) {
      case 0: assert(!r.flow.complete(cover,op,1,0,0)); break;
      case 1: assert(!r.flow.data(cover,op,0,0,bytes,1)); break;
      case 2: assert(!r.flow.data(cover,op,1,1,bytes,1)); break;
      case 3: assert(!r.flow.data(cover,op,1,0,nullptr,1)); break;
      case 4: assert(!r.flow.data(cover,op,1,0,bytes,0)); break;
      case 5: assert(!r.flow.data(cover,op,1,0,bytes,16385)); break;
      case 6: assert(!r.flow.data(cover,ArtOperation::Properties,1,0,bytes,1)); break;
      default:
        assert(r.flow.data(cover,op,9,0,bytes,1));
        if(variant==7) assert(!r.flow.data(cover,op,8,1,bytes,1));
        if(variant==8) assert(!r.flow.complete(cover,op,8,0,1));
        if(variant==9) assert(!r.flow.complete(cover,op,9,-1,1));
        if(variant==10) assert(!r.flow.complete(cover,op,9,0,2));
        if(variant==11) assert(!r.flow.complete(cover,ArtOperation::Thumbnail,9,0,1));
        if(variant==12) r.flow.error();
        if(variant==13) r.flow.disconnected();
        if(variant==14) { r.live.queue_lost=true; r.flow.tick(12,r.live,r.io); }
    }
    r.dead(); assert(r.io.cancels==1);
    assert(!r.flow.complete(cover,op,9,0,1)); r.dead();
  }
  { Rig r; r.connect(); assert(!r.flow.data(cover,ArtOperation::Image,1,0,bytes,1)); r.dead(); }
  { Rig r; r.receiving(); r.flow.connected(cover,0,r.live); r.dead(); }
  { Rig r; r.receiving(); assert(r.flow.data(cover,ArtOperation::Image,1,0,bytes,1));
    assert(r.flow.complete(cover,ArtOperation::Image,1,0,1));
    assert(!r.flow.complete(cover,ArtOperation::Image,1,0,1)); r.dead(); }
  for(unsigned stage=0;stage<5;++stage) {
    Rig r;
    if(stage==1) r.ready();
    if(stage==2) {r.ready(); assert(r.flow.submit(command(),r.live)==InputResult::Accepted);}
    if(stage==3) r.connect();
    if(stage==4) r.receiving();
    r.io.cancel_result=5;
    r.flow.cancel(); r.flow.cancel(); r.dead(); r.flow.stop(); r.dead();
    assert(r.io.cancels==(stage>=3 ? 1u:0u));
    if(stage>=3) assert(r.flow.cancelStatus()==5);
  }
}
void timers() {
  for(uint32_t start : {100u,UINT32_MAX-100u}) {
    { Rig r; r.connect(ArtOperation::Image,start); r.flow.tick(start+14999,r.live,r.io);
      assert(r.flow.state()==FlowState::Connecting); r.flow.tick(start+15000,r.live,r.io);
      assert(r.flow.failure()==FlowFailure::Timeout); r.dead(); }
    { Rig r; r.connect(ArtOperation::Image,start); r.live.cover=cover; r.flow.connected(cover,0,r.live);
      r.live.queue_empty=false; r.flow.tick(start+15000,r.live,r.io); r.dead(); assert(r.io.gets==0); }
    { Rig r; r.receiving(ArtOperation::Image,start); uint8_t b=1;
      assert(r.flow.data(cover,ArtOperation::Image,1,0,&b,1));
      r.flow.tick(start+30000,r.live,r.io); assert(r.flow.state()==FlowState::Receiving);
      r.flow.tick(start+30001,r.live,r.io); assert(r.flow.failure()==FlowFailure::Timeout); r.dead(); }
  }
}
void queue_order() {
  struct Record { unsigned kind=0; uint32_t offset=0; uint16_t size=0; std::array<uint8_t,512> bytes{}; };
  Rig r; r.connect(); OwnedRecordQueue<Record,8> q; Record event; event.kind=1;
  assert(q.push(event)); r.live.cover=cover; r.live.queue_empty=false;
  r.flow.tick(11,r.live,r.io); assert(r.io.gets==0); // Callback merely copied Connected.
  assert(q.pop(event)); r.flow.connected(cover,0,r.live);
  r.live.queue_empty=true; r.flow.tick(12,r.live,r.io); assert(r.io.gets==1);
  uint8_t b=42; event.kind=2; assert(q.push(event,&b,1)); b=9;
  event.kind=3; assert(q.push(event));
  while(q.pop(event)) {
    if(event.kind==2) assert(r.flow.data(cover,ArtOperation::Image,50,0,event.bytes.data(),event.size));
    else assert(r.flow.complete(cover,ArtOperation::Image,50,0,1));
  }
  assert(r.flow.object().data()[0]==42);
  // Same copied records arriving after live loss cannot revive the object.
  r.live.signal={}; r.flow.tick(13,r.live,r.io);
  event.kind=2; assert(q.push(event,&b,1)); event.kind=3; assert(q.push(event));
  while(q.pop(event)) {
    if(event.kind==2) assert(!r.flow.data(cover,ArtOperation::Image,50,0,event.bytes.data(),event.size));
    else assert(!r.flow.complete(cover,ArtOperation::Image,50,0,1));
  }
  r.dead();
  Rig lost; lost.receiving(); OwnedRecordQueue<Record,8> full;
  for(unsigned i=0;i<8;++i) assert(full.push(event,&b,1));
  assert(!full.push(event,&b,1)); lost.live.queue_lost=full.failed();
  lost.flow.tick(13,lost.live,lost.io); lost.dead();
}
int main() {
  parser(); success(); admission(); deferred_browse(); sessions(); failures(); timers(); queue_order();
  std::puts("PASS: real 16KiB CoverArtFlow, parser, three GET selections, full identities, ownership, ordering, rejection, one-shot cancel, stale sessions, loss and deadline wraparound (modeled transport only)");
}
