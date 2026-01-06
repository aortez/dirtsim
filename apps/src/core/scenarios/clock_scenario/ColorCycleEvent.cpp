#include "ColorCycleEvent.h"
#include "core/MaterialType.h"
#include "spdlog/spdlog.h"

namespace DirtSim {
namespace ClockEvents {

MaterialType startColorCycle(ColorCycleEventState& state, double colors_per_second)
{
    // Calculate time per color from rate.
    state.time_per_color = 1.0 / colors_per_second;
    state.current_index = 0;
    state.time_in_current = 0.0;

    // Return the first color.
    return getAllMaterialTypes()[0];
}

std::optional<MaterialType> updateColorCycle(ColorCycleEventState& state, double deltaTime)
{
    state.time_in_current += deltaTime;

    // Check if it's time to advance to the next color.
    if (state.time_in_current >= state.time_per_color) {
        state.time_in_current -= state.time_per_color;
        state.current_index = (state.current_index + 1) % getAllMaterialTypes().size();

        MaterialType new_material = getAllMaterialTypes()[state.current_index];

        spdlog::debug("ClockScenario: COLOR_CYCLE advanced to {} (index {})",
            getMaterialName(new_material), state.current_index);

        return new_material;
    }

    return std::nullopt;
}

} // namespace ClockEvents
} // namespace DirtSim
