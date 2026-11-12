#include "FakeClient.h"
#include "examples/SppControlMainLoopSample/SppControlWorkflow.h"
#include <cstdio>
#include <algorithm>
using namespace spp_sample;
const spp::Address first{{2,0,0,0,0,1}}, next{{2,0,0,0,0,2}};
Options options(){Options o;o.first_peer=first;o.next_peer=next;o.timeout_us=1000;return o;}
using Flow=Workflow<FakeClient>;
void finish(Flow&f,uint64_t&t){
 const spp::Control order[]{spp::Control::QueryPort,spp::Control::ConfigurePort,spp::Control::LineStatus,spp::Control::ModemStatus};
 for(unsigned i=0;i<300&&!f.done();++i){
  const auto completed=f.report().controls_completed;f.step(++t);
  if(f.report().controls_completed!=completed){assert(completed<4);assert(f.report().last_control==order[completed]);}
 }
 assert(f.done());
}
void toControl(Flow&f,FakeClient&c,uint64_t&t){for(unsigned i=0;i<80&&!c.control_calls;++i)f.step(++t);assert(c.control_calls==1);}
unsigned cases=0;
void pass(const char*n){++cases;printf("PASS %s\n",n);}
int main(){
 for(auto mode:{FakeClient::Transport::Basic,FakeClient::Transport::Ertm}){
  FakeClient c({first});c.mode=mode;Flow f(c,options());uint64_t t=0;
  assert(f.start(t));assert(c.snapshot().runtime==spp::Runtime::Stopped);assert(c.actions.empty());
  finish(f,t);assert(f.stage()==Stage::Complete&&f.failure()==Failure::None);
  assert(f.report().controls_completed==4&&f.report().control_failures==0&&c.snapshot().config.peer==next);
  auto ix=[&](spp::Op op){return std::find(c.actions.begin(),c.actions.end(),op)-c.actions.begin();};
  assert(ix(spp::Op::RegisterService)<ix(spp::Op::Connect));
  assert(ix(spp::Op::UnregisterService)<ix(spp::Op::Disconnect));
  assert(ix(spp::Op::Disconnect)<ix(spp::Op::SelectPeer));
  assert(c.snapshot().routes[0].phase==spp::Phase::Closed&&c.snapshot().endpoints[0].phase==spp::EndpointPhase::Disabled);
  assert(!f.start(++t));pass("queued start, four serialized controls, service removal, close, peer confirmation");
 }
 {
  FakeClient c({first});c.hold_control=true;c.mode=FakeClient::Transport::Ertm;Flow f(c,options());uint64_t t=0;assert(f.start(t));toControl(f,c,t);
  const auto s=c.snapshot();assert(s.routes[0].control_pending);
  for(int i=0;i<5;++i) { f.step(++t); }assert(c.control_calls==1&&f.report().controls_completed==0);
  assert(f.report().routes[0].transport==FakeClient::Transport::Ertm);
  spp::Event e;e.handle=spp::handleOf(s,0);e.peer=first;e.cid=s.routes[0].cid;e.token=s.acl_token;e.kind=spp::Kind::RemoteLineStatus;e.control.value=3;assert(c.engine.post(e));
  e.kind=spp::Kind::RemoteModemStatus;e.control.value=0x8d;assert(c.engine.post(e));
  e.kind=spp::Kind::PortConfiguration;e.remote=true;e.control.port.baud=spp::Baud::B115200;assert(c.engine.post(e));
  for(int i=0;i<4;++i) { f.step(++t); }
  assert(f.report().routes[0].line_events==1&&f.report().routes[0].line_status==3);
  assert(f.report().routes[0].modem_events==1&&f.report().routes[0].modem_status==0x8d);
  assert(f.report().routes[0].remote_port_events==1&&f.report().controls_completed==0);
  c.hold_control=false;c.releaseDeferred();finish(f,t);assert(f.stage()==Stage::Complete);
  assert(f.report().routes[0].line_events==0);pass("pending control is not completion; copied incoming observations and generation reset");
 }
 {
  FakeClient c({first});c.control_error=0x0c;Flow f(c,options());uint64_t t=0;assert(f.start(t));finish(f,t);
  assert(f.stage()==Stage::Complete&&f.report().control_failures==4&&f.report().controls_completed==4);
  pass("local SDK control rejection recorded without treating it as lost stream or peer ACK");
 }
 {
  FakeClient c({first});c.hold_close=true;c.pool.active=1;Flow f(c,options());uint64_t t=0;assert(f.start(t));
  for(int i=0;i<100&&c.deferred.empty();++i)f.step(++t);
  assert(!c.deferred.empty()&&c.snapshot().routes[0].phase==spp::Phase::Closing);
  for(int i=0;i<5;++i) { f.step(++t); }assert(c.select_calls==0);
  c.releaseDeferred();for(int i=0;i<5;++i) { f.step(++t); }
  assert(c.snapshot().routes[0].phase==spp::Phase::Closed&&c.select_calls==0&&c.pool.active==1);
  c.pool.active=0;finish(f,t);assert(f.stage()==Stage::Complete);pass("Closed DLC does not release ERTM mux lease or permit early peer selection");
 }
 {
  FakeClient c({first});c.hold_selection=true;Flow f(c,options());uint64_t t=0;assert(f.start(t));
  for(int i=0;i<100&&f.stage()!=Stage::SelectingPeer;++i)f.step(++t);
  assert(f.stage()==Stage::SelectingPeer&&c.snapshot().config.peer==first&&!f.done());
  c.hold_selection=false;c.releaseDeferred();finish(f,t);assert(c.snapshot().config.peer==next);pass("selectPeer queue acceptance waits for snapshot confirmation");
 }
 {
  FakeClient c({first});c.hold_init=true;Flow f(c,options());uint64_t t=0;assert(f.start(t));f.step(++t);f.requestStop(t);
  for(int i=0;i<5;++i) { f.step(++t); }assert(!f.done()&&c.actions.size()==1);
  c.releaseDeferred();finish(f,t);assert(f.stage()==Stage::Complete&&c.select_calls==0);
  assert(std::find(c.actions.begin(),c.actions.end(),spp::Op::RegisterService)==c.actions.end());pass("stop during initialization waits for ready and starts no service");
 }
 {
  FakeClient c({first});c.hold_control=true;c.hold_close=true;c.pool.active=1;Flow f(c,options());uint64_t t=0;assert(f.start(t));toControl(f,c,t);
  t+=1001;f.step(t);assert(f.failure()==Failure::Timeout);
  for(int i=0;i<10;++i) { f.step(++t); }t+=1001;f.step(t);assert(f.stage()==Stage::Failed&&c.pool.active==1&&c.select_calls==0);
  pass("deadline fails closed without fabricating resource release");
 }
 {
  FakeClient c({first});Flow f(c,options());uint64_t t=0;assert(f.start(t));
  c.engine.poll();for(int i=0;i<20;++i){spp::Event e;e.kind=spp::Kind::Begin;assert(c.engine.post(e));c.engine.poll();}
  f.step(++t);assert(f.failure()==Failure::HistoryLost);assert(c.select_calls==0);pass("overwritten diagnostic history never becomes assumed success");
 }
 {
  FakeClient c({first});c.refuse_queue=true;Flow f(c,options());assert(!f.start(0));assert(f.failure()==Failure::NotQueued&&c.actions.empty());pass("rejected start is not advertised as started");
 }
 {
  FakeClient c({first});auto o=options();o.next_peer=first;Flow f(c,o);assert(!f.start(0));assert(f.failure()==Failure::InvalidOptions&&c.actions.empty());pass("invalid peer configuration has no side effects");
 }
 {
  FakeClient c({first});c.select_refusals=2;Flow f(c,options());uint64_t t=0;assert(f.start(t));finish(f,t);
  assert(f.stage()==Stage::Complete&&c.select_calls==3&&f.report().select_attempts==3);
  pass("private queue/binding refusal retries only a nonqueued selection");
 }
 {
  FakeClient c({first});c.hold_control=true;Flow f(c,options());uint64_t t=0;assert(f.start(t));toControl(f,c,t);
  const auto s=c.snapshot();spp::Event e;e.kind=spp::Kind::Closed;e.handle=spp::handleOf(s,0);e.cid=s.routes[0].cid;assert(c.engine.post(e));
  f.step(++t);assert(f.failure()==Failure::RouteClosed);finish(f,t);
  assert(f.stage()==Stage::Failed&&c.select_calls==0&&c.control_calls==1);
  pass("route closes during a control: no stale next control or peer selection");
 }
 {
  FakeClient c({first});c.hold_control=true;c.hold_sdp=true;Flow f(c,options());uint64_t t=0;assert(f.start(t));toControl(f,c,t);
  spp::Event e;e.kind=spp::Kind::Discover;e.peer=first;e.uuid=spp::Uuid::from16(0x1101);assert(c.engine.post(e));f.step(++t);
  assert(c.snapshot().query.busy);c.hold_control=false;c.releaseDeferred();
  for(int i=0;i<100&&c.snapshot().query.phase!=spp::QueryPhase::Cancelled;++i)f.step(++t);
  assert(c.snapshot().query.phase==spp::QueryPhase::Cancelled&&c.snapshot().query.busy);
  for(int i=0;i<5;++i) { f.step(++t); }
  assert(c.select_calls==0);c.releaseDeferred();finish(f,t);assert(f.stage()==Stage::Complete);
  pass("cancelled SDP still blocks selection until actual quiescence");
 }
 for(auto stage:{Stage::WaitingListen,Stage::WaitingOpen}){
  FakeClient c({first});Flow f(c,options());uint64_t t=0;assert(f.start(t));
  for(int i=0;i<100&&f.stage()!=stage;++i)f.step(++t);
  assert(f.stage()==stage);f.requestStop(t);finish(f,t);
  assert(f.stage()==Stage::Complete&&c.control_calls==0&&c.select_calls==0);
  assert(c.snapshot().endpoints[0].phase==spp::EndpointPhase::Disabled&&c.snapshot().routes[0].phase==spp::Phase::Closed);
  if(stage==Stage::WaitingListen)assert(std::find(c.actions.begin(),c.actions.end(),spp::Op::Connect)==c.actions.end());
  pass("stop after queued listen/connect waits for effects and tears them down");
 }
 {
  FakeClient c({first});c.hold_selection=true;Flow f(c,options());uint64_t t=0;assert(f.start(t));
  for(int i=0;i<100&&f.stage()!=Stage::SelectingPeer;++i)f.step(++t);
  assert(f.stage()==Stage::SelectingPeer);f.requestStop(t);
  for(int i=0;i<5;++i) { f.step(++t); }
  assert(!f.done()&&c.snapshot().config.peer==first);c.releaseDeferred();finish(f,t);
  assert(f.stage()==Stage::Complete&&c.snapshot().config.peer==next&&c.select_calls==1);
  pass("stop cannot silently cancel a queued selection; confirmation precedes completion");
 }
 printf("%u workflow cases PASS; actual sample header and core, modeled transport only\n",cases);
}
