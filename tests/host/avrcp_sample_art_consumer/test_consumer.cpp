#include "examples/AvrcpMainLoopSample/CoverArtObject.h"
#include "examples/AvrcpMainLoopSample/OwnedRecordQueue.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>
using namespace avrcp_sample;
using Object = CoverArtObject<1024>;
const ArtBinding binding{{1,2,3,4,5,6}, 7, 8, 9, 10};
constexpr auto op = ArtOperation::Image;
void arm(Object &o) { o.reset(); assert(o.arm(binding, op)); }
void dead(const Object &o) {
  assert(o.state() == ArtState::Discarded && !o.data() && !o.size() && !o.received());
}
int main() {
  Object o; uint8_t bytes[1025]{};
  for (size_t i=0; i<sizeof(bytes); ++i) bytes[i] = uint8_t(i);
  arm(o); assert(!o.data());
  assert(o.append(binding,op,17,0,bytes,512));
  bytes[0] = 99;
  assert(o.append(binding,op,17,512,bytes+512,512));
  assert(!o.data() && !o.size() && o.received()==1024);
  assert(o.complete(binding,op,17,0,1024)); assert(o.size()==1024 && o.data()[0]==0);
  for(size_t i=1;i<1024;++i) assert(o.data()[i]==uint8_t(i));
  assert(!o.complete(binding,op,17,0,1024)); dead(o);
  arm(o); assert(!o.append(binding,op,1,0,bytes,1025)); dead(o);
  arm(o); assert(!o.append(binding,op,1,0,bytes,std::numeric_limits<size_t>::max())); dead(o);
  arm(o); assert(!o.append(binding,op,1,0,nullptr,1)); dead(o);
  arm(o); assert(!o.append(binding,op,1,0,nullptr,0)); dead(o);
  arm(o); assert(!o.append(binding,op,1,0,bytes,0)); dead(o);
  arm(o); assert(!o.complete(binding,op,1,0,0)); dead(o);
  arm(o); assert(!o.append(binding,op,0,0,bytes,1)); dead(o);
  for(unsigned field=0;field<10;++field) {
    auto b=binding;
    if(field<6) ++b.peer[field];
    else if(field==6) ++b.epoch;
    else if(field==7) ++b.generation;
    else if(field==8) ++b.cid;
    else ++b.avrcp_cid;
    arm(o); assert(!o.append(b,op,1,0,bytes,1)); dead(o);
    arm(o); assert(o.append(binding,op,1,0,bytes,1));
    assert(!o.complete(b,op,1,0,1)); dead(o);
  }
  arm(o); assert(!o.append(binding,ArtOperation::Properties,1,0,bytes,1)); dead(o);
  arm(o); assert(o.append(binding,op,1,0,bytes,1));
  assert(!o.complete(binding,ArtOperation::Thumbnail,1,0,1)); dead(o);
  for(uint32_t offset : {0u,1u,3u,UINT32_MAX}) {
    arm(o); assert(o.append(binding,op,1,0,bytes,2));
    assert(!o.append(binding,op,1,offset,bytes,1)); dead(o);
  }
  arm(o); assert(!o.append(binding,op,1,1,bytes,1)); dead(o);
  arm(o); assert(o.append(binding,op,1,0,bytes,1));
  assert(!o.append(binding,op,2,1,bytes,1)); dead(o);
  for(int variant=0;variant<4;++variant) {
    arm(o); assert(o.append(binding,op,1,0,bytes,2));
    assert(!o.complete(binding,op,variant==0?2:1,variant==1?-1003:0,
                       variant==2?1:variant==3?3:2)); dead(o);
  }
  for(auto reason : {ArtFailure::Error,ArtFailure::Cancelled,ArtFailure::Disconnected,ArtFailure::QueueLoss}) {
    arm(o); assert(o.append(binding,op,1,0,bytes,1)); o.discard(reason); dead(o);
    assert(!o.complete(binding,op,1,0,1)); assert(o.failure()==reason);
    assert(!o.append(binding,op,1,0,bytes,1)); dead(o);
  }
  arm(o); assert(!o.arm(binding,op)); dead(o);
  for(unsigned i=0;i<5;++i) {
    auto b=binding; if(i==0)b.peer={}; if(i==1)b.epoch=0;if(i==2)b.generation=0;
    if(i==3)b.cid=0;
    if(i==4)b.avrcp_cid=0;
    o.reset();assert(!o.arm(b,op));dead(o);
  }
  o.reset();assert(!o.arm(binding,static_cast<ArtOperation>(255)));dead(o);
  arm(o); assert(o.append(binding,op,1,0,bytes,1024));
  assert(!o.append(binding,op,1,1024,bytes,1)); dead(o);
  for(auto operation : {ArtOperation::Image, ArtOperation::Thumbnail, ArtOperation::Properties}) {
    o.reset(); assert(o.arm(binding,operation));
    assert(o.append(binding,operation,UINT32_MAX,0,bytes,1));
    assert(o.complete(binding,operation,UINT32_MAX,0,1));
    assert(o.size()==1);
  }
  // Reset also revokes previously complete storage and permits deliberate reuse.
  o.reset(); assert(o.state()==ArtState::Idle && !o.data() && !o.received());
  CoverArtObject<1> tiny; assert(tiny.arm(binding,op));
  assert(tiny.append(binding,op,1,0,bytes,1)); assert(tiny.complete(binding,op,1,0,1));
  // Real queue public interface, synthetic records: FIFO completion is essential.
  struct Record { bool terminal=false; uint32_t offset=0; uint16_t size=0;
                  std::array<uint8_t,512> bytes{}; };
  OwnedRecordQueue<Record,8> queue;
  bytes[0]=42; Record r; assert(queue.push(r,bytes,512));
  r.offset=512; assert(queue.push(r,bytes+512,512));
  r.terminal=true; assert(queue.push(r)); bytes[0]=17;
  arm(o);
  while(queue.pop(r)) {
    if(r.terminal) assert(o.complete(binding,op,88,0,1024));
    else assert(o.append(binding,op,88,r.offset,r.bytes.data(),r.size));
  }
  assert(o.data()[0]==42);
  OwnedRecordQueue<Record,1> lost;
  assert(lost.push(r));assert(!lost.push(r));
  if(lost.failed())o.discard(ArtFailure::QueueLoss);
  dead(o);
  // The actual sample gate allows one invalid-object request across multiple
  // queued bad chunks, even after helper reset. No transport/callback claim.
  OwnedRecordQueue<Record,8> malformed;
  r = {}; r.offset = 1;
  for(unsigned i=0;i<5;++i) assert(malformed.push(r,bytes,1));
  ArtCancellationGate gate;
  unsigned cancel_requests = 0;
  arm(o);
  while(malformed.pop(r)) {
    if (!o.append(binding,op,1,r.offset,r.bytes.data(),r.size)) {
      o.discard(ArtFailure::Count);
      if(gate.take()) ++cancel_requests;
    }
  }
  assert(cancel_requests==1); dead(o);
  arm(o); assert(!o.append(binding,op,1,1,bytes,1));
  if(gate.take()) ++cancel_requests;
  assert(cancel_requests==1);
  // Seeded independent vector reference for bounded valid transfers and faults.
  std::mt19937 rng(0xabc123);
  for(unsigned trial=0;trial<10000;++trial) {
    arm(o); std::vector<uint8_t> ref; const size_t total=1+rng()%1024;
    while(ref.size()<total) {
      const size_t n=1+rng()%(total-ref.size());
      for(size_t i=0;i<n;++i)bytes[i]=uint8_t(rng());
      assert(o.append(binding,op,trial+1,uint32_t(ref.size()),bytes,n));
      ref.insert(ref.end(),bytes,bytes+n); bytes[0]^=0xff;
    }
    if(trial%3==0) {
      assert(!o.append(binding,op,trial+1,uint32_t(total-1),bytes,1));dead(o);
    } else {
      assert(o.complete(binding,op,trial+1,0,uint32_t(total)));
      assert(o.size()==ref.size());
      assert(std::memcmp(o.data(),ref.data(),ref.size())==0);
    }
  }
  std::printf("PASS: Cover Art consumer boundaries, identities, ownership, queue, 10000 reference transfers; sizeof(Object)=%zu\n",sizeof(o));
}
