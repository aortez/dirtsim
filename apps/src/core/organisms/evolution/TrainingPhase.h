#pragma once

#include <cstdint>

namespace DirtSim {

enum class TrainingPhase : uint8_t {
    Normal = 0,
    Plateau,
    Stuck,
    Recovery,
};

} // namespace DirtSim
