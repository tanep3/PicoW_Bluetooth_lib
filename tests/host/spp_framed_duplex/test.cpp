#include "examples/SppFramedDuplexSample/FramedDuplex.h"
#include "tests/host/spp_usage_sample/FakeClient.h"
#include <cstdio>
using namespace framed_sample;
const spp::Address peer{{2,0,0,0,0,1}};
struct Client : FakeClient {
  std::array<std::vector<uint8_t>,2> wire, delivered;
  bool hold=true;
  uint16_t mtu=100;
  unsigned writes=0, reads=0;
  Client():FakeClient({peer}){}
  auto read(spp::Handle h,uint8_t*p,size_t n){++reads;auto got=engine.read(h,p,n);if(h.slot<2)delivered[h.slot].insert(delivered[h.slot].end(),p,p+got.bytes);return got;}
  auto tryWrite(spp::Handle h,const uint8_t*p,size_t n){++writes;return engine.tryWrite(h,p,n);}
  void execute(const spp::Action&a,const spp::State&s,const uint8_t*p,size_t n)override {
    spp::Event e;e.handle=a.handle;e.cid=a.cid;e.token=a.token;
    if(a.op==spp::Op::RequestCanSend){e.kind=spp::Kind::CanSend;if(hold)deferred.push_back(e);else assert(engine.post(e));return;}
    if(a.op==spp::Op::Send){wire[a.handle.slot].insert(wire[a.handle.slot].end(),p,p+n);e.kind=spp::Kind::SendResult;assert(engine.post(e));return;}
    if(a.op==spp::Op::Connect){
      if(!s.acl_active){spp::Event acl;acl.kind=spp::Kind::AclOpened;acl.peer=peer;acl.cid=0x40;acl.token=1;assert(engine.post(acl));}
      e.kind=spp::Kind::Opened;e.peer=peer;e.channel=s.routes[a.handle.slot].channel;e.token=1;e.cid=7+a.handle.slot;e.mtu=mtu;assert(engine.post(e));return;
    }
    if(a.op==spp::Op::Accept){e.kind=spp::Kind::Opened;e.peer=s.config.peer;e.channel=s.routes[a.handle.slot].channel;e.token=s.acl_token;e.mtu=mtu;assert(engine.post(e));return;}
    FakeClient::execute(a,s,p,n);
  }
  void drain(){for(int i=0;i<100 && engine.pending();++i)engine.poll();assert(!engine.pending());}
  void setup(bool incoming){assert(begin());drain();if(incoming){
    assert(listen(0,1,spp::Uuid::from16(0x1101)));assert(listen(1,2,spp::Uuid::from16(0x1101)));drain();
    spp::Event e;e.kind=spp::Kind::AclOpened;e.peer=peer;e.cid=0x40;e.token=1;assert(engine.post(e));drain();
    for(uint8_t channel:{2,1}){e.kind=spp::Kind::Incoming;e.channel=channel;e.cid=uint16_t(10+channel);assert(engine.post(e));drain();}
  }else{assert(connect(0,peer,1));drain();assert(connect(1,peer,2));drain();}}
  void rx(uint8_t slot,const uint8_t*p,size_t n){auto s=snapshot();spp::Event e;e.handle=spp::handleOf(s,slot);e.cid=s.routes[slot].cid;assert(engine.receive(e,p,n));drain();}
};
using Flow=Workflow<Client>;
void step(Client&c,Flow&f){c.drain();f.observe();f.io();}
int main(){
 unsigned cases=0;
 for(bool incoming:{false,true}) {
  Client c;c.setup(incoming);Flow f(c);assert(f.selectMode(incoming?Mode::Incoming:Mode::Outgoing));f.observe();
  const uint8_t control=incoming?1:0,data=incoming?0:1;
  assert(f.routes()[control].role==Role::Control && f.routes()[data].role==Role::Data);
  assert(!f.selectMode(incoming?Mode::Outgoing:Mode::Incoming));
  assert(f.enqueue(Role::Control));assert(f.enqueue(Role::Data));assert(!f.enqueue(Role::Data));
  c.hold=false;for(int i=0;i<20;++i)step(c,f);c.drain();
  assert(c.wire[control]==std::vector<uint8_t>(std::begin(ControlFrame),std::end(ControlFrame)));
  assert(c.wire[data]==std::vector<uint8_t>(std::begin(DataFrames),std::end(DataFrames)));
  c.rx(control,ControlFrame,1);step(c,f);c.rx(control,ControlFrame+1,sizeof(ControlFrame)-1);step(c,f);
  c.rx(data,DataFrames,sizeof(DataFrames));step(c,f);
  assert(f.routes()[control].controls==1 && f.routes()[control].last_command==7);
  assert(f.routes()[data].temperatures==1 && f.routes()[data].last_temperature==2500);
  assert(f.routes()[data].vectors==1 && (f.routes()[data].last_vector==std::array<uint16_t,3>{{10,20,30}}));
  assert(c.delivered[control]==std::vector<uint8_t>(std::begin(ControlFrame),std::end(ControlFrame)));
  assert(c.delivered[data]==std::vector<uint8_t>(std::begin(DataFrames),std::end(DataFrames)));
  // Compare exact reassembled receive bytes using the core's read copy API too.
  c.rx(data,DataFrames,sizeof(DataFrames));std::array<uint8_t,sizeof(DataFrames)> read{};
  auto got=c.read(spp::handleOf(c.snapshot(),data),read.data(),read.size());assert(got.bytes==read.size());assert(std::equal(read.begin(),read.end(),DataFrames));c.drain();
  ++cases;printf("PASS %s exact TX both roles, fragmented/coalesced typed RX, actual core byte copy\n",incoming?"incoming reversed order":"outgoing");
 }
 {
  Client c;c.setup(false);Flow f(c);assert(f.selectMode(Mode::Outgoing));f.observe();
  std::array<uint8_t,spp::TxCapacity-3> prefix{};prefix.fill(0x55);
  auto h=spp::handleOf(c.snapshot(),0);assert(c.tryWrite(h,prefix.data(),prefix.size()).bytes==prefix.size());c.drain();
  assert(f.enqueue(Role::Control));step(c,f);assert(f.routes()[0].tx_offset==3);
  unsigned before=c.writes;step(c,f);assert(c.writes==before+1 && f.routes()[0].tx_offset==3); // WouldBlock
  spp::Event tick;assert(c.engine.post(tick));before=c.writes;f.io();assert(c.writes==before+1 && f.routes()[0].tx_offset==3); // Busy
  c.hold=false;c.releaseDeferred();for(int i=0;i<30;++i)step(c,f);c.drain();
  std::vector<uint8_t> expected(prefix.begin(),prefix.end());expected.insert(expected.end(),std::begin(ControlFrame),std::end(ControlFrame));
  assert(c.wire[0]==expected);assert(f.routes()[0].tx_offset==sizeof(ControlFrame));
  ++cases;puts("PASS actual core partial acceptance/WouldBlock/Busy, bounded retry and exact suffix");
 }
 {
  Client c;c.setup(false);Flow f(c);assert(f.selectMode(Mode::Outgoing));f.observe();
  c.rx(0,ControlFrame,3);step(c,f);assert(f.routes()[0].rx_size==3);
  assert(f.enqueue(Role::Control));auto old=spp::handleOf(c.snapshot(),0);
  assert(c.disconnect(old));c.drain();f.observe();assert(f.discardedTx()==6 && f.discardedRx()==3);
  assert(c.connect(0,peer,1));c.drain();f.observe();assert(f.routes()[0].handle.generation!=old.generation);
  assert(f.routes()[0].tx_size==0 && f.routes()[0].rx_size==0 && f.routes()[0].controls==0);
  assert(c.tryWrite(old,ControlFrame,sizeof(ControlFrame)).error==spp::Error::InvalidHandle);
  c.rx(0,ControlFrame,sizeof(ControlFrame));step(c,f);assert(f.routes()[0].controls==1);
  assert(c.wire[0].empty());++cases;puts("PASS close/generation truncates pending TX/RX; no replay or concatenation");
 }
 {
  Client c;c.setup(false);Flow f(c);assert(f.selectMode(Mode::Outgoing));f.observe();
  c.rx(0,DataFrames,4);step(c,f);assert(f.invalidFrames()==1 && f.routes()[0].invalid);assert(!f.enqueue(Role::Control));
  auto reads=c.reads;step(c,f);assert(c.reads==reads+1); // unaffected data route only
  ++cases;puts("PASS wrong-role header stops that route without echo or resync");
 }
 {
  Client c;c.setup(false);Flow f(c);assert(f.selectMode(Mode::Outgoing));f.observe();
  std::array<uint8_t,spp::TxCapacity-2> prefix{};auto h=spp::handleOf(c.snapshot(),1);
  assert(c.tryWrite(h,prefix.data(),prefix.size()).bytes==prefix.size());c.drain();
  assert(f.enqueue(Role::Data));step(c,f);assert(f.routes()[1].tx_offset==2);
  c.rx(1,DataFrames,5);step(c,f);assert(f.routes()[1].rx_size==5);
  assert(c.disconnect(h));c.drain();assert(c.connect(1,peer,2));c.drain();
  // No observe between close/reopen: generation still retires the old parser/TX.
  f.observe();assert(f.discardedTx()==sizeof(DataFrames)-2 && f.discardedRx()==5);
  assert(f.routes()[1].tx_size==0 && f.routes()[1].rx_size==0);
  assert(c.snapshot().routes[1].tx_size==0);
  c.rx(1,DataFrames,sizeof(DataFrames));step(c,f);assert(f.routes()[1].temperatures==1 && f.routes()[1].vectors==1);
  assert(c.wire[1].empty());++cases;puts("PASS partially accepted data and partial RX discarded across unseen close/reopen");
 }
 {
  Client c;c.setup(false);Flow f(c);assert(f.selectMode(Mode::Outgoing));f.observe();
  c.rx(0,ControlFrame,3);step(c,f);
  std::array<uint8_t,spp::TxCapacity-2> prefix{};
  assert(c.tryWrite(spp::handleOf(c.snapshot(),0),prefix.data(),prefix.size()).bytes==prefix.size());c.drain();
  assert(f.enqueue(Role::Control));step(c,f);assert(f.routes()[0].tx_offset==2);
  f.discardApplication();c.refuse_queue=true;assert(!c.disconnect(spp::handleOf(c.snapshot(),0)));
  for(int i=0;i<3;++i)step(c,f);
  assert(f.routes()[0].invalid && !f.enqueue(Role::Control));assert(c.wire[0].empty());
  c.refuse_queue=false;c.hold_close=true;assert(c.disconnect(spp::handleOf(c.snapshot(),0)));step(c,f);
  assert(c.snapshot().routes[0].phase==spp::Phase::Closing);
  c.releaseDeferred();c.drain();assert(c.connect(0,peer,1));c.drain();f.observe();assert(!f.routes()[0].invalid);
  assert(f.enqueue(Role::Control));++cases;puts("PASS manual abort remains stopped on refused/delayed disconnect until new generation");
 }
 for(size_t split: {size_t(2),size_t(5),size_t(9)}) {
  Client c;c.setup(false);Flow f(c);assert(f.selectMode(Mode::Outgoing));f.observe();
  c.rx(1,DataFrames,split);step(c,f);c.rx(1,DataFrames+split,sizeof(DataFrames)-split);step(c,f);
  assert(f.routes()[1].temperatures==1 && f.routes()[1].vectors==1);
 }
 ++cases;puts("PASS fixed data frame pair split inside header, payload, and second header");
 {
  Client c;c.setup(false);Flow f(c);assert(f.selectMode(Mode::Outgoing));f.observe();
  std::array<uint8_t,sizeof(DataFrames)*3> bytes{};for(int i=0;i<3;++i)std::copy(std::begin(DataFrames),std::end(DataFrames),bytes.begin()+i*sizeof(DataFrames));
  c.rx(1,bytes.data(),bytes.size());step(c,f);assert(f.routes()[1].vectors==2);step(c,f);
  assert(f.routes()[1].vectors==3 && f.routes()[1].temperatures==3);
  assert(c.delivered[1]==std::vector<uint8_t>(bytes.begin(),bytes.end()));
  ++cases;puts("PASS coalesced 48-byte receive crosses bounded 32-byte reads");
 }
 for(auto bad: {std::array<uint8_t,4>{{2,1,2,0}},std::array<uint8_t,4>{{1,1,255,255}}}) {
  Client c;c.setup(false);Flow f(c);assert(f.selectMode(Mode::Outgoing));f.observe();c.rx(0,bad.data(),bad.size());step(c,f);
  assert(f.routes()[0].invalid && f.invalidFrames()==1);
 }
 ++cases;puts("PASS fixed invalid version/length stops parser before payload indexing");
 {
  Client c;c.mtu=1019;c.setup(false);c.hold=false;Flow f(c);assert(f.selectMode(Mode::Outgoing));f.observe();
  for(int turn=0;turn<48;++turn) {
    c.drain();f.observe();f.enqueue(Role::Control);f.enqueue(Role::Data);
    const auto state=c.snapshot();
    if(state.routes[0].credits) c.rx(0,ControlFrame,sizeof(ControlFrame));
    if(state.routes[1].credits) c.rx(1,DataFrames,sizeof(DataFrames));
    step(c,f);
  }
  c.drain();assert(f.routes()[0].controls>=6 && f.routes()[1].vectors>=6);
  assert(c.wire[0].size()>=6*sizeof(ControlFrame) && c.wire[1].size()>=6*sizeof(DataFrames));
  for(size_t i=0;i<c.wire[0].size();++i)assert(c.wire[0][i]==ControlFrame[i%sizeof(ControlFrame)]);
  for(size_t i=0;i<c.wire[1].size();++i)assert(c.wire[1][i]==DataFrames[i%sizeof(DataFrames)]);
  ++cases;puts("PASS MTU1019 one-credit pressure: both routes/directions progress with rotating opportunities");
 }
 printf("%u fixed cases PASS: real SppCore, modeled callbacks; no SDK/radio execution\n",cases);
}
