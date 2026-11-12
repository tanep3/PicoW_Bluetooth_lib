#include "AvrcpCoverArt.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace model { bool callback=false; unsigned mutations=0; }
// Linker wrappers catch callback printing, including printf optimized to puts.
extern "C" int __wrap_printf(const char *format, ...) {
  assert(!model::callback);
  va_list ap; va_start(ap,format); int r=std::vfprintf(stdout,format,ap); va_end(ap); return r;
}
extern "C" int __wrap_puts(const char *s) {
  assert(!model::callback); return std::fprintf(stdout,"%s\n",s);
}
#define main firmware_main_for_test
#include "examples/AvrcpMainLoopSample/main.cpp"
#undef main
using namespace picow::avrcp;
using avrcp_sample::FlowState;
namespace model {
Application *app=nullptr;
std::string scenario, input;
size_t at=0;
unsigned step=0, reads=0, signal_calls=0, queries=0, browse_calls=0, lists=0;
unsigned connects=0, gets=0, cancels=0, poweroffs=0;
Binding signal{peer,7,8,9}, browse{peer,7,18,19};
AvrcpCoverArt::Binding cover{};
AvrcpCoverArt::Operation chosen=AvrcpCoverArt::Operation::Image;
std::vector<std::string> stops;
void mutation() { assert(!callback); ++mutations; }
template<class F> void emit(F f) {
  assert(!callback); unsigned before=mutations;
  const auto state=app->art_flow.state(); const auto received=app->art_flow.object().received();
  callback=true; f(); callback=false;
  assert(app->art_flow.state()==state && app->art_flow.object().received()==received);
  assert(mutations==before); // Every modeled facade mutation and print is guarded.
}
void sdk() {
  uint8_t bytes[]={4,5,6};
  emit([&]{app->control.sdk({signal,bytes,3},app);});
  bytes[0]=99; // Poison the borrowed bytes after callback return.
  if(!app->queue.failed()) {
    auto copy=app->queue; Record record;
    while(copy.pop(record)) if(record.kind==Record::Kind::Sdk)
      assert(record.size==3 && record.bytes[0]==4);
  }
}
void artEvent(AvrcpCoverArt::Event event) {
  emit([&]{app->cover.cb(event,app);});
}
void change(AvrcpCoverArt::Binding &b, const std::string &field) {
  if(field=="peer") ++b.peer[5];
  else if(field=="epoch") ++b.epoch;
  else if(field=="generation") ++b.generation;
  else if(field=="cid") ++b.cid;
  else if(field=="avrcp_cid") ++b.avrcp_cid;
  else assert(false);
}
int get(const AvrcpCoverArt::Binding &b, AvrcpCoverArt::Operation op,const char *handle) {
  mutation(); ++gets;
  assert(!std::strcmp(handle,"1234567")); assert(op==chosen);
  assert(Application::artBinding(b)==Application::artBinding(cover));
  assert(app->queue.size()==0 && app->art_flow.state()==FlowState::Receiving);
  assert(app->art_flow.object().state()==avrcp_sample::ArtState::Armed);
  if(scenario=="get_reject") return -71;
  uint8_t bytes[]={11,22,33};
  AvrcpCoverArt::Data d{cover,41,0,op,bytes,3};
  emit([&]{assert(app->cover.sink(d,app));});
  bytes[0]=99;
  artEvent({AvrcpCoverArt::Kind::Complete,cover,scenario=="complete_error" ? -1:0,
            scenario=="complete_operation" ? AvrcpCoverArt::Operation::Thumbnail:op,
            scenario=="complete_request" ? 42u:41u,scenario=="complete_count" ? 4u:3u});
  assert(app->art_flow.object().state()==avrcp_sample::ArtState::Armed);
  return 0;
}
bool success() { return scenario=="image" || scenario=="thumbnail" || scenario=="properties"; }
void checkBeforeStop() {
  if(success()) {
    assert(app->art_flow.state()==FlowState::Complete);
    assert(app->art_flow.object().size()==3 && app->art_flow.object().data()[0]==11);
  } else if(scenario=="no_input" || scenario=="early_input") {
    assert(app->art_flow.state()==FlowState::Ready);
  } else {
    assert(app->art_flow.state()==FlowState::Failed);
    using avrcp_sample::FlowFailure;
    auto expected=FlowFailure::Session;
    if(scenario=="browse_reject") expected=FlowFailure::Browse;
    if(scenario=="connect_reject") expected=FlowFailure::Connect;
    if(scenario=="get_reject") expected=FlowFailure::Get;
    if(scenario.rfind("complete_",0)==0) expected=FlowFailure::Transfer;
    if(scenario=="cancel") expected=FlowFailure::Cancelled;
    assert(app->art_flow.failure()==expected);
    if(scenario=="get_reject") assert(app->art_flow.localStatus()==-71);
    if(scenario=="connect_reject") assert(app->art_flow.localStatus()==-73);
    assert(!app->art_flow.object().data() && !app->art_flow.object().size());
  }
  assert(connects<=1 && gets<=1 && cancels<=1);
}
}
void AvrcpTarget::poll() {
  using namespace model; mutation();
  if(!app) return;
  if(step==8 && scenario!="no_input" && scenario!="early_input") {
    input="art "+std::string(chosen==AvrcpCoverArt::Operation::Thumbnail ? "thumbnail":
           chosen==AvrcpCoverArt::Operation::Properties ? "properties":"image")+" 1234567\n"; at=0;
  }
  if(step==10 && scenario=="queue_loss") for(unsigned i=0;i<9;++i) sdk();
  if(step==11 && scenario.rfind("live_",0)==0) change(cover,scenario.substr(5));
  if(step==11 && scenario=="signal_stale") ++signal.generation;
  if(step==11 && scenario=="browse_stale") ++browse.generation;
  if(step==11 && scenario=="disconnected") {
    emit([&]{app->browsing.cb({AvrcpBrowsing::Kind::Disconnected,browse},app);});
  }
  if(step==12 && scenario=="cancel") { input="cancel\n";at=0; }
  if(step==18 && scenario!="no_input" && scenario!="early_input") { input="art image 1234567\n";at=0; } // No blind retry or second intent.
  if(step==29) checkBeforeStop();
}
int AvrcpController::connect(const Address &p) {
  using namespace model; mutation(); ++signal_calls; assert(p==peer);
  app=static_cast<Application*>(ctx);
  if(scenario=="early_input") {input="art image 1234567\n";at=0;}
  emit([&]{cb({EventKind::Connected,signal},ctx);}); return 0;
}
int AvrcpController::query(const Binding&,Query) {
  using namespace model; mutation(); ++queries; model::sdk();model::sdk();
  if(scenario=="query_reset") emit([&]{cb({EventKind::Reset,signal},ctx);});
  return 0;
}
Binding AvrcpController::bindingForPeer(const Address&) const {return model::signal;}
int AvrcpBrowsing::connect(const Address&) {
  using namespace model; mutation(); ++browse_calls;
  assert(queries==1 && app->queue.size()==0); // Both synchronous query packets were drained.
  if(scenario=="browse_reject") return -72;
  emit([&]{cb({Kind::Connected,browse},ctx);}); return 0;
}
int AvrcpBrowsing::folderItems(const Binding&,int,uint32_t,uint32_t) {
  using namespace model; mutation();++lists; uint8_t bytes[]={4};
  emit([&]{cb({Kind::Response,browse,0,bytes,1},ctx);});
  bytes[0]=99;
  auto copy=app->queue; Record record; bool found=false;
  while(copy.pop(record)) if(record.kind==Record::Kind::Browse) {
    found=true; assert(record.browse.bytes==nullptr && record.size==1 && record.bytes[0]==4);
  }
  assert(found); return 0;
}
Binding AvrcpBrowsing::bindingForPeer(const Address&) const {return model::browse;}
int AvrcpCoverArt::connect(const Address&) {
  using namespace model; mutation();++connects;
  assert(at==input.size() && input=="art "+std::string(chosen==Operation::Thumbnail ? "thumbnail":chosen==Operation::Properties ? "properties":"image")+" 1234567\n");
  assert(app->art_flow.state()==FlowState::Connecting && app->queue.size()==0);
  if(scenario=="connect_reject") return -73;
  cover={peer,7,28,29,9}; auto copied=cover;
  if(scenario.rfind("copied_",0)==0) change(copied,scenario.substr(7));
  artEvent({Kind::Connected,copied}); sdk(); // GET must wait for both copied records.
  assert(gets==0 && app->art_flow.state()==FlowState::Connecting); return 0;
}
AvrcpCoverArt::Binding AvrcpCoverArt::bindingForPeer(const Address&) const {return model::cover;}
int AvrcpCoverArt::thumbnail(const Binding& b,const char*h) {return model::get(b,Operation::Thumbnail,h);}
int AvrcpCoverArt::image(const Binding& b,const char*h,const char*) {return model::get(b,Operation::Image,h);}
int AvrcpCoverArt::properties(const Binding& b,const char*h) {return model::get(b,Operation::Properties,h);}
int AvrcpCoverArt::cancelPeer(const Address&) {model::mutation();++model::cancels;return -74;}
int AvrcpController::cancelConnection(const Address&) {model::mutation();return 0;}
void AvrcpCoverArt::stop() {model::mutation();model::stops.push_back("cover");}
void AvrcpBrowsing::stop() {model::mutation();model::stops.push_back("browse");}
void AvrcpController::stop() {model::mutation();model::stops.push_back("control");}
void AvrcpTarget::stop() {model::mutation();model::stops.push_back("target");}
int BluetoothRuntime::powerOff() {model::mutation();++model::poweroffs;return 0;}
int hci_get_state() {assert(!model::callback);return HCI_STATE_WORKING;}
void stdio_init_all() {}
uint32_t get_absolute_time() {return model::step>=30 ? 60000:model::step;}
int getchar_timeout_us(uint32_t t) {
  assert(!model::callback && t==0); ++model::reads;
  return model::at<model::input.size() ? model::input[model::at++]:PICO_ERROR_TIMEOUT;
}
void sleep_ms(uint32_t t) {assert(t==1 && model::reads<=16);model::reads=0;++model::step;}
int main(int argc,char **argv) {
  using namespace model; assert(argc==2);scenario=argv[1];
  if(scenario=="thumbnail") chosen=AvrcpCoverArt::Operation::Thumbnail;
  if(scenario=="properties") chosen=AvrcpCoverArt::Operation::Properties;
  int result=firmware_main_for_test();
  assert(result==(scenario=="queue_loss" ? 2:0));
  if(scenario=="queue_loss") assert(app->art_flow.failure()==avrcp_sample::FlowFailure::QueueLoss);
  assert(signal_calls==1 && queries==1);
  assert(browse_calls==(scenario=="query_reset" ? 0u:1u));
  bool no_connect=scenario=="query_reset" || scenario=="browse_reject" || scenario=="no_input" || scenario=="early_input";
  assert(connects==(no_connect ? 0u:1u));
  bool no_get=no_connect || scenario=="connect_reject" || scenario=="queue_loss" ||
    scenario.rfind("copied_",0)==0 || scenario.rfind("live_",0)==0 ||
    scenario=="signal_stale" || scenario=="browse_stale" || scenario=="disconnected";
  assert(gets==(no_get ? 0u:1u)); assert(cancels==connects);
  if(connects) assert(app->art_flow.cancelStatus()==-74);
  assert(poweroffs==1 && stops==std::vector<std::string>({"cover","browse","control","target"}));
  assert(app->art_flow.state()==FlowState::Stopped && !app->art_flow.object().data());
  std::printf("PASS actual main.cpp modeled facade scenario=%s connect=%u get=%u cancel=%u\n",scenario.c_str(),connects,gets,cancels);
}
