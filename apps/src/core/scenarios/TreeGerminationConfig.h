#pragma once

#include <cstdint>
#include <zpp_bits.h>

namespace DirtSim::Config {

enum class TreeBrainType : uint8_t {
    RULE_BASED = 0,
    NEURAL_NET = 1,
};

struct TreeGermination {
    TreeBrainType brain_type = TreeBrainType::RULE_BASED;
    uint32_t neural_seed = 42;

    using serialize = zpp::bits::members<2>;
};

} // namespace DirtSim::Config
