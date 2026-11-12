#pragma once
// Production deterministic engine; transport outcomes below are test doubles.
#include "spp-core/SppCore.h"
#include <vector>
#include <cassert>
struct FakeClient : spp::ActionExecutor {
  enum class Transport { Basic, Ertm, Unknown };
  struct PoolStatus { uint8_t active = 0; } pool;
  spp::Engine<> engine;
  std::vector<spp::Op> actions;
  bool refuse_queue=false, hold_control=false, hold_close=false;
  bool hold_init=false, hold_sdp=false, hold_selection=false;
  int control_error=0;
  unsigned control_calls=0, select_calls=0, select_refusals=0;
  Transport mode=Transport::Basic;
  std::vector<spp::Event> deferred;
  explicit FakeClient(spp::Config config):engine(config,*this){}
  bool post(spp::Event e) { return !refuse_queue && engine.post(e); }
  bool begin(){spp::Event e;e.kind=spp::Kind::Begin;return post(e);}
  void poll(){engine.poll(1);}
  auto snapshot() const {return engine.snapshot();}
  size_t historySize()const{return engine.historySize();}
  auto history(size_t i)const{return engine.history(i);}
  uint64_t droppedEvents()const{return engine.droppedEvents();}
  auto ertmStatus()const{return pool;}
  Transport transport(spp::Handle h)const{return spp::matches(snapshot(),h)&&snapshot().routes[h.slot].phase==spp::Phase::Open?mode:Transport::Unknown;}
  bool listen(uint8_t slot,uint8_t channel,spp::Uuid uuid){spp::Event e;e.kind=spp::Kind::Listen;e.slot=slot;e.channel=channel;e.uuid=uuid;return post(e);}
  bool unlisten(uint8_t slot){spp::Event e;e.kind=spp::Kind::Unlisten;e.slot=slot;e.token=snapshot().endpoints[slot].token;return post(e);}
  bool connect(uint8_t slot,spp::Address peer,uint8_t channel){spp::Event e;e.kind=spp::Kind::ConnectChannel;e.slot=slot;e.peer=peer;e.channel=channel;return post(e);}
  bool control(spp::Handle h,spp::ControlRequest c){spp::Event e;e.kind=spp::Kind::ControlRequested;e.handle=h;e.control=c;return post(e);}
  bool queryPort(spp::Handle h){return control(h,{});}
  bool configurePort(spp::Handle h,spp::PortSettings p){spp::ControlRequest c;c.operation=spp::Control::ConfigurePort;c.port=p;return control(h,c);}
  bool sendLineStatus(spp::Handle h,uint8_t v){spp::ControlRequest c;c.operation=spp::Control::LineStatus;c.value=v;return control(h,c);}
  bool sendModemStatus(spp::Handle h,uint8_t v){spp::ControlRequest c;c.operation=spp::Control::ModemStatus;c.value=v;return control(h,c);}
  bool disconnect(spp::Handle h){spp::Event e;e.kind=spp::Kind::Disconnect;e.handle=h;return post(e);}
  bool cancelDiscovery(uint32_t token){spp::Event e;e.kind=spp::Kind::CancelDiscovery;e.token=token;return post(e);}
  bool selectPeer(spp::Address peer){++select_calls;if(select_refusals){--select_refusals;return false;}if(engine.pending()||pool.active)return false;spp::Event e;e.kind=spp::Kind::SelectPeer;e.peer=peer;if(hold_selection){deferred.push_back(e);return true;}return post(e);}
  void releaseDeferred(){for(auto e:deferred)assert(engine.post(e));deferred.clear();}
  void execute(const spp::Action&a,const spp::State&s,const uint8_t*,size_t)override{
    using namespace spp;actions.push_back(a.op);Event e;e.handle=a.handle;e.token=a.token;e.cid=a.cid;
    switch(a.op){
    case Op::Initialize:e.kind=Kind::InitResult;if(hold_init){deferred.push_back(e);return;}break;
    case Op::RegisterService:e.kind=Kind::ListenResult;e.slot=a.handle.slot;break;
    case Op::UnregisterService:e.kind=Kind::UnlistenResult;e.slot=a.handle.slot;break;
    case Op::Connect:{Event acl;acl.kind=Kind::AclOpened;acl.peer=s.config.peer;acl.cid=0x40;acl.token=1;assert(engine.post(acl));e.kind=Kind::Opened;e.peer=s.routes[a.handle.slot].peer;e.channel=s.routes[a.handle.slot].channel;e.cid=7+a.handle.slot;e.mtu=100;e.token=1;break;}
    case Op::GrantCredits:e.kind=Kind::CreditResult;break;
    case Op::Control:++control_calls;e.kind=Kind::ControlResult;e.status=control_error;if(hold_control){deferred.push_back(e);return;}break;
    case Op::Disconnect:case Op::CancelConnect:e.kind=Kind::Closed;if(hold_close){deferred.push_back(e);return;}break;
    case Op::CancelSdp:e.kind=Kind::SdpQuiesced;if(hold_sdp){deferred.push_back(e);return;}break;
    default:return;
    }
    assert(engine.post(e));
  }
};
