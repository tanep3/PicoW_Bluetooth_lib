#include "sample_config.h"
#include "SppClient.h"
#include "FramedDuplex.h"
#include "pico/stdlib.h"
#include <cstdio>
using namespace framed_sample;
namespace {
constexpr spp::Address peer{{SPP_SAMPLE_PEER_BYTES}};
int run() {
  static spp::Client client(spp::Config{peer}); // authenticated, no auto-confirmation
  static Workflow<spp::Client> flow(client,SPP_SAMPLE_CONTROL_CHANNEL,SPP_SAMPLE_DATA_CHANNEL);
  stdio_init_all();
  std::printf("b begin; c/d connect control/data OR l/k listen control/data; t control; v data; x disconnect; u withdraw; s status\n");
  bool begin_queued=false;
  for (;;) {
    client.poll(); flow.observe();
    const int command=getchar_timeout_us(0);
    bool request=false, queued=false;
    const auto s=client.snapshot();
    switch(command) {
    case 'b': request=true; if(!begin_queued) { queued=client.begin(); begin_queued=queued; } break;
    case 'c': case 'd':
      request=true;
      if(s.runtime==spp::Runtime::Ready && flow.selectMode(Mode::Outgoing)) {
        const uint8_t slot=command=='c'?0:1;
        if(s.routes[slot].phase==spp::Phase::Closed)
          queued=client.connect(slot,peer,slot?SPP_SAMPLE_DATA_CHANNEL:SPP_SAMPLE_CONTROL_CHANNEL);
      } break;
    case 'l': case 'k':
      request=true;
      if(s.runtime==spp::Runtime::Ready && flow.selectMode(Mode::Incoming)) {
        const uint8_t endpoint=command=='l'?0:1;
        if(s.endpoints[endpoint].phase==spp::EndpointPhase::Disabled)
          queued=client.listen(endpoint,endpoint?SPP_SAMPLE_DATA_CHANNEL:SPP_SAMPLE_CONTROL_CHANNEL,spp::Uuid::from16(0x1101));
      } break;
    case 't': case 'v':
      std::printf("application enqueue=%u (not peer ACK)\n",flow.enqueue(command=='t'?Role::Control:Role::Data)); break;
    case 'x':
      flow.discardApplication();
      for(uint8_t i=0;i<spp::RouteCount;++i) if(s.routes[i].phase!=spp::Phase::Closed && s.routes[i].phase!=spp::Phase::Closing)
        std::printf("disconnect route=%u queued=%u\n",i,client.disconnect(spp::handleOf(s,i)));
      break;
    case 'u':
      for(uint8_t i=0;i<spp::EndpointCount;++i) if(s.endpoints[i].phase==spp::EndpointPhase::Listening)
        std::printf("unlisten endpoint=%u queued=%u\n",i,client.unlisten(i));
      break;
    case 's':
      std::printf("runtime=%u error=%u mode=%u app discarded TX/RX=%llu/%llu invalid=%llu\n",unsigned(s.runtime),unsigned(s.error),unsigned(flow.mode()),
        (unsigned long long)flow.discardedTx(),(unsigned long long)flow.discardedRx(),(unsigned long long)flow.invalidFrames());
      for(uint8_t i=0;i<spp::EndpointCount;++i)
        std::printf("endpoint=%u channel=%u phase=%u status=%ld\n",i,s.endpoints[i].channel,unsigned(s.endpoints[i].phase),(long)s.endpoints[i].status);
      for(uint8_t i=0;i<spp::RouteCount;++i) {
        const auto &r=flow.routes()[i]; const auto &actual=s.routes[i];
        std::printf("slot=%u channel=%u gen=%lu phase=%u error=%u role=%u invalid=%u pending=%u counts control/temp/vector=%lu/%lu/%lu values=%u/%u/(%u,%u,%u)\n",
          i,actual.channel,(unsigned long)actual.generation,unsigned(actual.phase),unsigned(actual.error),unsigned(r.role),r.invalid,unsigned(r.tx_size-r.tx_offset),
          (unsigned long)r.controls,(unsigned long)r.temperatures,(unsigned long)r.vectors,r.last_command,r.last_temperature,r.last_vector[0],r.last_vector[1],r.last_vector[2]);
      } break;
    default: break;
    }
    if(request) std::printf("request queued=%u; use s to observe completion\n",queued);
    flow.io(); sleep_ms(1);
  }
}
}
int main() { if(!SPP_SAMPLE_ENABLE_RADIO) return 0; return run(); }
