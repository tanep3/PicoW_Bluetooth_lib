#include "Model.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
extern "C" int __wrap_printf(const char *format,...) {
 assert(!model::callback); char text[2048];va_list ap;va_start(ap,format);int n=vsnprintf(text,sizeof text,format,ap);va_end(ap);assert(n>=0&&size_t(n)<sizeof text);model::log+=text;return n;
}
extern "C" int __wrap_puts(const char*s){assert(!model::callback);model::log+=s;model::log+='\n';return 0;}
#define main firmware_main_for_test
#include "examples/AvrcpMediaControlSample/main.cpp"
#undef main
using namespace avrcp_media_sample;
using CT=picow::avrcp::AvrcpController;
const Address peer{{1,2,3,4,5,6}};
const Binding live{peer,7,8,9};
void has(const char*s){if(model::log.find(s)==std::string::npos){fprintf(stderr,"Missing %s in:\n%s",s,model::log.c_str());assert(false);}}
void hci(uint8_t code,uint8_t state){uint8_t p[]={code,1,state};model::emit([&]{guard->callback(HCI_EVENT_PACKET,0,p,sizeof p);});}
void start(Application&a){a.command(Command::Start,{});assert(model::begins==1&&CT::instance->timeout==700);hci(BTSTACK_EVENT_STATE,HCI_STATE_WORKING);a.tick();assert(model::presses==0&&model::queries==0&&model::connects==0&&model::listens==0);}
void event(CT::EventKind k,uint8_t response=9,Binding b=live,uint8_t op=0x46){model::emit([&]{CT::instance->cb({k,b,op,response},CT::instance->ctx);});}
void status(Binding b=live,uint8_t ctype=12){uint8_t p[15]={HCI_EVENT_AVRCP_META,13,AVRCP_SUBEVENT_PLAY_STATUS,9,0,ctype,0x78,0x56,0x34,0x12,0,0,0,0,1};model::emit([&]{CT::instance->sdk({b,p,15},CT::instance->sdkctx);});memset(p,0xee,sizeof p);}
void parserTests(){
 struct Case{const char*text;Command command;};
 for(auto c:{Case{"START\n",Command::Start},{"PAIR\n",Command::Pair},{"LISTEN\n",Command::Listen},{"CONNECT_AVRCP\n",Command::Connect},{"PAUSE\r\n",Command::Pause},{"PLAY\n",Command::Play},{"QUERY_STATUS\n",Command::Query},{"STATUS\n",Command::Status},{"DISCONNECT\n",Command::Disconnect},{"SHUTDOWN\n",Command::Shutdown},{"\n",Command::None},{" PLAY\n",Command::Invalid},{"play\n",Command::Invalid},{"PLAY\rX\n",Command::Invalid},{"PLAY\r\r\n",Command::Invalid}}){CommandParser p;Command got=Command::None;for(const char*s=c.text;*s;++s)got=p.feed(*s);assert(got==c.command&&p.empty());}
 CommandParser p;for(unsigned i=0;i<10000;++i)assert(p.feed('A')==Command::None);assert(p.feed('\n')==Command::Invalid&&p.empty());
 for(int c:{0,1,31,127,255,-2}){p.feed(c);assert(p.feed('\n')==Command::Invalid);}
 for(char c:std::string("PLAY\n")) {auto got=p.feed(c);if(c=='\n')assert(got==Command::Play);}
}
int main(int argc,char**argv){assert(argc==2);std::string s=argv[1];using namespace model;
 if(s=="parser"){parserTests();}
 else if(s=="default"){assert(firmware_main_for_test()==0);assert(stdios==0&&begins==0&&watchdogs==0&&mutations==0);}
 else if(s=="console"||s=="console_stale"||s=="console_pair"){
  struct Done{};input=s=="console"?"STATUS\nSTART\n":"START\n";
  on_sleep=[&]{
   if(sleeps==1){assert(begins==1);hci(BTSTACK_EVENT_STATE,HCI_STATE_WORKING);if(s!="console_pair")CT::instance->live=live;input=s=="console_pair"?"PAIR\n":s=="console_stale"?"PA":"PAUSE\n";input_at=0;}
   if(sleeps==2&&s=="console_stale"){++CT::instance->live.generation;input="USE\n";input_at=0;}
   if(sleeps==3)throw Done{};
  };
  try{firmware_main_for_test();assert(false);}catch(const Done&){}
  assert(stdios==1&&watchdogs==1&&begins==1);
  if(s=="console_stale")assert(presses==0);
  else if(s=="console_pair")assert(listens==unsigned(AVRCP_MEDIA_ALLOW_PAIRING!=0));
  else assert(presses==1&&getters==0);
 }
 else {
  Application a(peer,true);
  if(s=="preinit"){a.command(Command::Status,{});a.command(Command::Play,{});a.tick();assert(getters==0&&begins==0&&mutations==0);has("initialized=0 hci=255");}
  else if(s=="startup_timeout"){a.command(Command::Start,{});ms=15000;on_poll=[] {assert(poweroffs==1);};a.tick();has("reason=3");}
  else if(s=="begin_failure"){fail_begin=true;a.command(Command::Start,{});a.command(Command::Start,{});assert(begins==1&&controller_begins==0);has("reason=9");}
  else {
   start(a);
   if(s=="start_once"){a.command(Command::Start,{});a.command(Command::Shutdown,{});a.command(Command::Start,{});a.command(Command::Play,live);a.tick();assert(begins==1&&poweroffs==1&&presses==0);}
   else if(s=="pair"){a.command(Command::Pair,{});assert(listens==1&&policies==2);a.command(Command::Pair,{});assert(listens==1);ms=300000;a.tick();has("PAIR_WINDOW_CLOSED");assert(policies==4);}
   else if(s=="listen"){a.command(Command::Listen,{});assert(listens==1&&policies==0);}
   else if(s=="no_binding"){a.command(Command::Play,{});a.command(Command::Query,{});a.command(Command::Connect,{});assert(presses==0&&queries==0&&connects==0);signaling=true;active_link=true;a.command(Command::Connect,{});assert(connects==1);a.command(Command::Play,{});assert(presses==0);}
   else if(s=="lifetime"){ms=900000;on_poll=[] {assert(poweroffs==1);};a.tick();has("reason=2");}
   else if(s=="hci_off"){hci(BTSTACK_EVENT_STATE,HCI_STATE_OFF);a.tick();has("reason=12");assert(poweroffs==1);}
   else if(s=="foreign_hci"||s=="unapproved_pairing"){
    uint8_t p[12]={uint8_t(s=="foreign_hci"?HCI_EVENT_CONNECTION_REQUEST:HCI_EVENT_USER_CONFIRMATION_REQUEST),10,1,2,3,4,5,6,0,0,0,0};
    if(s=="foreign_hci")++p[2];
    model::emit([&]{guard->callback(HCI_EVENT_PACKET,0,p,sizeof p);});a.command(Command::Play,live);assert(presses==0&&poweroffs==1);has(s=="foreign_hci"?"reason=7":"reason=8");
   }
   else if(s=="allowed_pairing"){
    a.command(Command::Pair,{});uint8_t p[12]={HCI_EVENT_USER_CONFIRMATION_REQUEST,10,1,2,3,4,5,6};
    model::emit([&]{guard->callback(HCI_EVENT_PACKET,0,p,sizeof p);});a.tick();assert(poweroffs==0&&listens==1);
   }
   else if(s=="malformed_status"){
    uint8_t p[14]={HCI_EVENT_AVRCP_META,12,AVRCP_SUBEVENT_PLAY_STATUS};
    model::emit([&]{CT::instance->sdk({live,p,sizeof p},CT::instance->sdkctx);});a.tick();assert(poweroffs==1);has("queue_loss=1");
   }
   else if(s=="disconnect_a2dp_only"){
    signaling=true;active_link=true;assert(CT::instance->live.cid==0);
    a.command(Command::Disconnect,{});assert(sink_disconnects==1&&controller_disconnects==0&&policies==2);assert(poweroffs==0);
   }
   else if(s=="disconnect_stale"){
    signaling=true;CT::instance->live=live;
    for(int i=0;i<5;++i){auto b=live;if(i==0)++b.peer[0];if(i==1)++b.epoch;if(i==2)++b.generation;if(i==3)++b.cid;if(i==4)b={};a.command(Command::Disconnect,b);}
    assert(sink_disconnects==0&&controller_disconnects==0&&policies==0);
   }
   else if(s=="disconnect_live"||s=="disconnect_reject"){
    signaling=true;CT::instance->live=live;if(s=="disconnect_reject")local_rc=-8;
    a.command(Command::Disconnect,live);assert(controller_disconnects==1&&sink_disconnects==1&&policies==2);
    has(s=="disconnect_reject"?"DISCONNECT_LOCAL rc=-8":"DISCONNECT_LOCAL rc=0");assert(poweroffs==0);
   }
   else if(s=="disconnect_no_retry"){
    CT::instance->live=live;a.command(Command::Pause,live);event(CT::EventKind::Disconnected);CT::instance->live={};a.tick();ms=5000;a.tick();assert(connects==0&&presses==1&&releases==0&&queries==0&&poweroffs==0);
   }
   else if(s=="queue_overflow"){for(int i=0;i<33;++i)event(CT::EventKind::Connected);a.command(Command::Play,live);assert(presses==0);a.tick();assert(poweroffs==1);has("queue_loss=1");}
   else if(s=="poll_overflow_press"||s=="poll_overflow_query"){
    CT::instance->live=live;
    bool query=s=="poll_overflow_query";
    a.command(query?Command::Query:Command::Pause,live);
    CT::instance->phase_=CT::Phase::Held;
    on_poll=[query] {
     if(query)status();else event(CT::EventKind::PressResponse);
     for(unsigned i=0;i<32;++i)event(CT::EventKind::Connected);
    };
    a.tick();assert(releases==0&&poweroffs==1);has("queue_loss=1");
    assert(log.find("remote_accepted=1")==std::string::npos);
    assert(log.find("interpreted=1")==std::string::npos);
    if(query&&log.find("PLAY_STATUS")!=std::string::npos)has("interpreted=0");
   }
   else if(s=="poll_overflow"){on_poll=[] {for(int i=0;i<33;++i)event(CT::EventKind::Connected);};a.tick();assert(poweroffs==1);has("queue_loss=1");}
   else {
    CT::instance->live=live;
    if(s=="local_reject"){
     local_rc=-8;a.command(Command::Pause,live);a.command(Command::Query,live);assert(presses==1&&queries==1);ms=1500;a.tick();assert(poweroffs==0);has("PRESS_LOCAL op=70 rc=-8");
    }else if(s=="query_copy"||s=="query_reject"||s=="query_stale"){
     a.command(Command::Query,live);assert(queries==1);a.command(Command::Play,live);assert(presses==0);
     auto b=live;if(s=="query_stale")++b.generation;status(b,s=="query_reject"?10:12);a.tick();has("length_ms=305419896");has(s=="query_copy"?"interpreted=1":"interpreted=0");
     a.command(Command::Pause,live);assert(presses==(s=="query_stale"?0u:1u));
    }else if(s=="stale_input"){
     for(int i=0;i<4;++i){auto b=live;if(i==0)++b.peer[0];if(i==1)++b.epoch;if(i==2)++b.generation;if(i==3)++b.cid;a.command(Command::Play,b);}assert(presses==0);
    }else {
     a.command(Command::Pause,live);assert(presses==1);a.command(Command::Play,live);a.command(Command::Query,live);assert(presses==1&&queries==0);
     if(s=="timeout"){ms=1500;on_poll=[] {assert(poweroffs==1);};a.tick();assert(poweroffs==1);has("reason=10");}
     else if(s=="sdk_timeout"){ms=700;on_poll=[] {event(CT::EventKind::Timeout);};a.tick();assert(poweroffs==1);}
     else if(s=="operation_mismatch"){CT::instance->phase_=CT::Phase::Held;event(CT::EventKind::PressResponse,9,live,0x44);a.tick();assert(releases==0);}
     else if(s=="release_without_press"){event(CT::EventKind::ReleaseResponse);a.tick();has("remote_accepted=0");}
     else if(s=="stale_event"){auto b=live;++b.generation;event(CT::EventKind::PressResponse,9,b);a.tick();assert(releases==0);}
     else {
      assert(s=="click"||s=="press_reject"||s=="release_reject"||s=="release_local_fail");
      bool reject=s=="press_reject";CT::instance->phase_=reject?CT::Phase::ReleasePending:CT::Phase::Held;
      event(CT::EventKind::PressResponse,reject?10:9);assert(releases==0);
      if(s=="release_local_fail")local_rc=-7;
      a.tick();assert(releases==(reject?0u:1u));
      if(s=="release_local_fail"){assert(poweroffs==1);has("reason=11");}
      else {event(CT::EventKind::ReleaseResponse,s=="release_reject"?10:9);a.tick();has(s=="click"?"remote_accepted=1":"remote_accepted=0");a.command(Command::Query,live);assert(queries==1);}
     }
    }
   }
  }
 }
 fprintf(stdout,"PASS actual Application/parser/main modeled scenario=%s radio=%d pairing=%d\n",s.c_str(),AVRCP_MEDIA_ENABLE_RADIO,AVRCP_MEDIA_ALLOW_PAIRING);
}
