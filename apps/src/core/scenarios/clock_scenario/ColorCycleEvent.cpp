#include "ColorCycleEvent.h"
#include "core/MaterialType.h"
#include "spdlog/spdlog.h"

namespace DirtSim {
namespace ClockEvents {

Material::EnumType startColorCycle(ColorCycleEventState& state, double colors_per_second)
{
    // Calculate time per color from rate.
    state.time_per_color = 1.0 / colors_per_second;
    state.current_index = 0;
    state.time_in_current = 0.0;

    // Return the first color.
    return Material::getAllTypes()[0];
}

std::optional<Material::EnumType> updateColorCycle(ColorCycleEventState& state, double deltaTime)
{
    state.time_in_current += deltaTime;

    // Check if it's time to advance to the next color.
    if (state.time_in_current >= state.time_per_color) {
        state.time_in_current -= state.time_per_color;
        state.current_index = (state.current_index + 1) % Material::getAllTypes().size();

        Material::EnumType new_material = Material::getAllTypes()[state.current_index];

        spdlog::debug(
            "ClockScenario: COLOR_CYCLE advanced to {} (index {})",
            toString(new_material),
            state.current_index);

        return new_material;
    }

    return std::nullopt;
}

} // namespace ClockEvents
} // namespace DirtSim
