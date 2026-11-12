#include "AvrcpTarget.h"
#include "AvrcpShared.h"
#include "classic/avrcp.h"
#include "classic/avrcp_target.h"
#include <cstring>

#if !defined(HAVE_MALLOC) && (!defined(MAX_NR_AVRCP_CONNECTIONS) || MAX_NR_AVRCP_CONNECTIONS < 4)
#error "Two-peer AVRCP target requires MAX_NR_AVRCP_CONNECTIONS >= 4 (both SDK roles)"
#endif

namespace picow::avrcp {
AvrcpTarget* AvrcpTarget::active_ = nullptr;
AvrcpTarget::~AvrcpTarget() {
  stop();
  // SDK responses may retain pointers after stop until asynchronous cleanup.
  // Process-lifetime storage is intentionally retained until board reset.
  if (attempted_) info_.release();
  if (active_ == this) {
    AvrcpShared::instance().detach(AvrcpShared::Target);
    avrcp_target_register_packet_handler([](uint8_t,uint16_t,uint8_t*,uint16_t){});
    active_ = nullptr;
  }
}
bool AvrcpTarget::allowed(const Address& peer) const {
  for (unsigned i = 0; i < config_.peer_count; ++i)
    if (config_.peers[i] == peer) return true;
  return false;
}
bool AvrcpTarget::live(const Binding& binding) const {
  const auto link = BluetoothRuntime::instance().linkForPeer(binding.peer);
  return binding.cid && link.active && link.epoch == binding.epoch;
}
bool AvrcpTarget::begin(const Config& config, bool advertise_browsing) {
#ifndef PICOW_AVRCP_BROWSING
  if (advertise_browsing) return false;
#endif
  if (attempted_ || active_ || config.peer_count == 0 || config.peer_count > 2)
    return false;
  for (unsigned i = 0; i < config.peer_count; ++i) {
    if (config.peers[i] == Address{}) return false;
    for (unsigned j = 0; j < i; ++j)
      if (config.peers[i] == config.peers[j]) return false;
  }
  auto& runtime = BluetoothRuntime::instance();
  attempted_ = true;
  active_ = this;
  config_ = config;
  status_.last_error = AvrcpShared::instance().attach(AvrcpShared::Target, {
    avrcpPacket, hciPacket,
    [](const Address& p) { return active_ && active_->status_.started && !active_->status_.stopped && active_->allowed(p); },
    [](uint16_t cid) { return active_ && active_->core_.bindingForCid(cid).cid != 0; }
  });
  if (status_.last_error) { stop(); return false; }
  avrcp_target_register_packet_handler(avrcpPacket);
  sdp_handle_ = sdp_create_service_record_handle();
  uint16_t features = AVRCP_FEATURE_MASK_CATEGORY_PLAYER_OR_RECORDER |
      (advertise_browsing ? AVRCP_FEATURE_MASK_BROWSING : 0);
#ifdef PICOW_AVRCP_SDK_OVERLAY
  if (core_.supported(0x7e) && pass_handler_) features |= AVRCP_FEATURE_MASK_GROUP_NAVIGATION;
  for (auto value : player_settings_) if (value) { features |= AVRCP_FEATURE_MASK_PLAYER_APPLICATION_SETTINGS; break; }
#endif
  avrcp_target_create_sdp_record(sdp_, sdp_handle_,
                               features,
                               nullptr, nullptr);
  status_.last_error = sdp_register_service(sdp_);
  if (status_.last_error) { stop(); return false; }
  sdp_registered_ = true;
  status_.last_error = runtime.powerOn();
  status_.started = !status_.last_error;
  if (!status_.started) stop();
  return status_.started;
}
int AvrcpTarget::connect(const Address& peer) {
  if (requesting_ || !status_.started || status_.stopped || !allowed(peer) || core_.bindingForPeer(peer).cid)
    return ERROR_CODE_COMMAND_DISALLOWED;
  return AvrcpShared::instance().connect(peer);
}
int AvrcpTarget::cancelConnection(const Address &peer) {
  if (requesting_ || !status_.started || !allowed(peer)) return ERROR_CODE_COMMAND_DISALLOWED;
  return status_.last_error = AvrcpShared::instance().cancelConnection(peer);
}
bool AvrcpTarget::cancellationPending(const Address &peer) const {
  return allowed(peer) && AvrcpShared::instance().cancellationPending(peer);
}
int AvrcpTarget::cancellationStatus(const Address &peer) const {
  return allowed(peer) ? AvrcpShared::instance().cancellationStatus(peer) : ERROR_CODE_COMMAND_DISALLOWED;
}
int AvrcpTarget::disconnect(const Binding& binding) {
  if (requesting_ || !live(binding) || !(core_.bindingForCid(binding.cid) == binding))
    return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
  return avrcp_disconnect(binding.cid);
}
bool AvrcpTarget::setSupported(uint8_t operation, bool enabled) {
  if (attempted_) return false;
  return core_.setSupported(operation, enabled) == Result::Ok;
}
bool AvrcpTarget::setPlayback(uint32_t length, uint32_t position, uint8_t playback) {
  if (requesting_ || (playback > 4 && playback != 0xff)) return false;
  for (auto& info : *info_) { info.length = length; info.position = position; info.playback = playback; }
  return true;
}
int AvrcpTarget::validateBinding(const Binding& b) const {
  if (requesting_ || !status_.started || status_.stopped) return ERROR_CODE_COMMAND_DISALLOWED;
  return live(b) && core_.bindingForCid(b.cid) == b ? 0 : ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
}
int AvrcpTarget::mutableInfo(const Binding& b) const {
  const int result = validateBinding(b); if (result) return result;
  const auto *c = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_TARGET, b.cid);
  if (!c) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
  // SDK streams metadata from retained pointers, including between continuing
  // response requests. Never overwrite an in-flight response's storage.
  if (c->state != AVCTP_CONNECTION_OPENED || c->target_now_playing_info_response ||
      (c->pdu_id == AVRCP_PDU_ID_GET_ELEMENT_ATTRIBUTES && c->data_offset < c->data_len))
    return ERROR_CODE_COMMAND_DISALLOWED;
  return 0;
}
AvrcpTarget::OwnedInfo& AvrcpTarget::infoFor(const Binding& b) {
  return (*info_)[config_.peers[0] == b.peer ? 0 : 1];
}
int AvrcpTarget::recordResponse(int result) {
  status_.last_error = result; if (result) ++status_.response_errors; return result;
}
int AvrcpTarget::setMetadata(const Binding& b, const Metadata& m) {
  const int check = mutableInfo(b); if (check) return check;
  const char* values[]{m.title, m.artist, m.album, m.genre};
  size_t sizes[4]{};
  for (unsigned i = 0; i < 4; ++i) {
    if (!values[i]) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    while (sizes[i] < 256 && values[i][sizes[i]]) ++sizes[i];
    if (sizes[i] == 256) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  }
  auto& owned = infoFor(b);
  for (unsigned i = 0; i < 4; ++i) std::memcpy(owned.text[i].data(), values[i], sizes[i]+1);
  avrcp_track_t track{};
  std::memcpy(track.track_id, m.track_id.data(), 8);
  track.track_nr = m.track_number; track.song_length_ms = m.length_ms;
  track.title = owned.text[0].data(); track.artist = owned.text[1].data();
  track.album = owned.text[2].data(); track.genre = owned.text[3].data();
  const int result = avrcp_target_set_now_playing_info(b.cid, &track, m.total_tracks);
  if (!result) owned.length = m.length_ms;
  return recordResponse(result);
}
int AvrcpTarget::supportNotification(const Binding& b, uint8_t event) {
  const int check = mutableInfo(b); if (check) return check;
  // The pinned overlay implements six additional notification types.
  // Without it, advertise only the original SDK-supported subset.
  switch (event) {
    case AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED:
    case AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED:
    case AVRCP_NOTIFICATION_EVENT_BATT_STATUS_CHANGED:
    case AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED:
    case AVRCP_NOTIFICATION_EVENT_ADDRESSED_PLAYER_CHANGED:
    case AVRCP_NOTIFICATION_EVENT_UIDS_CHANGED:
    case AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED: break;
#ifdef PICOW_AVRCP_SDK_OVERLAY
    case AVRCP_NOTIFICATION_EVENT_TRACK_REACHED_START:
    case AVRCP_NOTIFICATION_EVENT_TRACK_REACHED_END:
    case AVRCP_NOTIFICATION_EVENT_PLAYBACK_POS_CHANGED:
    case AVRCP_NOTIFICATION_EVENT_SYSTEM_STATUS_CHANGED:
    case AVRCP_NOTIFICATION_EVENT_PLAYER_APPLICATION_SETTING_CHANGED:
    case AVRCP_NOTIFICATION_EVENT_AVAILABLE_PLAYERS_CHANGED: break;
#endif
    default: return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
  }
  return recordResponse(avrcp_target_support_event(b.cid, static_cast<avrcp_notification_event_id_t>(event)));
}
int AvrcpTarget::notifyPlayback(const Binding& b, uint8_t value) {
  const int check = validateBinding(b); if (check) return check;
  if (value > 4 && value != 0xff) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  const int result = avrcp_target_set_playback_status(b.cid, static_cast<avrcp_playback_status_t>(value));
  if (!result) infoFor(b).playback = value;
  return recordResponse(result);
}
int AvrcpTarget::notifyVolume(const Binding& b, uint8_t value) {
  const int check = validateBinding(b); if (check) return check;
  if (value > 127) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return recordResponse(avrcp_target_volume_changed(b.cid, value));
}
int AvrcpTarget::notifyTrack(const Binding& b, const std::array<uint8_t,8>& id) {
  const int check = mutableInfo(b); if (check) return check;
  auto copy = id; return recordResponse(avrcp_target_track_changed(b.cid, copy.data()));
}
int AvrcpTarget::notifyContentChanged(const Binding& b) {
  const int check = validateBinding(b); if (check) return check;
  return recordResponse(avrcp_target_playing_content_changed(b.cid));
}
int AvrcpTarget::notifyAddressedPlayer(const Binding& b, uint16_t player, uint16_t uid) {
  const int check = validateBinding(b); if (check) return check;
  // SDK's addressed-player helper ignores uid changes when player is unchanged.
  int result = avrcp_target_uids_changed(b.cid, uid);
  if (!result) result = avrcp_target_addressed_player_changed(b.cid, player, uid);
  return recordResponse(result);
}
int AvrcpTarget::notifyUids(const Binding& b, uint16_t uid) {
  const int check = validateBinding(b); if (check) return check;
  return recordResponse(avrcp_target_uids_changed(b.cid, uid));
}
int AvrcpTarget::notifyBattery(const Binding& b, uint8_t value) {
  const int check = validateBinding(b); if (check) return check;
  if (value > 4) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return recordResponse(avrcp_target_battery_status_changed(b.cid, static_cast<avrcp_battery_status_t>(value)));
}
int AvrcpTarget::setCompanies(const Binding& b, const uint32_t* values, size_t count) {
  const int check = mutableInfo(b); if (check) return check;
  if (!values || !count || count > 8) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  for (size_t i = 0; i < count; ++i) if (values[i] > 0xffffff) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  auto& owned = infoFor(b); std::memcpy(owned.companies.data(), values, count*sizeof(*values));
  return recordResponse(avrcp_target_support_companies(b.cid, uint8_t(count), owned.companies.data()));
}
int AvrcpTarget::setUnitInfo(const Binding& b, uint8_t type, uint32_t company) {
  const int check = mutableInfo(b); if (check) return check;
  if (type > 0x1f || company > 0xffffff) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  return recordResponse(avrcp_target_set_unit_info(b.cid, static_cast<avrcp_subunit_type_t>(type), company));
}
int AvrcpTarget::setSubunitInfo(const Binding& b, uint8_t type, const uint8_t* bytes, size_t size) {
  const int check = mutableInfo(b); if (check) return check;
  if (type > 0x1f || !bytes || !size || size > 32) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  auto& owned = infoFor(b); std::memcpy(owned.subunit.data(), bytes, size);
  return recordResponse(avrcp_target_set_subunit_info(b.cid, static_cast<avrcp_subunit_type_t>(type), owned.subunit.data(), uint16_t(size)));
}
namespace {
uint8_t settingMax(uint8_t id) { return id == 1 ? 2 : id == 2 ? 4 : id == 3 || id == 4 ? 3 : 0; }
uint32_t network32(const uint8_t* p) { return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3]; }
}
#ifdef PICOW_AVRCP_SDK_OVERLAY
extern "C" uint8_t picow_avrcp_target_notify(uint16_t, uint8_t, const uint8_t*, uint16_t);
#endif
bool AvrcpTarget::configurePlayerSetting(uint8_t id, uint8_t value) {
#ifndef PICOW_AVRCP_SDK_OVERLAY
  (void)id; (void)value; return false;
#else
  if (attempted_ || !settingMax(id) || value > settingMax(id)) return false;
  player_settings_[id-1] = value; return true;
#endif
}
bool AvrcpTarget::updatePlayerSetting(uint8_t id, uint8_t value) {
  if (requesting_ || status_.stopped || !settingMax(id) || !value || value > settingMax(id) || !player_settings_[id-1]) return false;
  player_settings_[id-1] = value; return true;
}
int AvrcpTarget::extensionNotify(const Binding& b, uint8_t event, const uint8_t* value, uint16_t n) {
  const int check = validateBinding(b); if (check) return check;
#ifdef PICOW_AVRCP_SDK_OVERLAY
  return recordResponse(picow_avrcp_target_notify(b.cid,event,value,n));
#else
  (void)event; (void)value; (void)n; return ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
#endif
}
int AvrcpTarget::notifyPlayerSettings(const Binding& b) {
  uint8_t bytes[9]{}; uint16_t size = 1;
  for (unsigned i=0;i<4;++i) if (player_settings_[i]) { ++bytes[0];bytes[size++]=uint8_t(i+1);bytes[size++]=player_settings_[i]; }
  if (!bytes[0]) return ERROR_CODE_COMMAND_DISALLOWED;
  return extensionNotify(b,AVRCP_NOTIFICATION_EVENT_PLAYER_APPLICATION_SETTING_CHANGED,bytes,size);
}
int AvrcpTarget::notifyPosition(const Binding& b, uint32_t value) {
  uint8_t bytes[]{uint8_t(value>>24),uint8_t(value>>16),uint8_t(value>>8),uint8_t(value)};
  const int result = extensionNotify(b,AVRCP_NOTIFICATION_EVENT_PLAYBACK_POS_CHANGED,bytes,4);
  if (!result) infoFor(b).position=value;
  return result;
}
uint32_t AvrcpTarget::positionNotificationInterval(const Binding& b) const {
  if (!live(b) || !(core_.bindingForCid(b.cid)==b)) return 0;
  return (*info_)[config_.peers[0]==b.peer?0:1].position_interval;
}
int AvrcpTarget::notifyTrackReachedStart(const Binding& b) { return extensionNotify(b,AVRCP_NOTIFICATION_EVENT_TRACK_REACHED_START,nullptr,0); }
int AvrcpTarget::notifyTrackReachedEnd(const Binding& b) { return extensionNotify(b,AVRCP_NOTIFICATION_EVENT_TRACK_REACHED_END,nullptr,0); }
int AvrcpTarget::notifySystemStatus(const Binding& b, uint8_t value) {
  if (value>2) return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  const int result=extensionNotify(b,AVRCP_NOTIFICATION_EVENT_SYSTEM_STATUS_CHANGED,&value,1);
  if (!result) infoFor(b).system_status=value;
  return result;
}
int AvrcpTarget::notifyAvailablePlayers(const Binding& b) { return extensionNotify(b,AVRCP_NOTIFICATION_EVENT_AVAILABLE_PLAYERS_CHANGED,nullptr,0); }
extern "C" uint8_t picow_avrcp_target_pass_through(uint16_t cid, uint8_t operation, bool pressed, const uint8_t* operands, uint16_t size) {
  return AvrcpTarget::handlePassThrough(cid, operation, pressed, operands, size);
}
uint8_t AvrcpTarget::handlePassThrough(uint16_t cid, uint8_t operation, bool pressed, const uint8_t* operands, uint16_t size) {
  constexpr auto reject = uint8_t(PassThroughDecision::Rejected);
  constexpr auto unsupported = uint8_t(PassThroughDecision::NotImplemented);
  if (!active_ || size > 5 || (size && !operands)) return reject;
  auto& self = *active_;
  const auto binding = self.core_.bindingForCid(cid);
  if (self.validateBinding(binding)) return reject;
  if (!self.core_.supported(operation)) return unsupported;
  const bool group = operation == 0x7e;
  uint8_t group_id = 0;
  auto& info = self.infoFor(binding);
  if (group) {
    if (size != 5) return reject;
    if (operands[0] != 0 || operands[1] != 0x19 || operands[2] != 0x58 || operands[3] != 0 || operands[4] > 1) return unsupported;
    group_id = operands[4];
    if (info.group_generation != binding.generation) {
      info.group_generation = binding.generation; info.group_pressed = {};
    }
    if (info.group_pressed[group_id] == pressed) return reject;
    if (!self.pass_handler_) return unsupported;
  } else {
    // This facade implements operand-free AV/C panel buttons only.
    if (size != 0) return reject;
    const auto valid = self.core_.validateOperation(binding, operation, pressed);
    if (valid != Result::Ok) return valid == Result::Unsupported ? unsupported : reject;
  }
  PassThroughRequest request{}; request.binding = binding; request.operation = operation;
  request.pressed = pressed; request.operand_count = uint8_t(size);
  if (size) std::memcpy(request.operands.data(), operands, size);
  self.requesting_ = true;
  auto decision = self.pass_handler_ ? self.pass_handler_(request, self.pass_context_) : PassThroughDecision::Accepted;
  if (decision == PassThroughDecision::Accepted) {
    if (group) info.group_pressed[group_id] = pressed;
    else if (self.core_.operation(binding, operation, pressed) != Result::Ok) decision = PassThroughDecision::Rejected;
  }
  self.requesting_ = false;
  if (decision != PassThroughDecision::Accepted && decision != PassThroughDecision::NotImplemented && decision != PassThroughDecision::Rejected) {
    ++self.status_.response_errors; return reject;
  }
  return uint8_t(decision);
}
extern "C" uint8_t picow_avrcp_target_extension(uint16_t cid,uint8_t pdu,const uint8_t* params,uint16_t size,uint8_t* out,uint16_t* out_size) {
  return AvrcpTarget::handleExtension(cid,pdu,params,size,out,out_size);
}
uint8_t AvrcpTarget::handleExtension(uint16_t cid,uint8_t pdu,const uint8_t* params,uint16_t size,uint8_t* out,uint16_t* out_size) {
  if (!active_ || !out_size || !out || (size && !params)) return AVRCP_STATUS_INVALID_PARAMETER;
  auto& self=*active_; const auto b=self.core_.bindingForCid(cid);
  if (self.validateBinding(b)) return AVRCP_STATUS_INVALID_COMMAND;
  const uint16_t capacity=*out_size; *out_size=0;
  auto put=[&](uint8_t value) { if (*out_size>=capacity) return false; out[(*out_size)++]=value; return true; };
  auto supported=[&](uint8_t id) { return settingMax(id) && self.player_settings_[id-1]; };
  auto decide=[&](Request& request) {
    Response response{}; self.requesting_=true;
    if (self.request_handler_) response=self.request_handler_(request,self.request_context_);
    self.requesting_=false;
    return response.status < AVRCP_STATUS_RESERVED && response.status != AVRCP_STATUS_RESERVED_6 ? response.status : uint8_t(AVRCP_STATUS_INVALID_PARAMETER);
  };
  if (pdu==AVRCP_PDU_ID_SET_ADDRESSED_PLAYER) {
    if (size!=2) return AVRCP_STATUS_INVALID_PARAMETER;
    Request request{};request.kind=Request::Kind::SetAddressedPlayer;request.binding=b;
    request.player_id=uint16_t(params[0])<<8|params[1]; return decide(request);
  }
  if (pdu==AVRCP_PDU_ID_REGISTER_NOTIFICATION) {
    if (size!=5) return AVRCP_STATUS_INVALID_PARAMETER;
    auto& info=self.infoFor(b);
    switch(params[0]) {
      case AVRCP_NOTIFICATION_EVENT_TRACK_REACHED_START:
      case AVRCP_NOTIFICATION_EVENT_TRACK_REACHED_END:
      case AVRCP_NOTIFICATION_EVENT_AVAILABLE_PLAYERS_CHANGED: return AVRCP_STATUS_SUCCESS;
      case AVRCP_NOTIFICATION_EVENT_PLAYBACK_POS_CHANGED: {
        const auto interval=network32(params+1); if (!interval) return AVRCP_STATUS_INVALID_PARAMETER;
        if (capacity<4) return AVRCP_STATUS_INTERNAL_ERROR;
        info.position_interval=interval;
        put(uint8_t(info.position>>24));put(uint8_t(info.position>>16));put(uint8_t(info.position>>8));put(uint8_t(info.position));return AVRCP_STATUS_SUCCESS;
      }
      case AVRCP_NOTIFICATION_EVENT_SYSTEM_STATUS_CHANGED:
        return put(info.system_status)?AVRCP_STATUS_SUCCESS:AVRCP_STATUS_INTERNAL_ERROR;
      case AVRCP_NOTIFICATION_EVENT_PLAYER_APPLICATION_SETTING_CHANGED:
        if (capacity<9) return AVRCP_STATUS_INTERNAL_ERROR;
        put(0);for(unsigned i=0;i<4;++i)if(self.player_settings_[i]){++out[0];put(uint8_t(i+1));put(self.player_settings_[i]);}
        return out[0]?AVRCP_STATUS_SUCCESS:AVRCP_STATUS_INVALID_PARAMETER;
      default:return AVRCP_STATUS_INVALID_PARAMETER;
    }
  }
  if(pdu==AVRCP_PDU_ID_LIST_PLAYER_APPLICATION_SETTING_ATTRIBUTES){
    if(size || capacity<5)return AVRCP_STATUS_INVALID_PARAMETER;
    put(0);for(unsigned i=0;i<4;++i)if(self.player_settings_[i]){++out[0];put(uint8_t(i+1));}
    return AVRCP_STATUS_SUCCESS;
  }
  if(pdu==AVRCP_PDU_ID_LIST_PLAYER_APPLICATION_SETTING_VALUES){
    if(size!=1 || !supported(params[0]) || capacity<5)return AVRCP_STATUS_INVALID_PARAMETER;
    put(settingMax(params[0]));for(uint8_t value=1;value<=settingMax(params[0]);++value)put(value);return AVRCP_STATUS_SUCCESS;
  }
  if(pdu==AVRCP_PDU_ID_GET_CURRENT_PLAYER_APPLICATION_SETTING_VALUE || pdu==AVRCP_PDU_ID_SET_PLAYER_APPLICATION_SETTING_VALUE){
    const bool set=pdu==AVRCP_PDU_ID_SET_PLAYER_APPLICATION_SETTING_VALUE;
    if(size<1 || !params[0] || params[0]>4 || size!=1+params[0]*(set?2:1) || capacity<9)return AVRCP_STATUS_INVALID_PARAMETER;
    Request request{};request.kind=Request::Kind::SetPlayerSettings;request.binding=b;request.setting_count=params[0];
    uint8_t seen=0;
    for(unsigned i=0;i<params[0];++i){
      const auto id=params[1+i*(set?2:1)];if(!supported(id) || (seen&(1u<<id)))return AVRCP_STATUS_INVALID_PARAMETER;
      seen|=1u<<id;
      request.setting_ids[i]=id;request.setting_values[i]=set?params[2+2*i]:self.player_settings_[id-1];
      if(!request.setting_values[i] || request.setting_values[i]>settingMax(id))return AVRCP_STATUS_INVALID_PARAMETER;
    }
    if(set){const auto status=decide(request);if(status!=AVRCP_STATUS_SUCCESS)return status;
      for(unsigned i=0;i<request.setting_count;++i)self.player_settings_[request.setting_ids[i]-1]=request.setting_values[i];
    }else{put(request.setting_count);for(unsigned i=0;i<request.setting_count;++i){put(request.setting_ids[i]);put(request.setting_values[i]);}}
    return AVRCP_STATUS_SUCCESS;
  }
  if(pdu==AVRCP_PDU_ID_GET_PLAYER_APPLICATION_SETTING_ATTRIBUTE_TEXT || pdu==AVRCP_PDU_ID_GET_PLAYER_APPLICATION_SETTING_VALUE_TEXT){
    const bool values=pdu==AVRCP_PDU_ID_GET_PLAYER_APPLICATION_SETTING_VALUE_TEXT;
    if(size<(values?2:1))return AVRCP_STATUS_INVALID_PARAMETER;
    const uint8_t attr=values?params[0]:0,count=params[values?1:0];
    if(!count || count>4 || size!=(values?2:1)+count || (values&&!supported(attr)))return AVRCP_STATUS_INVALID_PARAMETER;
    if(!put(count))return AVRCP_STATUS_INTERNAL_ERROR;
    const char* names[]{"Equalizer","Repeat","Shuffle","Scan"};
    const char* labels[][4]={{"Off","On","",""},{"Off","Single track","All tracks","Group"},{"Off","All tracks","Group",""},{"Off","All tracks","Group",""}};
    uint8_t seen=0;
    for(unsigned i=0;i<count;++i){const uint8_t id=params[(values?2:1)+i];
      if((values?(!id||id>settingMax(attr)):!supported(id)) || (seen&(1u<<id)))return AVRCP_STATUS_INVALID_PARAMETER;
      seen|=1u<<id;
      const char* text=values?labels[attr-1][id-1]:names[id-1];const auto n=std::strlen(text);
      if(size_t(capacity-*out_size)<4+n)return AVRCP_STATUS_INTERNAL_ERROR;
      put(id);put(0);put(106);put(uint8_t(n));for(size_t j=0;j<n;++j)put(uint8_t(text[j]));
    }
    return AVRCP_STATUS_SUCCESS;
  }
  return AVRCP_STATUS_INVALID_COMMAND;
}
void AvrcpTarget::poll() {
  if (requesting_) return;
  BluetoothRuntime::instance().poll();
  if (BluetoothRuntime::instance().failed() && !status_.stopped) {
    status_.last_error = -1;
    stop();
  }
}
void AvrcpTarget::stop() {
  if (requesting_ || !attempted_ || status_.stopped) return;
  status_.stopped = true;
  status_.started = false;
  if (sdp_registered_) {
    sdp_unregister_service(sdp_handle_);
    sdp_registered_ = false;
  }
  std::array<uint16_t, 2> closing{};
  for (unsigned i = 0; i < config_.peer_count; ++i)
    closing[i] = core_.bindingForPeer(config_.peers[i]).cid;
  core_.reset();
  for (auto cid : closing) if (cid) {
    const auto error = AvrcpShared::instance().disconnectUnlessShared(AvrcpShared::Target, cid);
    if (error) { status_.last_error = error; ++status_.connection_errors; }
  }
  // Keep SDK callbacks and SDP record storage alive until destruction/reset.

}
void AvrcpTarget::avrcpPacket(uint8_t type, uint16_t, uint8_t* packet, uint16_t size) {
  if (active_) active_->receive(type, packet, size);
}
void AvrcpTarget::hciPacket(uint8_t type, uint16_t, uint8_t* packet, uint16_t size) {
  if (!active_ || type != HCI_EVENT_PACKET || !packet || size < 2 ||
      unsigned(packet[1]) + 2 != size) return;
  auto& self = *active_;
  if (packet[0] == BTSTACK_EVENT_STATE && size >= 3 &&
      btstack_event_state_get_state(packet) == HCI_STATE_OFF) self.core_.reset();
  if (packet[0] == HCI_EVENT_DISCONNECTION_COMPLETE && size >= 6 &&
      !hci_event_disconnection_complete_get_status(packet)) {
    const auto link = BluetoothRuntime::instance().linkForHandle(
        hci_event_disconnection_complete_get_connection_handle(packet));
    const auto binding = self.core_.bindingForPeer(link.peer);
    if (binding.cid && binding.epoch == link.epoch) self.core_.disconnect(binding);
  }
}
void AvrcpTarget::receive(uint8_t type, uint8_t* packet, uint16_t size) {
  if (type != HCI_EVENT_PACKET) return;
  if (!packet || size < 3 || unsigned(packet[1]) + 2 != size) {
    ++status_.malformed; return;
  }
  if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
  switch (hci_event_avrcp_meta_get_subevent_code(packet)) {
  case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED: {
    if (size < 14) { ++status_.malformed; return; }
    const auto error = avrcp_subevent_connection_established_get_status(packet);
    if (error) { ++status_.connection_errors; status_.last_error = error; return; }
    Address peer{};
    avrcp_subevent_connection_established_get_bd_addr(packet, peer.data());
    const auto cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
    const auto link = BluetoothRuntime::instance().linkForPeer(peer);
    if (requesting_ || !status_.started || status_.stopped || !allowed(peer)) {
      if (!AvrcpShared::instance().accepted(peer)) avrcp_disconnect(cid);
      return;
    }
    if (!link.active ||
        link.handle != avrcp_subevent_connection_established_get_con_handle(packet) ||
        core_.connect({peer, link.epoch, cid}) != Result::Ok) {
      ++status_.connection_errors; avrcp_disconnect(cid); return;
    }
    break;
  }
  case AVRCP_SUBEVENT_CONNECTION_RELEASED: {
    if (size < 5) { ++status_.malformed; return; }
    const auto binding = core_.bindingForCid(
        avrcp_subevent_connection_released_get_avrcp_cid(packet));
    // stop(), HCI disconnect or an earlier release may already have retired it.
    // Do not publish an invalid application request for asynchronous SDK cleanup.
    if (binding.cid) core_.disconnect(binding);
    else ++status_.ignored;
    break;
  }
  case AVRCP_SUBEVENT_OPERATION: {
    // Installed Pico SDK emits nine bytes, including one operand placeholder.
    if (size < 9) { ++status_.malformed; return; }
    const auto cid = avrcp_subevent_operation_get_avrcp_cid(packet);
    const auto binding = core_.bindingForCid(cid);
    const auto pressed = avrcp_subevent_operation_get_button_pressed(packet);
    if (pressed > 1 || avrcp_subevent_operation_get_operands_length(packet) != 0) {
      ++status_.malformed; return;
    }
    if (status_.stopped || !live(binding)) { ++status_.ignored; return; }
#ifdef PICOW_AVRCP_SDK_OVERLAY
    // The overlay dispatches through the synchronous hook, never this SDK event.
    ++status_.ignored;
#else
    // BTstack has already queued the transaction-labelled pass-through response.
    // A second application ACK would corrupt/duplicate the pending response.
    core_.operation(binding, avrcp_subevent_operation_get_operation_id(packet), pressed != 0);
#endif
    break;
  }
  case AVRCP_SUBEVENT_PLAY_STATUS_QUERY: {
    if (size < 5) { ++status_.malformed; return; }
    const auto cid = avrcp_subevent_play_status_query_get_avrcp_cid(packet);
    if (!live(core_.bindingForCid(cid)) || status_.stopped) {
      AvrcpShared::instance().disconnectUnlessShared(AvrcpShared::Target, cid);
      return;
    }
    const auto& info = infoFor(core_.bindingForCid(cid));
    status_.last_error = avrcp_target_play_status(cid, info.length, info.position,
        static_cast<avrcp_playback_status_t>(info.playback));
    if (status_.last_error) ++status_.response_errors;
    break;
  }
  case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
  case AVRCP_SUBEVENT_PLAY_ITEM:
  case AVRCP_SUBEVENT_ADD_TO_NOW_PLAYING: {
    const auto kind = packet[2];
    const bool volume = kind == AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED;
    if (size != (volume ? 7 : 16)) { ++status_.malformed; return; }
    const auto binding = core_.bindingForCid(little_endian_read_16(packet, 3));
    if (status_.stopped || !live(binding)) { ++status_.ignored; return; }
    Request request{}; request.binding = binding;
    request.kind = volume ? Request::Kind::AbsoluteVolume :
        kind == AVRCP_SUBEVENT_PLAY_ITEM ? Request::Kind::PlayItem : Request::Kind::AddToNowPlaying;
    if (volume) {
      if (packet[5] != AVRCP_CTYPE_NOTIFY || packet[6] > 127) { ++status_.malformed; return; }
      request.volume = packet[6];
    } else {
      request.uid_counter = little_endian_read_16(packet,5); request.scope = packet[7];
      std::memcpy(request.item_id.data(), packet+8, 8);
    }
    Response response{}; response.volume = request.volume;
    requesting_ = true;
    if (request_handler_) response = request_handler_(request, request_context_);
    requesting_ = false;
    if (volume) {
      if (response.volume > 127) { ++status_.response_errors; response.volume = request.volume; }
      recordResponse(avrcp_target_adjust_absolute_volume(binding.cid, response.volume));
    } else {
      if (response.status >= AVRCP_STATUS_RESERVED || response.status == AVRCP_STATUS_RESERVED_6)
        response.status = AVRCP_STATUS_INVALID_PARAMETER;
      const auto result = static_cast<avrcp_status_code_t>(response.status);
      recordResponse(kind == AVRCP_SUBEVENT_PLAY_ITEM ?
        avrcp_target_send_response_for_play_item_cmd(binding.cid, result) :
        avrcp_target_send_response_for_add_to_now_playing_cmd(binding.cid, result));
    }
    break;
  }
  default: ++status_.ignored; break;
  }
}
}
