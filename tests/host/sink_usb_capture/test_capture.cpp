#include "UsbPcmCapture.h"
#include <array>
#include <cassert>
#include <cstdio>
static uint32_t u32(size_t at) {
    uint32_t value=0;
    for (unsigned i=0;i<4;++i) value |= uint32_t(wire.at(at+i)) << (8*i);
    return value;
}
static uint32_t crc(size_t at, size_t len) {
    uint32_t value=~0u;
    for(size_t i=at;i<at+len;++i) {
        value ^= wire[i];
        for(unsigned j=0;j<8;++j) value=(value>>1)^((value&1)?0xedb88320u:0);
    }
    return ~value;
}
static size_t end_packet() {
    size_t pos=0, end=0;
    while(pos<wire.size()) {
        assert(std::memcmp(wire.data()+pos,"PCMUSB01",8)==0);
        unsigned size=u32(pos+16);
        assert(size<=1024);
        assert(crc(pos,32+size)==u32(pos+32+size));
        if(u32(pos+8)==3) end=pos;
        pos += 36+size;
    }
    assert(pos==wire.size() && end);
    return end;
}
int main() {
    std::array<int16_t,1024> pcm{}; // Synthetic silence, no real recording.
    UsbPcmCapture capture;
    assert(!capture.start(0,2,1,0,0,0));
    assert(!capture.start(48001,2,1,0,0,0));
    assert(!capture.start(44100,3,1,0,0,0));
    assert(capture.start(44100,2,1,0,10,20));
    assert(!console_enabled && capture.busy());
    assert(!capture.start(44100,2,1,0,0,0));
    short_write=7; // Exercise arbitrary USB partial-write boundaries.
    for(unsigned frames=0;frames<220500;frames+=512) {
        capture.consume(pcm.data(),512,44100,2,1);
        capture.poll(frames/44,true,10,20);
    }
    capture.poll(6000,true,10,20);
    assert(!capture.busy() && console_enabled);
    auto e=end_packet();
    assert(u32(e+32)==220500 && u32(e+36)==0 && u32(e+40)==0 && u32(e+44)==0);
    assert(u32(e+48)==0 && u32(e+56)==220500);
    // Queue overflow is visible rather than corrupting packet contents.
    wire.clear(); connected=false;
    assert(capture.start(44100,2,2,0,0,0));
    for(unsigned i=0;i<100;++i) capture.consume(pcm.data(),512,44100,2,2);
    capture.poll(7001,true,5,3);
    connected=true; short_write=4096;
    for(unsigned i=0;i<20 && capture.busy();++i) capture.poll(7100+i,true,5,3);
    e=end_packet();
    assert(u32(e+36)>0 && u32(e+40)==5 && u32(e+44)==3 && u32(e+48)==2);
    // Empty reset/format boundary ends recording, even with no next PCM block.
    wire.clear(); assert(capture.start(48000,2,3,0,0,0));
    capture.consume(pcm.data(),0,0,0,4); capture.poll(1,true,0,0);
    e=end_packet(); assert(u32(e+48)==1);
    wire.clear(); assert(capture.start(48000,2,4,0,0,0));
    capture.poll(1,false,0,0); e=end_packet(); assert(u32(e+48)==3);
    // No USB host: always exit busy mode at the bounded deadline.
    wire.clear(); connected=false; assert(capture.start(48000,2,5,0,0,0));
    capture.poll(12001,true,0,0); assert(!capture.busy() && console_enabled);
    // Timer arithmetic survives wrap.
    connected=true; wire.clear(); assert(capture.start(48000,2,6,0xfffffff0u,0,0));
    capture.poll(20,true,0,0); assert(capture.busy());
    capture.poll(12001,true,0,0); assert(!capture.busy());
    std::puts("USB capture firmware host tests passed");
}
