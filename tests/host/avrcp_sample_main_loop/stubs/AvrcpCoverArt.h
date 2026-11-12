#pragma once
#include "AvrcpBrowsing.h"
namespace picow::avrcp {
struct AvrcpCoverArt {
  struct Binding { Address peer{}; uint32_t epoch=0,generation=0; uint16_t cid=0,avrcp_cid=0; };
  enum class Operation : uint8_t { Thumbnail, Image, Properties };
  enum class Kind : uint8_t { Connected, Disconnected, Complete, Error };
  struct Data { Binding binding{}; uint32_t request=0,offset=0; Operation operation=Operation::Image; const uint8_t *bytes=nullptr; uint16_t size=0; };
  struct Event { Kind kind{}; Binding binding{}; int status=0; Operation operation=Operation::Image; uint32_t request=0,bytes=0; };
  void (*cb)(const Event&,void*)=nullptr; void *ctx=nullptr;
  bool (*sink)(const Data&,void*)=nullptr; void *sinkctx=nullptr;
  void setCallback(decltype(cb) f,void *p) { model::mutation(); cb=f;ctx=p; }
  void setDataHandler(decltype(sink) f,void *p) { model::mutation(); sink=f;sinkctx=p; }
  bool begin(const AvrcpTarget::Config&,uint32_t) { model::mutation(); return true; }
  bool setTimeouts(uint32_t,uint32_t) { model::mutation(); return true; }
  int connect(const Address&); Binding bindingForPeer(const Address&) const;
  int thumbnail(const Binding&,const char*); int image(const Binding&,const char*,const char* = nullptr);
  int properties(const Binding&,const char*); int cancelPeer(const Address&);
  void poll() { model::mutation(); } void poll(uint32_t) { model::mutation(); } void stop();
};
}
