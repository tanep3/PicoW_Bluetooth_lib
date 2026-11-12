#pragma once
#include "btstack.h"
#include <array>
#include <cstdint>
// Process-lifetime, single-main-loop owner of radio, shared HCI facts and SDP.
// Per-profile callbacks/storage must outlive this runtime.
class BluetoothRuntime {
public:
  enum class Profile : uint8_t { A2dp, Spp, Avrcp, A2dpSink };
  static constexpr unsigned ProfileCount = 4;
  using Address = std::array<uint8_t, 6>;
  struct Link {
    Address peer{};
    uint32_t epoch = 0;
    uint16_t handle = 0;
    bool active = false;
  };
  static BluetoothRuntime &instance();
  // Call before any successful profile claim or initialize(). nullptr selects
  // the default RAM-only link-key database. A custom table must remain valid
  // and unchanged, and its state valid, for this process-lifetime runtime.
  // No callback is invoked here; BTstack invokes open() after CYW43 init.
  // Requires all six management/key callbacks, and all or none of the three
  // iterator callbacks. Failure leaves the previous selection unchanged.
  bool setLinkKeyDatabase(const btstack_link_key_db_t *database);
  bool claim(Profile profile);
  int initialize();
  int powerOn();
  int powerOff();
  void poll();
  bool ownsRadio() const { return initialized_; }
  // Terminal until reset; never reset SDK-private SDP client state.
  bool failed() const { return failed_; }
  void setHandler(Profile, btstack_packet_handler_t);
  void setPairingPolicy(Profile, Address peer, bool allow_confirmation);
  void confirmPairing(const Address &peer);
  void setSecurityLevel(Profile, uint8_t level);
  Link linkForPeer(const Address &) const;
  Link linkForHandle(uint16_t) const;
  int startSdp(uint32_t token, const Address &,
               const std::array<uint8_t, 16> &uuid,
               btstack_packet_handler_t callback);
  void cancelSdp(uint32_t token);
  bool sdpOwned() const { return sdp_token_ != 0; }

private:
  static void hciPacket(uint8_t, uint16_t, uint8_t *, uint16_t);
  static void sdpPacket(uint8_t, uint16_t, uint8_t *, uint16_t);
  bool claimed_[ProfileCount]{};
  bool initialized_ = false, failed_ = false, powered_ = false;
  const btstack_link_key_db_t *link_key_db_ = nullptr;
  bool link_key_db_locked_ = false;
  std::array<btstack_packet_handler_t, ProfileCount> handlers_{};
  btstack_packet_callback_registration_t registration_{};
  std::array<Link, 4> links_{};
  uint32_t epoch_ = 0, sdp_token_ = 0;
  btstack_packet_handler_t sdp_callback_ = nullptr;
  bool sdp_cancelled_ = false;
  std::array<Address, 4> confirmations_{};
  uint8_t confirmation_count_ = 0;
  std::array<Address, ProfileCount> pairing_peers_{};
  std::array<bool, ProfileCount> allow_confirmation_{};
  std::array<uint8_t, ProfileCount> security_{{2, 2, 2, 2}};
};
