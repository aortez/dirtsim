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
 * @param current_time Current time string for initializing time tracking.
 * @return The starting material that was selected.
 */
MaterialType startColorShowcase(
    ColorShowcaseEventState& state,
    const std::vector<MaterialType>& showcase_materials,
    std::mt19937& rng,
    const std::string& current_time);

/**
 * Updates the ColorShowcase event when time changes.
 * Uses internal state tracking to detect time changes independently,
 * avoiding conflicts with events that take over rendering.
 *
 * @param state The event state to update.
 * @param showcase_materials List of materials to showcase.
 * @param current_time Current time string.
 * @return The new material if time changed, or empty optional if no change.
 */
std::optional<MaterialType> updateColorShowcase(
    ColorShowcaseEventState& state,
    const std::vector<MaterialType>& showcase_materials,
    const std::string& current_time);

} // namespace ClockEvents
} // namespace DirtSim
