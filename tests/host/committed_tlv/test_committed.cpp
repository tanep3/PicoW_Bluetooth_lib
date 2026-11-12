#include "PicoCommittedTlv.h"
#include "PicoPersistentBonds.h"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <limits>
using Bank=std::array<uint8_t,4096>;
using Media=std::array<Bank,2>;
Media media;
int operations=0, cut=-1, prefix=0, mode=0, eraseCalls=0;
// mode 0: power interruption; 1: silent operation drop/prefix; 2: strided damage.
struct Interrupted {};
uint32_t size(void *) { return 4096; }
uint32_t alignment(void *) { return 1; }
void read(void *,int bank,uint32_t at,uint8_t *out,uint32_t n) {
    assert(bank>=0 && bank<2 && at<=4096 && n<=4096-at);
    std::memcpy(out,media[bank].data()+at,n);
}
void mutate(int bank,unsigned at,const uint8_t *data,unsigned n,bool erase) {
    assert(bank>=0 && bank<2 && at<=4096 && n<=4096-at);
    bool fault=operations++==cut;
    unsigned count=fault?unsigned(prefix):n;
    for(unsigned i=0;i<count;++i) {
        unsigned j=(fault && mode==2)?(i*37)%n:i;
        if(erase) media[bank][at+j]=0xff;
        else media[bank][at+j]&=data[j];
    }
    if(fault && mode==0) throw Interrupted{};
}
void erase(void *,int bank) { ++eraseCalls; mutate(bank,0,nullptr,4096,true); }
void write(void *,int bank,uint32_t at,const uint8_t *data,uint32_t n) {
    // Mirrors actual physical page programming: only aligned whole pages.
    assert(at%256==0 && n==256); mutate(bank,at,data,n,false);
}
const hal_flash_bank_t hal={size,alignment,erase,read,write};
const btstack_tlv_t *tlv;
void reopen() { operations=0; eraseCalls=0; cut=-1; tlv=picow_committed_tlv_init(&hal,nullptr); btstack_tlv_set_instance(tlv,nullptr); assert(operations==0); }
void fresh() { for(auto &b:media) b.fill(0xff); reopen(); assert(picow_committed_tlv_status().state==PICOW_TLV_BLANK); assert(tlv->store_tag(nullptr,1,(const uint8_t *)"x",1)!=0); assert(operations==0); assert(picow_committed_tlv_provision_blank()==0); }
std::array<uint8_t,48> oldValue{},newValue{},neighbor{};
std::array<uint8_t,73> unrelated{};
int recoveryChecked(uint32_t tag,uint8_t *buffer,uint32_t size) {
    if(picow_committed_tlv_status().state==PICOW_TLV_READ_ONLY || picow_committed_tlv_status().state==PICOW_TLV_EXHAUSTED) {
        assert(tlv->get_tag(nullptr,tag,nullptr,0)==0);
        return picow_committed_tlv_recovery_get(tag,buffer,size);
    }
    return tlv->get_tag(nullptr,tag,buffer,size);
}
void value(uint32_t tag,const uint8_t *p,unsigned n) {
    uint8_t b[4000]{};
    assert(recoveryChecked(tag,nullptr,0)==int(n));
    assert(recoveryChecked(tag,b,sizeof b)==int(n));
    assert(!std::memcmp(b,p,n));
}
void seed() {
    fresh();
    assert(tlv->store_tag(nullptr,0x50424b00,oldValue.data(),48)==0);
    assert(tlv->store_tag(nullptr,0x50424b01,neighbor.data(),48)==0);
    assert(tlv->store_tag(nullptr,0xdeadbeef,unrelated.data(),73)==0);
    reopen();
}
int operation(int scenario) {
    if(scenario==0) return tlv->store_tag(nullptr,0x50424b00,newValue.data(),48);
    if(scenario==2) return tlv->store_tag(nullptr,0x50424b02,newValue.data(),48);
    unsigned failures=picow_committed_tlv_status().write_failures;
    tlv->delete_tag(nullptr,0x50424b00);
    return picow_committed_tlv_status().write_failures!=failures;
}
void committed(int scenario) {
    assert(picow_committed_tlv_status().state==PICOW_TLV_READY);
    if(scenario==0) value(0x50424b00,newValue.data(),48);
    if(scenario==1) assert(tlv->get_tag(nullptr,0x50424b00,nullptr,0)==0);
    if(scenario==2) value(0x50424b02,newValue.data(),48);
}
void verify(int scenario) {
    value(0x50424b01,neighbor.data(),48); value(0xdeadbeef,unrelated.data(),73);
    uint8_t b[48]{}; int n=recoveryChecked(0x50424b00,b,48);
    if(scenario==1) assert(n==0 || (n==48 && !std::memcmp(b,oldValue.data(),48)));
    else if(scenario==0) assert(n==48 && (!std::memcmp(b,oldValue.data(),48) || !std::memcmp(b,newValue.data(),48)));
    else { value(0x50424b00,oldValue.data(),48); n=recoveryChecked(0x50424b02,b,48); assert(n==0 || (n==48 && !std::memcmp(b,newValue.data(),48))); }
}
void matrix() {
    unsigned cases=0;
    for(int parity=0;parity<2;++parity) {
    seed();
    if(parity) assert(tlv->store_tag(nullptr,0xdeadbeef,unrelated.data(),73)==0);
    const Media baseline=media;
    for(int scenario=0;scenario<3;++scenario) {
        media=baseline; reopen(); assert(operation(scenario)==0); const int total=operations;
        assert(total==17); // 1 bank erase + 15 body pages + 1 commit page.
        for(int op=0;op<total;++op) {
            int bytes=op==0?4096:256;
            for(int n=0;n<=bytes;++n) {
                media=baseline; reopen(); cut=op;prefix=n;mode=0;
                int result=-1; try { result=operation(scenario); } catch(const Interrupted &) {}
                // Every mutation must leave the active bank byte-identical.
                assert(media[1-parity]==baseline[1-parity]);
                const Media after=media;
                reopen(); verify(scenario); if(result==0) committed(scenario); assert(media==after);
                reopen(); verify(scenario); if(result==0) committed(scenario); assert(media==after);
                if(picow_committed_tlv_status().state==PICOW_TLV_READ_ONLY) {
                    assert(tlv->store_tag(nullptr,99,newValue.data(),48)!=0); assert(media==after);
                }
                ++cases;
            }
            for(int variant=1;variant<=2;++variant) for(int n: {0,1,bytes/2,bytes-1,bytes}) {
                media=baseline; reopen();cut=op;prefix=n;mode=variant;int result=operation(scenario);
                assert(media[1-parity]==baseline[1-parity]);reopen();verify(scenario);if(result==0)committed(scenario);++cases;
            }
        }
    }
    }
    std::printf("snapshot update/delete/insert (both active-bank parities): %u interrupted/silent/strided physical-operation cases, zero prior-record losses\n",cases);
}
uint32_t crc(const Bank &b) {
    uint32_t c=0xffffffffu;for(unsigned i=0;i<3840;++i) {c^=(i>=28&&i<32)?0:b[i];for(int j=0;j<8;++j)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}return ~c;
}
void put32(Bank &b,unsigned at,uint32_t n) {for(unsigned i=0;i<4;++i)b[at+i]=uint8_t(n>>(8*i));}
void sequence(Bank &b,uint64_t n) {put32(b,16,uint32_t(n));put32(b,20,uint32_t(n>>32));put32(b,28,crc(b));}
void policies() {
    seed(); const Media baseline=media;
    // Nonempty legacy, unknown, future, corrupted, and mixed banks are preserved.
    for(int kind=0;kind<5;++kind) {
        media=baseline;
        if(kind==0) std::memcpy(media[0].data(),"BTstack",7);
        if(kind==1) media[0][0]=0x55;
        if(kind==2) { put32(media[0],12,2);put32(media[0],28,crc(media[0])); }
        if(kind==3) media[0][90]^=1;
        if(kind==4) media[0][4095]=0xff;
        const Media before=media;reopen();
        assert(picow_committed_tlv_status().state==PICOW_TLV_READ_ONLY && picow_committed_tlv_status().readable);
        value(0x50424b00,oldValue.data(),48);value(0xdeadbeef,unrelated.data(),73);
        assert(picow_committed_tlv_provision_blank()!=0);assert(tlv->store_tag(nullptr,99,newValue.data(),48)!=0);tlv->delete_tag(nullptr,0xdeadbeef);assert(media==before);
    }
    for(auto &b:media) { b.fill(0xff); } std::memcpy(media[0].data(),"BTstack",7);
    {const Media before=media;reopen();assert(!picow_committed_tlv_status().readable);assert(picow_committed_tlv_provision_blank()!=0);assert(media==before);}
    for(uint64_t seq: {uint64_t(4),uint64_t(99)}) {
        media=baseline;sequence(media[0],seq);const Media before=media;reopen();
        assert(picow_committed_tlv_status().state==PICOW_TLV_AMBIGUOUS);assert(!picow_committed_tlv_status().readable);assert(tlv->store_tag(nullptr,99,newValue.data(),48)!=0);assert(media==before);
    }
    media=baseline;sequence(media[0],std::numeric_limits<uint64_t>::max()-1);sequence(media[1],std::numeric_limits<uint64_t>::max());
    {const Media before=media;reopen();assert(picow_committed_tlv_status().state==PICOW_TLV_EXHAUSTED);assert(tlv->store_tag(nullptr,99,newValue.data(),48)!=0);assert(media==before);}
    fresh();std::array<uint8_t,3800> large{};assert(tlv->store_tag(nullptr,0,large.data(),large.size())==0);
    const Media full=media;assert(tlv->store_tag(nullptr,1,newValue.data(),48)!=0);assert(media==full);value(0,large.data(),large.size());
    tlv->delete_tag(nullptr,0);reopen();assert(tlv->get_tag(nullptr,0,nullptr,0)==0);
    fresh();assert(tlv->store_tag(nullptr,0xffffffffu,newValue.data(),48)==0);
    uint8_t shortRead[3]{};assert(tlv->get_tag(nullptr,0xffffffffu,shortRead,3)==3);assert(shortRead[2]==0x22);
    const Media valid=media;assert(tlv->store_tag(nullptr,42,nullptr,48)!=0);assert(tlv->store_tag(nullptr,42,newValue.data(),0xffffffffu)!=0);assert(media==valid);
    // Invalid structural lengths/duplicate tags with a recomputed CRC reject.
    seed();put32(media[1],36,0xffffffffu);put32(media[1],28,crc(media[1]));const Media corrupt=media;reopen();assert(picow_committed_tlv_status().state==PICOW_TLV_READ_ONLY);assert(media==corrupt);
    seed();put32(media[1],88,0x50424b00);put32(media[1],28,crc(media[1]));const Media duplicate=media;reopen();assert(picow_committed_tlv_status().state==PICOW_TLV_READ_ONLY);assert(media==duplicate);
    // A final representable generation commits successfully, then blocks wrap.
    seed();sequence(media[0],std::numeric_limits<uint64_t>::max()-2);sequence(media[1],std::numeric_limits<uint64_t>::max()-1);reopen();
    assert(tlv->store_tag(nullptr,0x50424b00,newValue.data(),48)==0);reopen();value(0x50424b00,newValue.data(),48);assert(picow_committed_tlv_status().state==PICOW_TLV_EXHAUSTED);
    std::puts("policy: blank explicit-only; legacy/unknown/future/corrupt/mixed preservation; ambiguous/wrap/capacity/deletion checks passed");
}
void adapter() {
    fresh();PicoPersistentBonds::clearErrors();unsigned evictions=PicoPersistentBonds::status().evictions;const auto *db=PicoPersistentBonds::database();db->open();bd_addr_t controller={1,2,3,4,5,6};db->set_local_bd_addr(controller);
    for(int i=0;i<9;++i) {bd_addr_t peer={0,0,0,0,0,uint8_t(i+1)};link_key_t key{};key[0]=uint8_t(i+1);db->put_link_key(peer,key,static_cast<link_key_type_t>(4));}
    assert(PicoPersistentBonds::status().evictions==evictions+1);assert(PicoPersistentBonds::status().errors==0);
    reopen();db->open();db->set_local_bd_addr(controller);
    for(int i=0;i<9;++i) {bd_addr_t peer={0,0,0,0,0,uint8_t(i+1)};link_key_t key{};link_key_type_t type{};assert(db->get_link_key(peer,key,&type)==(i?1:0));if(i)assert(key[0]==i+1);}
    std::puts("real bond adapter: 8-record capacity, ninth-record eviction and reopen passed (synthetic records only)");
}
void authenticationPolicy() {
    fresh();const auto *db=PicoPersistentBonds::database();bd_addr_t controller={1,2,3,4,5,6};bd_addr_t peer={0,0,0,0,0,7};link_key_t key{};key[0]=7;link_key_type_t type{};
    db->open();db->set_local_bd_addr(controller);db->put_link_key(peer,key,static_cast<link_key_type_t>(4));
    assert(db->get_link_key(peer,key,&type)==1);
    // Active bank 1 is valid; other bank now advertises an unknown future format.
    put32(media[0],12,2);put32(media[0],28,crc(media[0]));const Media before=media;
    reopen(); // Deliberately do NOT reopen adapter: invalidate its cached READY.
    assert(picow_committed_tlv_status().state==PICOW_TLV_READ_ONLY);
    assert(db->get_link_key(peer,key,&type)==0);
    btstack_link_key_iterator_t it{}; (void)db->iterator_init(&it);assert(db->iterator_get_next(&it,peer,key,&type)==0);
    assert(!PicoPersistentBonds::status().ready);
    assert(tlv->get_tag(nullptr,0x50424b00,nullptr,0)==0);
    assert(picow_committed_tlv_recovery_get(0x50424b00,nullptr,0)==48);
    db->open();db->set_local_bd_addr(controller);assert(db->get_link_key(peer,key,&type)==0);assert(!PicoPersistentBonds::status().ready);assert(media==before);
    std::puts("authentication policy: mixed/future state blocks normal generic/Classic reads and iteration, including after-open transition; explicit recovery retains serialized record");
}
void provisioning() {
    unsigned cases=0;
    for(int op=0;op<16;++op) for(int n=0;n<=256;++n) {
        for(auto &b:media)b.fill(0xff);
        reopen();cut=op;prefix=n;mode=0;
        try { (void)picow_committed_tlv_provision_blank(); } catch(const Interrupted &) {}
        assert(eraseCalls==0);
        Bank empty;empty.fill(0xff);assert(media[1]==empty);
        const Media after=media;reopen();assert(media==after);
        if(picow_committed_tlv_status().state!=PICOW_TLV_BLANK) {assert(picow_committed_tlv_provision_blank()!=0);assert(media==after);}
        ++cases;
    }
    std::printf("explicit blank provisioning: %u page-prefix cases, no erase and no automatic resume\n",cases);
}
void evictionFaults() {
    fresh();const auto *db=PicoPersistentBonds::database();bd_addr_t controller={1,2,3,4,5,6};
    db->open();db->set_local_bd_addr(controller);
    for(int i=1;i<=8;++i) {bd_addr_t p={0,0,0,0,0,uint8_t(i)};link_key_t k{};k[0]=uint8_t(i);db->put_link_key(p,k,static_cast<link_key_type_t>(4));}
    assert(tlv->store_tag(nullptr,0xdeadbeef,unrelated.data(),73)==0);
    const Media baseline=media;unsigned cases=0;
    std::array<std::array<uint8_t,48>,8> originalRecords{};
    for(unsigned slot=0;slot<8;++slot) assert(tlv->get_tag(nullptr,0x50424b00+slot,originalRecords[slot].data(),48)==48);
    bd_addr_t ninth={0,0,0,0,0,9};link_key_t ninthKey{};ninthKey[0]=9;
    db->put_link_key(ninth,ninthKey,static_cast<link_key_type_t>(4));
    std::array<uint8_t,48> ninthRecord{};assert(tlv->get_tag(nullptr,0x50424b00,ninthRecord.data(),48)==48);
    for(int op=0;op<17;++op) for(int n=0;n<=(op==0?4096:256);++n) {
        media=baseline;reopen();db->open();db->set_local_bd_addr(controller);PicoPersistentBonds::clearErrors();
        unsigned before=PicoPersistentBonds::status().stores;
        cut=op;prefix=n;mode=0;bd_addr_t p={0,0,0,0,0,9};link_key_t k{};k[0]=9;
        try { db->put_link_key(p,k,static_cast<link_key_type_t>(4)); } catch(const Interrupted &) {}
        bool success=PicoPersistentBonds::status().stores!=before;
        assert(media[1]==baseline[1]);
        const Media after=media;reopen();db->open();db->set_local_bd_addr(controller);
        int first=0,last=0;
        for(int i=1;i<=9;++i) {bd_addr_t peer={0,0,0,0,0,uint8_t(i)};link_key_t key{};link_key_type_t type{};int present=db->get_link_key(peer,key,&type);
            int recovered=0;
            for(unsigned slot=0;slot<8;++slot) {
                uint8_t record[48]{};
                if(picow_committed_tlv_recovery_get(0x50424b00+slot,record,48)==48 && record[23]==i) {
                    const uint8_t *expected=i==9?ninthRecord.data():originalRecords[i-1].data();
                    assert(!std::memcmp(record,expected,48));recovered=1;
                }
            }
            if(picow_committed_tlv_status().state==PICOW_TLV_READ_ONLY) {
                assert(present==0); // Preserved bytes do not authorize authentication.
            } else {
                assert(present==recovered);if(present)assert(key[0]==i);
            }
            present=recovered;
            if(i==1)first=present;else if(i==9)last=present;else assert(present==1);}
        assert(first+last==1);if(success)assert(last==1);
        value(0xdeadbeef,unrelated.data(),73);assert(media==after);++cases;
    }
    std::printf("actual bond adapter 8-to-9 eviction: %u page/erase-prefix cases, seven unchanged peers plus unrelated tag retained\n",cases);
}
int main() {oldValue.fill(0x11);newValue.fill(0x22);neighbor.fill(0x33);unrelated.fill(0x44);matrix();policies();authenticationPolicy();provisioning();evictionFaults();adapter();}
