#include "ColorCycleEvent.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"

namespace DirtSim {
namespace ClockEvents {

void startColorCycle(ColorCycleEventState& state, World& world, double colors_per_second)
{
    // Calculate time per color from rate.
    state.time_per_color = 1.0 / colors_per_second;
    state.current_index = 0;
    state.time_in_current = 0.0;

    // Apply the first color immediately.
    MaterialType first_material = getAllMaterialTypes()[0];
    WorldData& data = world.getData();
    for (uint32_t y = 1; y < data.height - 1; ++y) {
        for (uint32_t x = 1; x < data.width - 1; ++x) {
            Cell& cell = data.at(x, y);
            if (cell.material_type == MaterialType::WALL && cell.render_as >= 0) {
                cell.render_as = static_cast<int8_t>(first_material);
            }
        }
    }
}

void updateColorCycle(ColorCycleEventState& state, World& world, double deltaTime)
{
    state.time_in_current += deltaTime;

    // Check if it's time to advance to the next color.
    if (state.time_in_current >= state.time_per_color) {
        state.time_in_current -= state.time_per_color;
        state.current_index = (state.current_index + 1) % getAllMaterialTypes().size();

        MaterialType new_material = getAllMaterialTypes()[state.current_index];

        // Update all digit cells to the new color.
        WorldData& data = world.getData();
        for (uint32_t y = 1; y < data.height - 1; ++y) {
            for (uint32_t x = 1; x < data.width - 1; ++x) {
                Cell& cell = data.at(x, y);
                // Digit cells are WALL with render_as override set.
                if (cell.material_type == MaterialType::WALL && cell.render_as >= 0) {
                    cell.render_as = static_cast<int8_t>(new_material);
                }
            }
        }

        spdlog::debug("ClockScenario: COLOR_CYCLE advanced to {} (index {})",
            getMaterialName(new_material), state.current_index);
    }
}

void endColorCycle(World& world, MaterialType digit_material)
{
    // Restore original digit material color.
    WorldData& data = world.getData();
    for (uint32_t y = 1; y < data.height - 1; ++y) {
        for (uint32_t x = 1; x < data.width - 1; ++x) {
            Cell& cell = data.at(x, y);
            if (cell.material_type == MaterialType::WALL && cell.render_as >= 0) {
                cell.render_as = static_cast<int8_t>(digit_material);
            }
        }
    }
}

} // namespace ClockEvents
} // namespace DirtSim
