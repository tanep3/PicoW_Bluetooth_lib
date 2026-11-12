#pragma once
#include <cstddef>
#include <cstdint>
namespace spp {
namespace detail {
inline bool eventLengthValid(const uint8_t *packet, size_t size,
                             uint8_t opened_event) {
  if (!packet || size < 2)
    return false;
  // BTstack 1.6.2 rfcomm_emit_channel_opened declares event[18] but calls
  // the handler with its actual populated length, 17. Only normalize that
  // exact SDK quirk; all accessed fields are present through offset 16.
  if (packet[0] == opened_event && packet[1] == 16 && size == 17)
    return true;
  return size >= size_t(packet[1]) + 2;
}
} // namespace detail
} // namespace spp
