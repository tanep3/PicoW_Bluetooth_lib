#include "examples/AvrcpMainLoopSample/OwnedRecordQueue.h"
#include <cassert>
#include <deque>
#include <cstdio>
struct Record { unsigned id=0; uint16_t size=0; std::array<uint8_t,512> bytes{}; };
using Queue=avrcp_sample::OwnedRecordQueue<Record,8>;
int main() {
 Queue q; Record out; out.id=99;
 assert(!q.pop(out) && out.id==99 && !q.failed());
 std::array<uint8_t,513> input{};
 for (unsigned i=0;i<512;++i) input[i]=uint8_t(i);
 Record r; r.id=7;
 assert(q.push(r,input.data(),512)); input.fill(0);
 assert(q.pop(out) && out.id==7 && out.size==512);
 for (unsigned i=0;i<512;++i) assert(out.bytes[i]==uint8_t(i));
 assert(q.push(r,nullptr,0)); assert(q.pop(out) && out.size==0);
 assert(!q.push(r,nullptr,1) && q.failed() && q.size()==0);
 assert(!q.push(r,input.data(),513) && q.size()==0);
 for(unsigned i=0;i<8;++i){r.id=i;assert(q.push(r));}
 assert(!q.push(r) && q.size()==8);
 for(unsigned i=0;i<8;++i){assert(q.pop(out));assert(out.id==i);}
 assert(q.failed()); // failure is sticky even after drain
 Queue wrap; std::deque<unsigned> oracle; unsigned seed=19;
 for(unsigned i=0;i<40000;++i) {
  seed=seed*1664525u+1013904223u;
  if(seed&0x10000u){r.id=i;bool ok=wrap.push(r);assert(ok==(oracle.size()<8));if(ok)oracle.push_back(i);}
  else {bool ok=wrap.pop(out);assert(ok==!oracle.empty());if(ok){assert(out.id==oracle.front());oracle.pop_front();}}
  assert(wrap.size()==oracle.size());
 }
 std::puts("PASS owned bytes, empty/null/oversize, full, FIFO, sticky failure and 40000 oracle steps; no SDK/radio");
}
