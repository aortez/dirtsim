#include "RainEvent.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"

namespace DirtSim {
namespace ClockEvents {

void updateRain(
    World& world,
    double deltaTime,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uniform_dist)
{
    // Spawn water drops at random X positions near the top.
    constexpr double DROPS_PER_SECOND = 10.0;
    double drop_probability = DROPS_PER_SECOND * deltaTime;

    if (uniform_dist(rng) < drop_probability) {
        std::uniform_int_distribution<uint32_t> x_dist(2, world.getData().width - 3);
        uint32_t x = x_dist(rng);
        uint32_t y = 2;

        world.addMaterialAtCell(
            { static_cast<int16_t>(x), static_cast<int16_t>(y) }, Material::EnumType::Water, 0.5);
    }

    // Water drainage is handled by updateDrain() in ClockScenario::tick().
}

} // namespace ClockEvents
} // namespace DirtSim
