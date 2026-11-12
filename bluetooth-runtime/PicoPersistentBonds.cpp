#include "PicoPersistentBonds.h"
#include "btstack_tlv.h"
#ifdef PICOW_COMMITTED_TLV_PROVIDER
#include "PicoCommittedTlv.h"
#endif
#include <cstring>
#include <limits>

namespace {
constexpr uint32_t TagBase = 0x50424b00u;
constexpr unsigned RecordSize = 48;
const btstack_tlv_t *tlv;
void *context;
bd_addr_t localAddress{};
bool opened;
PicoPersistentBonds::Status state{};
struct Record { uint64_t sequence; bd_addr_t address; link_key_t key; uint8_t type; };
uint32_t crc(const uint8_t *p, unsigned n) {
    uint32_t value = 0xffffffffu;
    for (unsigned i = 0; i < n; ++i) {
        value ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
    }
    return ~value;
}
void encode(const Record &r, unsigned slot, uint8_t *out) {
    std::memset(out, 0, RecordSize);
    out[0] = 'P'; out[1] = 'B'; out[2] = 'K'; out[3] = 1;
    for (unsigned i = 0; i < 8; ++i) out[4+i] = uint8_t(r.sequence >> (8*i));
    std::memcpy(out+12, localAddress, 6);
    std::memcpy(out+18, r.address, 6); std::memcpy(out+24, r.key, 16);
    out[40] = r.type; out[41] = static_cast<uint8_t>(slot);
    uint32_t sum = crc(out, 44);
    for (unsigned i = 0; i < 4; ++i) out[44+i] = uint8_t(sum >> (8*i));
}
int read(unsigned slot, Record &r) {
    if (!state.ready) return false;
#ifdef PICOW_COMMITTED_TLV_PROVIDER
    if (picow_committed_tlv_status().state != PICOW_TLV_READY) {
        state.ready = false; state.errors |= PicoPersistentBonds::Unavailable; return -1;
    }
#endif
    uint8_t bytes[RecordSize]{};
    int length = tlv->get_tag(context, TagBase + slot, nullptr, 0);
    if (length == 0) return false;
    if (length != RecordSize || tlv->get_tag(context, TagBase + slot, bytes, RecordSize) != RecordSize) {
        state.errors |= PicoPersistentBonds::InvalidRecord; return -1;
    }
    uint32_t sum = 0;
    for (unsigned i = 0; i < 4; ++i) sum |= uint32_t(bytes[44+i]) << (8*i);
    if (bytes[0]!='P' || bytes[1]!='B' || bytes[2]!='K' || bytes[3]!=1 || bytes[42]!=0 || bytes[43]!=0 || bytes[41]!=slot || bytes[40]>8 || sum != crc(bytes, 44)) {
        state.errors |= PicoPersistentBonds::InvalidRecord; return -1;
    }
    if (std::memcmp(bytes+12,localAddress,6)) { state.errors |= PicoPersistentBonds::ForeignController; return -2; }
    r.sequence = 0;
    for (unsigned i = 0; i < 8; ++i) r.sequence |= uint64_t(bytes[4+i]) << (8*i);
    if (!r.sequence) { state.errors |= PicoPersistentBonds::InvalidRecord; return -1; }
    std::memcpy(r.address, bytes+18, 6); std::memcpy(r.key, bytes+24, 16); r.type = bytes[40];
    return true;
}
void open() {
    btstack_tlv_get_instance(&tlv, &context); // After CYW43 setup_tlv, not before.
    state.ready = false;
    opened = tlv && tlv->get_tag && tlv->store_tag && tlv->delete_tag;
#ifdef PICOW_COMMITTED_TLV_PROVIDER
    opened = opened && picow_committed_tlv_status().state == PICOW_TLV_READY;
#endif
    std::memset(localAddress,0,6);
    if (!opened) state.errors |= PicoPersistentBonds::Unavailable;
}
void setAddress(bd_addr_t address) {
    if (!opened) return;
    if (state.ready && std::memcmp(localAddress,address,6)) {
        state.errors |= PicoPersistentBonds::ForeignController; state.ready=false; opened=false; return;
    }
    std::memcpy(localAddress,address,6); state.ready=true;
}
void close() { opened = false; state.ready = false; tlv = nullptr; context = nullptr; }
int get(bd_addr_t address, link_key_t key, link_key_type_t *type) {
    if (!state.ready) { state.errors |= PicoPersistentBonds::Unavailable; return 0; }
    for (unsigned i = 0; i < PicoPersistentBonds::Capacity; ++i) {
        Record r{};
        if (read(i, r)==1 && !std::memcmp(address, r.address, 6)) {
            std::memcpy(key, r.key, 16); *type = static_cast<link_key_type_t>(r.type); return 1;
        }
    }
    return 0;
}
void put(bd_addr_t address, link_key_t key, link_key_type_t type) {
    if (!state.ready) { state.errors |= PicoPersistentBonds::Unavailable; return; }
    if (static_cast<unsigned>(type)>8) { state.errors |= PicoPersistentBonds::InvalidKeyType; return; }
    unsigned chosen = PicoPersistentBonds::Capacity, empty = chosen, oldest = chosen;
    uint64_t highest = 0, lowest = std::numeric_limits<uint64_t>::max();
    for (unsigned i=0; i<PicoPersistentBonds::Capacity; ++i) {
        Record r{};
        int validity=read(i,r);
        if (validity<0) return; // Preserve corrupt/foreign records for explicit application recovery.
        if (!validity) { if (empty == PicoPersistentBonds::Capacity) empty=i; continue; }
        if (!std::memcmp(address, r.address, 6)) chosen=i;
        if (r.sequence>highest) highest=r.sequence;
        if (oldest==PicoPersistentBonds::Capacity || r.sequence<lowest) { lowest=r.sequence; oldest=i; }
    }
    if (highest==std::numeric_limits<uint64_t>::max()) { state.errors |= PicoPersistentBonds::SequenceExhausted; return; }
    bool eviction = chosen==PicoPersistentBonds::Capacity && empty==PicoPersistentBonds::Capacity;
    if (chosen==PicoPersistentBonds::Capacity) chosen = eviction ? oldest : empty;
    Record r{}; r.sequence=highest+1; r.type=static_cast<uint8_t>(type);
    std::memcpy(r.address,address,6); std::memcpy(r.key,key,16);
    uint8_t bytes[RecordSize]; encode(r,chosen,bytes);
    int result=tlv->store_tag(context,TagBase+chosen,bytes,RecordSize);
    uint8_t verify[RecordSize]{};
    bool verified = tlv->get_tag(context,TagBase+chosen,nullptr,0)==RecordSize &&
        tlv->get_tag(context,TagBase+chosen,verify,RecordSize)==RecordSize && !std::memcmp(bytes,verify,RecordSize);
    if (result || !verified) { state.errors |= PicoPersistentBonds::WriteFailed; return; }
    ++state.stores; if (eviction) ++state.evictions;
}
void remove(bd_addr_t address) {
    if (!state.ready) { state.errors |= PicoPersistentBonds::Unavailable; return; }
    for (unsigned i=0; i<PicoPersistentBonds::Capacity; ++i) {
        Record r{};
        if (read(i,r)!=1 || std::memcmp(address,r.address,6)) continue;
        tlv->delete_tag(context,TagBase+i);
        if (tlv->get_tag(context,TagBase+i,nullptr,0)!=0) state.errors |= PicoPersistentBonds::DeleteFailed;
        else ++state.deletes;
    }
}
int iteratorInit(btstack_link_key_iterator_t *it) { it->context=nullptr; return PicoPersistentBonds::status().ready ? 1 : 0; }
int iteratorNext(btstack_link_key_iterator_t *it, bd_addr_t address, link_key_t key, link_key_type_t *type) {
    uintptr_t index=reinterpret_cast<uintptr_t>(it->context);
    while (index<PicoPersistentBonds::Capacity) {
        Record r{};
        bool found=read(static_cast<unsigned>(index++),r)==1;
        it->context=reinterpret_cast<void *>(index);
        if (found) { std::memcpy(address,r.address,6); std::memcpy(key,r.key,16); *type=static_cast<link_key_type_t>(r.type); return 1; }
    }
    return 0;
}
void iteratorDone(btstack_link_key_iterator_t *) {}
const btstack_link_key_db_t table={open,setAddress,close,get,put,remove,iteratorInit,iteratorNext,iteratorDone};
}
const btstack_link_key_db_t *PicoPersistentBonds::database() { return &table; }
PicoPersistentBonds::Status PicoPersistentBonds::status() {
#ifdef PICOW_COMMITTED_TLV_PROVIDER
    if (state.ready && picow_committed_tlv_status().state != PICOW_TLV_READY) {
        state.ready = false; state.errors |= Unavailable;
    }
#endif
    return state;
}
void PicoPersistentBonds::clearErrors() { state.errors=0; }
