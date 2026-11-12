#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace spp {
namespace detail {
struct SendAttempt {
  int32_t status;
  bool deferred;
};
// The callback is a readiness hint, not an exclusive reservation. Recheck at
// execution time, then keep buffer ownership until the prepared send returns.
// Port must expose the SDK's real send result, not a wrapper that masks it.
// Port contract: on error the caller still owns the reserved buffer; on success
// the SDK consumes/releases it. This is validated for this CYW43 polling/basic
// RFCOMM path. In an ERTM build, Port::reserve/release are no-ops because
// the SDK owns a dedicated RFCOMM scratch buffer and l2cap_send copies it.
// Arbitrary transports still need a separate audit. A reservation flag cannot
// identify ownership after a synchronous callback has reserved for another user.
template <typename Port>
SendAttempt sendPrepared(Port &port, uint16_t cid, const uint8_t *data,
                         uint16_t length) {
  if (!port.canSend(cid))
    return {0, true};
  port.reserve();
  std::memcpy(port.buffer(), data, length);
  const int32_t status = port.send(cid, length);
  if (status)
    port.release();
  return {status, status != 0 && port.transient(status)};
}
} // namespace detail
} // namespace spp
