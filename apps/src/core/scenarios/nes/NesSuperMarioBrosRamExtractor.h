#pragma once

#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

namespace DirtSim {

class NesSuperMarioBrosRamExtractor {
public:
    NesSuperMarioBrosState extract(
        const SmolnesRuntime::MemorySnapshot& snapshot, bool setupComplete) const;
};

} // namespace DirtSim
