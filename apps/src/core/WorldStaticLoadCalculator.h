#pragma once

#include "WorldCalculatorBase.h"

namespace DirtSim {

class World;

class WorldStaticLoadCalculator : public WorldCalculatorBase {
public:
    WorldStaticLoadCalculator() = default;

    void recomputeAll(World& world) const;

private:
    static constexpr double MIN_GRAVITY_THRESHOLD = 0.001;
};

} // namespace DirtSim
