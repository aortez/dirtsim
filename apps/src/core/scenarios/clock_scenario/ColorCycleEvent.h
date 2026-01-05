#pragma once

#include "ClockEventTypes.h"

namespace DirtSim {

class World;

namespace ClockEvents {

/**
 * Initializes the ColorCycle event state.
 *
 * @param state The state to initialize.
 * @param world The world to modify.
 * @param colors_per_second Rate of color cycling.
 */
void startColorCycle(ColorCycleEventState& state, World& world, double colors_per_second);

/**
 * Updates the ColorCycle event each tick.
 *
 * @param state The event state to update.
 * @param world The world to modify.
 * @param deltaTime Time since last update.
 */
void updateColorCycle(ColorCycleEventState& state, World& world, double deltaTime);

/**
 * Cleans up when the ColorCycle event ends.
 *
 * @param world The world to restore.
 * @param digit_material The material to restore digit cells to.
 */
void endColorCycle(World& world, MaterialType digit_material);

} // namespace ClockEvents
} // namespace DirtSim
