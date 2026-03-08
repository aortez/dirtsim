#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace DirtSim {

enum class GenomePoolId : uint8_t {
    DirtSim = 0,
    FlappyParatroopa,
    Smb,
    SuperTiltBro,
};

std::string toString(GenomePoolId id);

void to_json(nlohmann::json& j, const GenomePoolId& id);
void from_json(const nlohmann::json& j, GenomePoolId& id);

} // namespace DirtSim
