#include "A2dpSinkReceiver.h"
#include "BoundedPcmSink.h"
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "btstack.h"
#include "tusb.h"
#include <array>
#include <cassert>
#include <cstdio>

int sample_main();
namespace {
struct Done {};
A2dpSinkReceiver::State state;
IAudioSink *output;
unsigned polls=0, stdio_inits=0, commands=0;
using Fifo=sink_pcm_sample::BoundedPcmSink<8192>;
}
struct A2dpSinkReceiver::Impl {};
A2dpSinkReceiver::A2dpSinkReceiver(IAudioSink &sink,Settings) : impl_(new Impl) { output=&sink; }
A2dpSinkReceiver::~A2dpSinkReceiver()=default;
A2dpSinkReceiver::BeginResult A2dpSinkReceiver::begin() { state.working=true; return BeginResult::Ok; }
void A2dpSinkReceiver::poll() {
    assert(static_cast<Fifo*>(output)->queuedFrames()==0); // Previous burst fully drained.
    if(polls==5) throw Done{};
    pending_command=-1;
    if(polls==0) pending_command='r'; // Reject before stream begins.
    if(polls==1 || polls==2) {
        state.streaming=true;
        static std::array<int16_t,16384> pcm{};
        const unsigned frames=polls==1?512:8192;
        assert(output->write(pcm.data(),frames,44100,2)==frames);
        pending_command='r'; // First starts; second MUST be flushed while stdout disabled.
    }
    if(polls==3) state.streaming=false; // Finish early, still flush pending input.
    ++polls;
}
bool A2dpSinkReceiver::listen(){assert(false);return false;}
bool A2dpSinkReceiver::connect(){assert(false);return false;}
void A2dpSinkReceiver::disconnect(){assert(false);}
A2dpSinkReceiver::State A2dpSinkReceiver::snapshot()const{return state;}
A2dpSinkReceiver::Diagnostics A2dpSinkReceiver::diagnostics()const{return {};}
void stdio_init_all(){++stdio_inits;}
absolute_time_t get_absolute_time(){return polls*100;}
uint32_t to_ms_since_boot(absolute_time_t t){return t;}
int getchar_timeout_us(uint32_t){
    assert(console_enabled); // Busy input must use direct TinyUSB, not disabled stdio.
    ++commands;const auto value=pending_command;pending_command=-1;return value;
}
void tight_loop_contents(){}
void gap_discoverable_control(uint8_t){assert(false);}
void gap_local_bd_addr(bd_addr_t){assert(false);}
char *bd_addr_to_str(const bd_addr_t){assert(false);return nullptr;}
int main(){
    try { sample_main(); assert(false); } catch(const Done &){}
    assert(usb_inits==1 && usb_tasks>0 && stdio_inits==1);
    assert(rx_flushes==2 && commands==3 && console_enabled);
    unsigned begins=0;
    for(size_t i=0;i+36<=wire.size();) {
        auto get=[&](size_t offset){uint32_t v=0;for(unsigned j=0;j<4;++j)v|=uint32_t(wire[i+offset+j])<<(8*j);return v;};
        if(get(8)==1)++begins;
        i+=36+get(16);
    }
    assert(begins==1); // No deferred second recording after END.
    std::puts("PASS capture main: explicit gate, full 8192-frame drain, direct RX flush, no queued re-record");
}
