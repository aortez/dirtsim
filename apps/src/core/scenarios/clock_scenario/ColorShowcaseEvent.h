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
Material::EnumType startColorShowcase(
    ColorShowcaseEventState& state,
    const std::vector<Material::EnumType>& showcase_materials,
    std::mt19937& rng);

/**
 * Updates the ColorShowcase event.
 * Advances to the next showcase material when time_changed is true.
 *
 * @param state The event state to update.
 * @param showcase_materials List of materials to showcase.
 * @param time_changed Whether the displayed time changed this frame.
 * @return The new material if changed, or empty optional if no change.
 */
std::optional<Material::EnumType> updateColorShowcase(
    ColorShowcaseEventState& state,
    const std::vector<Material::EnumType>& showcase_materials,
    bool time_changed);

} // namespace ClockEvents
} // namespace DirtSim
