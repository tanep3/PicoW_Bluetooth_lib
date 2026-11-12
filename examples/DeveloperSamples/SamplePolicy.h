#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace sample {
using Address = std::array<uint8_t, 6>;
inline int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
inline bool parsePeer(const char* text, Address& out) {
  out = {};
  if (!text || std::strlen(text) != 17) return false;
  Address parsed{};
  for (unsigned i = 0; i < 6; ++i) {
    const int a = hex(text[3*i]), b = hex(text[3*i+1]);
    if (a < 0 || b < 0 || (i < 5 && text[3*i+2] != ':')) return false;
    parsed[i] = uint8_t(a*16+b);
  }
  if (parsed == Address{} || parsed == Address{255,255,255,255,255,255}) return false;
  out = parsed;
  return true;
}
inline bool liveAclUsable(bool active, uint32_t epoch, uint16_t handle) {
  return active && epoch != 0 && handle <= 0x0eff; // HCI handle 0 is valid.
}
struct PairingWindow {
  bool active = false;
  uint64_t until = 0;
  bool start(uint64_t now, bool opt_in, bool working, bool stopped) {
    if (active || !opt_in || !working || stopped) return false;
    active = true;
    until = now + 60000000;
    return true;
  }
  bool shouldClose(uint64_t now, bool connected) const {
    return active && (connected || now >= until);
  }
  void close() { active = false; }
};
// A maximum of three queued attempts per boot, with a five-second cooldown.
// Queue rejection consumes an attempt too; no tight-loop retry.
struct RetryBudget {
  unsigned used = 0;
  uint64_t next = 0;
  bool take(uint64_t now) {
    if (used == 3 || now < next) return false;
    ++used;
    next = now + 5000000;
    return true;
  }
  void closed(uint64_t now) { next = now + 5000000; }
};
}
