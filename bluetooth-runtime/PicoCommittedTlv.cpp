#include "PicoCommittedTlv.h"
#include <cstring>
#include <limits>
#ifndef PICOW_TLV_BANK_SIZE
#define PICOW_TLV_BANK_SIZE 4096
#endif
namespace {
constexpr unsigned Page = 256, Bank = PICOW_TLV_BANK_SIZE, Body = Bank - Page, Header = 32;
static_assert(Bank >= 4096 && Bank % 4096 == 0 && Bank <= 16384, "Unsupported snapshot bank size");
constexpr uint8_t Magic[8] = {'P','W','T','L','V','S','0','1'};
const hal_flash_bank_t *hal;
void *halContext;
uint8_t current[Body], scratch[Body];
unsigned used;
uint64_t generation;
int active = -1;
picow_tlv_status_t status{};
uint32_t u32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
uint64_t u64(const uint8_t *p) { return uint64_t(u32(p)) | uint64_t(u32(p+4))<<32; }
void put32(uint8_t *p, uint32_t n) { for (unsigned i=0;i<4;++i) p[i]=uint8_t(n>>(8*i)); }
void put64(uint8_t *p, uint64_t n) { put32(p,uint32_t(n)); put32(p+4,uint32_t(n>>32)); }
bool all(const uint8_t *p, unsigned n, uint8_t value) { for(unsigned i=0;i<n;++i) if(p[i]!=value) return false; return true; }
uint32_t crc(const uint8_t *p) {
    uint32_t c=0xffffffffu;
    for(unsigned i=0;i<Body;++i) {
        // Checksum field is represented as zero, includes padding and header.
        c ^= (i>=28 && i<32) ? 0 : p[i];
        for(unsigned b=0;b<8;++b) c=(c>>1) ^ (0xedb88320u & (0u-(c&1u)));
    }
    return ~c;
}
bool framing(const uint8_t *p, unsigned size) {
    if(size>Body-Header) return false;
    unsigned end=Header+size;
    for(unsigned at=Header;at<end;) {
        if(end-at<8) return false;
        unsigned length=u32(p+at+4);
        if(!length || length>end-at-8) return false;
        for(unsigned prior=Header;prior<at;prior+=8+u32(p+prior+4))
            if(u32(p+prior)==u32(p+at)) return false;
        at+=8+length;
    }
    return all(p+end,Body-end,0xff);
}
struct BankInfo { bool blank, valid; uint64_t seq; unsigned size; };
BankInfo inspect(int bank) {
    uint8_t commit[Page];
    hal->read(halContext,bank,0,scratch,Body);
    hal->read(halContext,bank,Body,commit,Page);
    BankInfo r{all(scratch,Body,0xff)&&all(commit,Page,0xff),false,0,0};
    if(!all(commit,Page,0) || std::memcmp(scratch,Magic,8) || u32(scratch+8)!=Bank || u32(scratch+12)!=1) return r;
    r.seq=u64(scratch+16); r.size=u32(scratch+24);
    r.valid=r.seq && crc(scratch)==u32(scratch+28) && framing(scratch,r.size);
    return r;
}
void reload() {
    BankInfo a=inspect(0), b=inspect(1);
    active=-1; used=0; generation=0; status.readable=0;
    if(a.valid && b.valid) {
        // Refuse equal, non-adjacent or wrapped generations; never guess.
        if(a.seq==b.seq || (a.seq>b.seq ? a.seq-b.seq : b.seq-a.seq)!=1) {
            status.state=PICOW_TLV_AMBIGUOUS; return;
        }
        active=a.seq>b.seq?0:1; status.state=PICOW_TLV_READY;
    } else if(a.valid || b.valid) {
        active=a.valid?0:1;
        status.state=(a.valid?b.blank:a.blank)?PICOW_TLV_READY:PICOW_TLV_READ_ONLY;
    } else {
        status.state=(a.blank && b.blank)?PICOW_TLV_BLANK:PICOW_TLV_READ_ONLY; return;
    }
    status.readable=1;
    const BankInfo &chosen=active==0?a:b;
    generation=chosen.seq; used=chosen.size;
    hal->read(halContext,active,0,current,Body);
    if(generation==std::numeric_limits<uint64_t>::max() && status.state==PICOW_TLV_READY) status.state=PICOW_TLV_EXHAUSTED;
}
bool blank(int bank) {
    uint8_t page[Page];
    for(unsigned at=0;at<Bank;at+=Page) { hal->read(halContext,bank,at,page,Page); if(!all(page,Page,0xff)) return false; }
    return true;
}
bool samePage(int bank,unsigned at,const uint8_t *expected) {
    uint8_t page[Page]; hal->read(halContext,bank,at,page,Page); return !std::memcmp(page,expected,Page);
}
// scratch contains a complete desired body; old active bank remains untouched.
bool publish(unsigned newUsed, bool provision) {
    const uint64_t next=generation+1;
    const int target=active<0?0:1-active;
    std::memcpy(scratch,Magic,8); put32(scratch+8,Bank); put32(scratch+12,1);
    put64(scratch+16,next); put32(scratch+24,newUsed); put32(scratch+28,0); put32(scratch+28,crc(scratch));
    if(!blank(target)) {
        if(provision) return false;
        hal->erase(halContext,target);
        if(!blank(target)) { ++status.write_failures; reload(); return false; }
    }
    for(unsigned at=0;at<Body;at+=Page) {
        hal->write(halContext,target,at,scratch+at,Page);
        if(!samePage(target,at,scratch+at)) { ++status.write_failures; reload(); return false; }
    }
    // A dedicated, previously erased physical page; no prior snapshot page is
    // reprogrammed by Pico HAL's full-page read/modify/program implementation.
    uint8_t commit[Page]{};
    hal->write(halContext,target,Body,commit,Page);
    // Same on-media validation and generation selection as reboot.
    reload();
    if(active!=target || generation!=next || (status.state!=PICOW_TLV_READY && status.state!=PICOW_TLV_EXHAUSTED)) { ++status.write_failures; return false; }
    return true;
}
int recoveryGet(uint32_t tag,uint8_t *buffer,uint32_t capacity) {
    if(active<0) return 0;
    for(unsigned at=Header;at<Header+used;at+=8+u32(current+at+4)) if(u32(current+at)==tag) {
        unsigned size=u32(current+at+4);
        if(!buffer) return int(size);
        unsigned n=capacity<size?capacity:size;
        std::memcpy(buffer,current+at+8,n); return int(n);
    }
    return 0;
}
int get(void *,uint32_t tag,uint8_t *buffer,uint32_t capacity) {
    // A validated older snapshot is not necessarily authoritative when another
    // bank is unknown/future/corrupt. Never expose it to normal BLE/Classic use.
    if(status.state!=PICOW_TLV_READY) return 0;
    return recoveryGet(tag,buffer,capacity);
}
int change(uint32_t tag,const uint8_t *data,unsigned length,bool deletion) {
    if(!hal) { ++status.write_failures; return 1; }
    reload();
    if(status.state!=PICOW_TLV_READY) { ++status.write_failures; return 1; }
    if(!deletion && (!data || !length || length>Body-Header-8)) { ++status.capacity_failures; return 2; }
    std::memset(scratch,0xff,Body);
    unsigned out=Header; bool found=false;
    for(unsigned at=Header;at<Header+used;at+=8+u32(current+at+4)) {
        unsigned n=8+u32(current+at+4);
        if(u32(current+at)==tag) { found=true; continue; }
        std::memcpy(scratch+out,current+at,n); out+=n;
    }
    if(deletion && !found) return 0;
    if(!deletion) {
        if(Body-out<8 || length>Body-out-8) { ++status.capacity_failures; return 2; }
        put32(scratch+out,tag); put32(scratch+out+4,length); std::memcpy(scratch+out+8,data,length); out+=8+length;
    }
    return publish(out-Header,false)?0:1;
}
int store(void *,uint32_t tag,const uint8_t *data,uint32_t size) { return change(tag,data,size,size==0); }
void remove(void *,uint32_t tag) { (void)change(tag,nullptr,0,true); }
const btstack_tlv_t table={get,store,remove};
}
extern "C" const btstack_tlv_t *picow_committed_tlv_init(const hal_flash_bank_t *impl,void *context) {
    hal=impl; halContext=context; active=-1; used=0; generation=0; status={PICOW_TLV_UNAVAILABLE,0,0,0};
    if(!hal || !hal->read || !hal->write || !hal->erase || !hal->get_size || !hal->get_alignment || hal->get_size(context)!=Bank || hal->get_alignment(context)!=1) { hal=nullptr; return &table; }
    reload(); return &table;
}
extern "C" int picow_committed_tlv_recovery_get(uint32_t tag,uint8_t *buffer,uint32_t capacity) { return recoveryGet(tag,buffer,capacity); }
extern "C" picow_tlv_status_t picow_committed_tlv_status() { return status; }
extern "C" int picow_committed_tlv_provision_blank() {
    if(!hal) return 1;
    reload(); if(status.state!=PICOW_TLV_BLANK) return 1;
    std::memset(scratch,0xff,Body); return publish(0,true)?0:1;
}
