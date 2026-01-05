#pragma once

#include "ClockEventTypes.h"
#include <random>
#include <vector>

namespace DirtSim {

namespace ClockEvents {

/**
 * Initializes the ColorShowcase event state.
 *
 * @param state The state to initialize.
 * @param showcase_materials List of materials to showcase.
 * @param rng Random number generator for selecting starting color.
 * @return The starting material that was selected.
 */
MaterialType startColorShowcase(
    ColorShowcaseEventState& state,
    const std::vector<MaterialType>& showcase_materials,
    std::mt19937& rng);

/**
 * Updates the ColorShowcase event when time changes.
 *
 * @param state The event state to update.
 * @param showcase_materials List of materials to showcase.
 * @param current_time Current time string.
 * @param last_drawn_time Previously drawn time string.
 * @return The new material if time changed, or empty optional if no change.
 */
std::optional<MaterialType> updateColorShowcase(
    ColorShowcaseEventState& state,
    const std::vector<MaterialType>& showcase_materials,
    const std::string& current_time,
    const std::string& last_drawn_time);

} // namespace ClockEvents
} // namespace DirtSim
