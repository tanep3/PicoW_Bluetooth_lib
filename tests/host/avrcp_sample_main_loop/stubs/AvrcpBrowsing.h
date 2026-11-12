#pragma once
// Minimal modeled public facade surface, NOT the SDK or a radio implementation.
#include <array>
#include <cstdint>
#include <cstddef>
namespace model { void mutation(); }
struct BluetoothRuntime {
  static BluetoothRuntime &instance() { static BluetoothRuntime r; return r; }
  int powerOff();
};
constexpr int HCI_STATE_WORKING = 2;
constexpr int AVRCP_BROWSING_MEDIA_PLAYER_LIST = 0;
int hci_get_state();
namespace picow::avrcp {
using Address = std::array<uint8_t,6>;
struct Binding { Address peer{}; uint32_t epoch=0, generation=0; uint16_t cid=0; };
struct AvrcpTarget {
  struct Config { unsigned peer_count=0; std::array<Address,1> peers{}; };
  struct PassThroughRequest {}; enum class PassThroughDecision { Rejected };
  struct Request {}; struct Response { int a,b; };
  template<class F> void setPassThroughHandler(F,void*) { model::mutation(); }
  template<class F> void setRequestHandler(F,void*) { model::mutation(); }
  void setPlayback(uint32_t,uint32_t,uint8_t) { model::mutation(); }
  bool begin(const Config&,bool) { model::mutation(); return true; }
  void poll(); void stop();
};
struct AvrcpController {
  enum class EventKind { Connected, Disconnected, Reset };
  enum class Query { PlayStatus };
  struct Event { EventKind kind{}; Binding binding{}; uint8_t response=0; };
  struct SdkEvent { Binding binding{}; const uint8_t *bytes=nullptr; uint16_t size=0; };
  void (*cb)(const Event&,void*)=nullptr; void *ctx=nullptr;
  void (*sdk)(const SdkEvent&,void*)=nullptr; void *sdkctx=nullptr;
  void setCallback(decltype(cb) f,void *p) { model::mutation(); cb=f;ctx=p; }
  void setSdkCallback(decltype(sdk) f,void *p) { model::mutation(); sdk=f;sdkctx=p; }
  bool begin(const AvrcpTarget::Config&,bool) { model::mutation(); return true; }
  bool setCommandTimeout(uint32_t) { model::mutation(); return true; }
  int connect(const Address&); int query(const Binding&,Query); int cancelConnection(const Address&);
  Binding bindingForPeer(const Address&) const;
  void poll() { model::mutation(); } void poll(uint32_t) { model::mutation(); } void stop();
};
struct AvrcpBrowsing {
  enum class Kind { Connected, Disconnected, Error, Response };
  struct Event { Kind kind{}; Binding binding{}; int status=0; const uint8_t *bytes=nullptr; uint16_t size=0; };
  struct RequestToken { uint32_t request=0; }; struct Request { RequestToken token{}; };
  void (*cb)(const Event&,void*)=nullptr; void *ctx=nullptr;
  void (*req)(const Request&,void*)=nullptr; void *reqctx=nullptr;
  void setCallback(decltype(cb) f,void *p) {model::mutation();cb=f;ctx=p;}
  void setRequestCallback(decltype(req) f,void *p) {model::mutation();req=f;reqctx=p;}
  bool begin(const AvrcpTarget::Config&,bool) { model::mutation(); return true; }
  bool setTimeouts(uint32_t,uint32_t,uint32_t,uint32_t) { model::mutation(); return true; }
  int connect(const Address&); int folderItems(const Binding&,int,uint32_t,uint32_t);
  int reject(const RequestToken&,uint8_t) { model::mutation(); return 0; }
  Binding bindingForPeer(const Address&) const;
  void poll() { model::mutation(); } void poll(uint32_t) { model::mutation(); } void stop();
};
}
