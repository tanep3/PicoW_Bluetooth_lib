#pragma once
#include "../../../spp-core/SppCore.h"
#include <vector>
namespace spp {
class Client {
public:
  IoResult next{};
  std::vector<uint8_t> accepted;
  IoResult tryWrite(Handle, const uint8_t* data, size_t size) {
    const auto count = std::min(next.bytes, size);
    accepted.insert(accepted.end(), data, data + count);
    return {count, next.error};
  }
};
}
