#pragma once

#include "ClockEventTypes.h"
#include <optional>

namespace DirtSim {

namespace ClockEvents {

/**
 * Initializes the ColorCycle event state.
 *
 * @param state The state to initialize.
 * @param colors_per_second Rate of color cycling.
 * @return The initial material to use for digits.
 */
Material::EnumType startColorCycle(ColorCycleEventState& state, double colors_per_second);

/**
 * Updates the ColorCycle event each tick.
 *
 * @param state The event state to update.
 * @param deltaTime Time since last update.
 * @return The new material if color changed, nullopt otherwise.
 */
std::optional<Material::EnumType> updateColorCycle(ColorCycleEventState& state, double deltaTime);

} // namespace ClockEvents
} // namespace DirtSim
