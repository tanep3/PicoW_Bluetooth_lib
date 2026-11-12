#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <string>
#include <functional>
namespace model {
inline bool callback=false, owned=false, signaling=false, active_link=false;
inline uint32_t ms=0; inline unsigned mutations=0, begins=0, controller_begins=0, presses=0, releases=0, queries=0, connects=0, disconnects=0, sink_disconnects=0, controller_disconnects=0, stops=0, poweroffs=0, listens=0, getters=0, watchdogs=0, stdios=0, sleeps=0, reads=0, policies=0;
inline int local_rc=0; inline bool fail_begin=false; inline std::string log,input; inline size_t input_at=0;
inline std::function<void()> on_poll,on_sleep,on_read;
inline void mutation() { assert(!callback); ++mutations; }
template<class F> void emit(F f) { assert(!callback); auto n=mutations; callback=true; f(); callback=false; assert(n==mutations); }
}
namespace picow::avrcp {
using Address=std::array<uint8_t,6>;
struct Binding { Address peer{}; uint32_t epoch=0,generation=0; uint16_t cid=0; friend bool operator==(const Binding&a,const Binding&b){return a.peer==b.peer&&a.epoch==b.epoch&&a.generation==b.generation&&a.cid==b.cid;} };
struct AvrcpController {
 enum class EventKind {Connected,Disconnected,PressResponse,ReleaseResponse,Timeout,CustomError,Reset};
 enum class Phase {Idle,Held,PressPending,ReleasePending}; enum class Query {PlayStatus};
 struct Event {EventKind kind{};Binding binding{};uint8_t operation=0,response=0;};
 struct SdkEvent {Binding binding{};const uint8_t*bytes=nullptr;uint16_t size=0;};
 struct Config {unsigned peer_count=0;std::array<Address,4> peers{};};
 struct Status {uint32_t connection_errors=0,malformed=0,ignored=0;};
 inline static AvrcpController *instance=nullptr;
 Binding live{}; Phase phase_=Phase::Idle; uint32_t timeout=0;
 void (*cb)(const Event&,void*)=nullptr;void*ctx=nullptr;
 void (*sdk)(const SdkEvent&,void*)=nullptr;void*sdkctx=nullptr;
 AvrcpController(){instance=this;}
 void setCallback(decltype(cb) f,void*p){model::mutation();cb=f;ctx=p;}
 void setSdkCallback(decltype(sdk) f,void*p){model::mutation();sdk=f;sdkctx=p;}
 void setCommandTimeout(uint32_t t){model::mutation();timeout=t;}
 bool begin(const Config&c){model::mutation();++model::controller_begins;assert(c.peer_count==1);return true;}
 Binding bindingForPeer(const Address&)const{return live;}
 Phase phase(const Binding&)const{return phase_;} Status status()const{return {};}
 int connect(const Address&){model::mutation();++model::connects;return model::local_rc;}
 int disconnect(const Binding&){model::mutation();++model::controller_disconnects;++model::disconnects;return model::local_rc;}
 int query(const Binding&,Query){model::mutation();++model::queries;return model::local_rc;}
 int press(const Binding&,uint8_t){model::mutation();++model::presses;phase_=Phase::PressPending;return model::local_rc;}
 int release(const Binding&){model::mutation();++model::releases;phase_=Phase::ReleasePending;return model::local_rc;}
 void stop(){model::mutation();++model::stops;live={};phase_=Phase::Idle;}
 void poll(uint32_t){model::mutation();if(model::on_poll)model::on_poll();}
};
}
struct IAudioSink {virtual ~IAudioSink()=default; virtual size_t write(const int16_t*,size_t,uint32_t,uint8_t)=0;virtual void reset()=0;};
struct A2dpSinkReceiver {
 struct Settings{picow::avrcp::Address peer;bool allow_pairing_confirmation;};enum class BeginResult{Ok,Error};
 struct Snapshot{bool signaling=false,streaming=false;};struct Diagnostics{uint64_t delivered_frames=0,dropped_frames=0,decode_failures=0;};
 A2dpSinkReceiver(IAudioSink&,Settings c){assert(!c.allow_pairing_confirmation); }
 BeginResult begin(){model::mutation();++model::begins;model::owned=!model::fail_begin;return model::fail_begin?BeginResult::Error:BeginResult::Ok;}
 bool listen(){model::mutation();++model::listens;return true;}
 void disconnect(){model::mutation();++model::sink_disconnects;++model::disconnects;}
 void poll(){model::mutation();}
 Snapshot snapshot()const{return {model::signaling,false};} Diagnostics diagnostics()const{return {};}
};
struct BluetoothRuntime {
 enum class Profile{A2dpSink,Avrcp};struct Link{bool active=false;};
 static BluetoothRuntime&instance(){static BluetoothRuntime r;return r;}
 bool ownsRadio()const{return model::owned;} Link linkForPeer(const picow::avrcp::Address&)const{return {model::active_link};}
 void setPairingPolicy(Profile,const picow::avrcp::Address&,bool){model::mutation();++model::policies;}
 int powerOff(){model::mutation();++model::poweroffs;model::owned=false;return 0;}
};
inline constexpr uint8_t HCI_EVENT_PACKET=4,HCI_EVENT_AVRCP_META=0xc0,AVRCP_SUBEVENT_PLAY_STATUS=0x16,AVRCP_CTYPE_RESPONSE_IMPLEMENTED_STABLE=12,BTSTACK_EVENT_STATE=0x60,HCI_STATE_WORKING=2,HCI_STATE_OFF=0,HCI_EVENT_CONNECTION_COMPLETE=3,HCI_EVENT_CONNECTION_REQUEST=4,HCI_EVENT_USER_CONFIRMATION_REQUEST=0x33,HCI_EVENT_PIN_CODE_REQUEST=0x16,HCI_EVENT_USER_PASSKEY_REQUEST=0x34;
struct btstack_packet_callback_registration_t{void(*callback)(uint8_t,uint16_t,uint8_t*,uint16_t)=nullptr;};
inline btstack_packet_callback_registration_t* guard=nullptr;
inline void hci_add_event_handler(btstack_packet_callback_registration_t*r){model::mutation();guard=r;}
inline uint8_t btstack_event_state_get_state(const uint8_t*p){return p[2];}
inline uint8_t hci_event_connection_complete_get_status(const uint8_t*p){return p[2];}
inline void hci_event_connection_complete_get_bd_addr(const uint8_t*p,uint8_t*a){for(int i=0;i<6;++i)a[i]=p[3+i];}
inline void hci_event_connection_request_get_bd_addr(const uint8_t*p,uint8_t*a){for(int i=0;i<6;++i)a[i]=p[2+i];}
inline void hci_event_user_confirmation_request_get_bd_addr(const uint8_t*p,uint8_t*a){for(int i=0;i<6;++i)a[i]=p[2+i];}
inline int hci_get_state(){assert(model::owned);++model::getters;return HCI_STATE_WORKING;}
inline void gap_discoverable_control(int){model::mutation();}inline void gap_connectable_control(int){model::mutation();}
inline void gap_set_local_name(const char*){model::mutation();}inline void gap_local_bd_addr(uint8_t*){assert(model::owned);}
inline const char*bd_addr_to_str(const uint8_t*){return "modeled-local";}
inline uint32_t little_endian_read_32(const uint8_t*p,int n){return uint32_t(p[n])|(uint32_t(p[n+1])<<8)|(uint32_t(p[n+2])<<16)|(uint32_t(p[n+3])<<24);}
