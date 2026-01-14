#pragma once

#include "ClockEventTypes.h"
#include <random>

namespace DirtSim {

class World;

namespace ClockEvents {

/**
 * Updates the Rain event, spawning water drops.
 *
 * @param world The world to add water drops to.
 * @param deltaTime Time since last update.
 * @param rng Random number generator.
 * @param uniform_dist Uniform distribution [0,1].
 */
void updateRain(
    World& world,
    double deltaTime,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uniform_dist);

} // namespace ClockEvents
} // namespace DirtSim
