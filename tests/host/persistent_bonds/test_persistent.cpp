#include "PicoPersistentBonds.h"
#include "btstack_tlv.h"
#include "btstack_tlv_flash_bank.h"
#include <array>
#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <algorithm>

extern "C" void btstack_assert_failed(const char *, int) { std::abort(); }
namespace {
constexpr size_t BankSize=512;
struct Media { std::array<std::array<uint8_t,BankSize>,2> banks; } media;
btstack_tlv_flash_bank_t journal;
const btstack_tlv_t *store;
const btstack_link_key_db_t *db=PicoPersistentBonds::database();
int cuts=-1, mutations=0, prefix=-1, reads=0; bool bounded=false;
std::vector<unsigned> operationSizes; bool drop=false, dropHeader=false;
std::jmp_buf reboot;
bd_addr_t local={1,2,3,4,5,6};
uint32_t size(void*) {return BankSize;}
uint32_t alignment(void*) {return 1;}
bool beforeMutation(unsigned length) { operationSizes.push_back(length); bool cut=cuts==mutations++; if (cut && prefix<0) std::longjmp(reboot,1); return cut; }
void erase(void*,int bank) {assert(bank>=0 && bank<2);bool cut=beforeMutation(BankSize);if(!drop)std::fill_n(media.banks[bank].begin(),cut?unsigned(prefix):BankSize,255);if(cut)std::longjmp(reboot,1);}
void read(void*,int bank,uint32_t offset,uint8_t *data,uint32_t length) {
    if (bounded && (++reads>10000 || bank<0 || bank>1 || offset>BankSize || length>BankSize-offset))std::longjmp(reboot,2);
    assert(bank>=0 && bank<2 && offset+length<=BankSize);std::memcpy(data,media.banks[bank].data()+offset,length);
}
void write(void*,int bank,uint32_t offset,const uint8_t *data,uint32_t length) {
    assert(bank>=0 && bank<2 && offset+length<=BankSize);bool cut=beforeMutation(length);
    if(drop || (dropHeader && offset==0))return;
    for(unsigned i=0;i<(cut?unsigned(prefix):length);++i) {assert((media.banks[bank][offset+i]&data[i])==data[i]);media.banks[bank][offset+i]&=data[i];}
    if(cut)std::longjmp(reboot,1);
}
const hal_flash_bank_t flash={size,alignment,erase,read,write};
void boot() {
    db->close(); store=btstack_tlv_flash_bank_init_instance(&journal,&flash,nullptr);
    btstack_tlv_set_instance(store,&journal);db->open();assert(!PicoPersistentBonds::status().ready);
    db->set_local_bd_addr(local);assert(PicoPersistentBonds::status().ready);
}
void fresh() {cuts=-1;drop=false;dropHeader=false;for(auto &b:media.banks)b.fill(255);boot();PicoPersistentBonds::clearErrors();}
void put(unsigned peer,unsigned value) {bd_addr_t a={};a[5]=peer;link_key_t k;std::memset(k,value,16);db->put_link_key(a,k,static_cast<link_key_type_t>(4));}
int get(unsigned peer) {bd_addr_t a={};a[5]=peer;link_key_t k={};link_key_type_t t{};if(!db->get_link_key(a,k,&t))return -1;for(auto b:k)assert(b==k[0]);assert(t==4);return k[0];}
void del(unsigned peer) {bd_addr_t a={};a[5]=peer;db->delete_link_key(a);}
unsigned count() {btstack_link_key_iterator_t it;assert(db->iterator_init(&it));bd_addr_t a;link_key_t k;link_key_type_t t;unsigned n=0;while(db->iterator_get_next(&it,a,k,&t)){++n;assert(n<=8);}db->iterator_done(&it);return n;}
std::map<uint32_t,std::vector<uint8_t>> records;
int fakeGet(void*,uint32_t tag,uint8_t *data,uint32_t length) {auto it=records.find(tag);if(it==records.end())return 0;if(!data)return it->second.size();unsigned n=std::min<unsigned>(length,it->second.size());std::memcpy(data,it->second.data(),n);return n;}
int fakeStore(void*,uint32_t tag,const uint8_t *data,uint32_t length){records[tag]=std::vector<uint8_t>(data,data+length);return 0;}
void fakeDelete(void*,uint32_t tag){records.erase(tag);}
const btstack_tlv_t fake={fakeGet,fakeStore,fakeDelete};
void fakeBoot(){db->close();btstack_tlv_set_instance(&fake,nullptr);db->open();db->set_local_bd_addr(local);PicoPersistentBonds::clearErrors();}
void integrity() {
    records.clear();fakeBoot();put(1,0x31);assert(get(1)==0x31);
    auto original=records;
    for(unsigned i=0;i<48;++i){records=original;records.begin()->second[i]^=1;assert(get(1)==-1);auto bad=records;put(2,0x32);assert(records==bad);}
    for(unsigned n:{1u,47u,49u}){records=original;records.begin()->second.resize(n);assert(get(1)==-1);auto bad=records;put(2,0x32);assert(records==bad);}
    records=original;auto value=records.begin()->second;records.clear();records[0x50424b01]=value;assert(get(1)==-1); // Slot transplant.
    records=original;bd_addr_t other={9,2,3,4,5,6};db->close();db->open();db->set_local_bd_addr(other);assert(get(1)==-1);put(2,0x32);assert(records==original);
    assert(PicoPersistentBonds::status().errors & PicoPersistentBonds::ForeignController);
    db->set_local_bd_addr(local);assert(!PicoPersistentBonds::status().ready);
    // No upstream singleton binding was needed; missing global TLV fails closed.
    db->close();btstack_tlv_set_instance(nullptr,nullptr);db->open();db->set_local_bd_addr(local);assert(!PicoPersistentBonds::status().ready);
}
void sweep(bool deletion,bool compact) {
    fresh();put(1,0x31);put(2,0x32);
    if(compact){while(journal.write_offset+56<=BankSize)put(1,0x31);put(1,0x31);while(journal.write_offset+56<=BankSize)put(1,0x31);}
    Media baseline=media;
    mutations=0;if(deletion)del(1);else put(1,0x41);int total=mutations;
    assert(total>0);if(compact)assert(total>3);
    for(int cut=0;cut<=total;++cut){
        media=baseline;cuts=-1;boot();mutations=0;cuts=cut;
        if(setjmp(reboot)==0){if(deletion)del(1);else put(1,0x41);}
        cuts=-1;boot();int v=get(1);
        assert(deletion?(v==-1 || v==0x31):(v==0x31 || v==0x41));assert(get(2)==0x32);
        assert(count()==(v==-1?1u:2u));
    }
    std::printf("PASS %s%s: %d interruption boundaries\n",compact?"compacting ":"",deletion?"delete":"store",total+1);
}

void partialSweep(bool deletion,bool compact) {
    fresh();put(1,0x31);put(2,0x32);
    if(compact){while(journal.write_offset+56<=BankSize)put(1,0x31);put(1,0x31);while(journal.write_offset+56<=BankSize)put(1,0x31);}
    Media baseline=media;operationSizes.clear();if(deletion)del(1);else put(1,0x41);
    auto sizes=operationSizes;unsigned cases=0,framingFailures=0,losses=0;
    for(unsigned operation=0;operation<sizes.size();++operation)for(unsigned n=0;n<=sizes[operation];++n){
        media=baseline;cuts=-1;prefix=-1;boot();mutations=0;cuts=operation;prefix=n;
        if(setjmp(reboot)==0){if(deletion)del(1);else put(1,0x41);}
        cuts=-1;prefix=-1;reads=0;bounded=true;
        int outcome=setjmp(reboot);
        if(outcome==0){
            boot();int a=get(1),b=get(2);
            assert(deletion?(a==-1||a==0x31):(a==-1||a==0x31||a==0x41));assert(b==-1||b==0x32);
            if(b==-1 || (!deletion && a==-1)){++losses;std::printf("LOSS %s%s operation=%u prefix=%u/%u prior_peer1=49 prior_peer2=50 recovered_peer1=%d recovered_peer2=%d\n",compact?"compacting ":"",deletion?"delete":"store",operation,n,sizes[operation],a,b);}
        }else {assert(outcome==2);++framingFailures;}
        bounded=false;++cases;
    }
    std::printf("PARTIAL %s%s: %u prefix cases, %u SDK framing/bounds failures, %u record-loss outcomes (not durability guarantees)\n",compact?"compacting ":"",deletion?"delete":"store",cases,framingFailures,losses);
}
}
int main() {
    integrity();std::puts("PASS exact length, all record-byte corruptions, slot/controller isolation, fail-closed writes");
    fresh();for(unsigned i=1;i<=8;++i)put(i,0x30+i);assert(count()==8);put(9,0x39);assert(get(1)==-1);assert(get(9)==0x39);boot();assert(count()==8);
    btstack_link_key_iterator_t it;assert(db->iterator_init(&it));bd_addr_t a;link_key_t k;link_key_type_t t;while(db->iterator_get_next(&it,a,k,&t))db->delete_link_key(a);db->iterator_done(&it);boot();assert(count()==0);
    std::puts("PASS capacity, deterministic eviction, reconstruction, delete during iteration");
    sweep(false,false);sweep(true,false);sweep(false,true);
    partialSweep(false,false);partialSweep(true,false);partialSweep(false,true);
    fresh();put(1,0x31);drop=true;put(1,0x41);assert(PicoPersistentBonds::status().errors&PicoPersistentBonds::WriteFailed);del(1);assert(PicoPersistentBonds::status().errors&PicoPersistentBonds::DeleteFailed);drop=false;
    fresh();put(1,0x31);while(journal.write_offset+56<=BankSize)put(1,0x31);
    dropHeader=true;put(1,0x41);assert(get(1)==0x41);assert(!(PicoPersistentBonds::status().errors&PicoPersistentBonds::WriteFailed));dropHeader=false;boot();assert(get(1)==0x31);
    std::puts("PASS silent write/delete failures; demonstrated readback is NOT reboot durability after dropped migration header");
}
