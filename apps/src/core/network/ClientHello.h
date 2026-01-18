#pragma once

#include <cstdint>
#include <zpp_bits.h>

namespace DirtSim {
namespace Network {

inline constexpr uint32_t kClientHelloProtocolVersion = 1;

struct ClientHello {
    uint32_t protocolVersion = kClientHelloProtocolVersion;
    bool wantsRender = false;
    bool wantsEvents = false;

    using serialize = zpp::bits::members<3>;
};

} // namespace Network
} // namespace DirtSim
