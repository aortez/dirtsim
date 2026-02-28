#pragma once

#include "core/UUID.h"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <zpp_bits.h>

namespace DirtSim::Config {

enum class TreeBrainType : uint8_t {
    RULE_BASED = 0,
    NEURAL_NET = 1,
};

struct TreeGermination {
    TreeBrainType brain_type = TreeBrainType::RULE_BASED;
    uint32_t neural_seed = 42;
    UUID genome_id; // If not nil, load genome from repository for NeuralNetBrain.

    using serialize = zpp::bits::members<3>;
};

void from_json(const nlohmann::json& j, TreeGermination& config);
void to_json(nlohmann::json& j, const TreeGermination& config);

} // namespace DirtSim::Config
