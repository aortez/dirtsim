#include "MeltdownEvent.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"
#include <algorithm>

namespace DirtSim {
namespace ClockEvents {

namespace {

void convertCellToBulkWater(World& world, Cell& cell, int x, int y)
{
    const float amount = std::clamp(cell.fill_ratio, 0.0f, 1.0f);
    cell.clear();

    if (amount >= World::MIN_MATTER_THRESHOLD) {
        world.addBulkWaterAtCell(x, y, amount);
    }
}

} // namespace

void startMeltdown(MeltdownEventState& state, World& world)
{
    // Use METAL for falling digits - dense, falls through water nicely.
    // The UI digitMaterial setting only affects render color, not physics.
    state.digit_material = Material::EnumType::Metal;
    WorldData& data = world.getData();

    // Convert interior WALL cells (digit display cells) to METAL so they can fall.
    // These are WALL cells with render_as set (indicating they are digit cells).
    int max_digit_y = 0;
    for (int y = 1; y < data.height - 1; ++y) {
        for (int x = 1; x < data.width - 1; ++x) {
            Cell& cell = data.at(x, y);

            // Only convert WALL cells with render_as override (digit cells).
            if (cell.material_type == Material::EnumType::Wall && cell.render_as >= 0) {
                // Convert to the digit material so it can fall.
                cell.material_type = state.digit_material;
                cell.render_as = -1; // Clear override - now it's the real material.
                max_digit_y = std::max(max_digit_y, static_cast<int>(y));
            }
        }
    }
    state.digit_bottom_y = max_digit_y;

    spdlog::info(
        "ClockScenario: MELTDOWN started (digit_bottom_y: {}, material: {})",
        state.digit_bottom_y,
        toString(state.digit_material));
}

void updateMeltdown(
    MeltdownEventState& state,
    World& world,
    double& remaining_time,
    double event_duration,
    bool drain_open,
    int16_t drain_start_x,
    int16_t drain_end_x)
{
    WorldData& data = world.getData();
    if (data.height < 3) return;

    int bottom_wall_y = data.height - 1;
    int above_bottom_y = data.height - 2;
    Material::EnumType digit_mat = state.digit_material;

    // Scan for digit material that has reached the bottom or fallen into drain.
    bool any_digit_material_above_bottom = false;

    for (int x = 1; x < data.width - 1; ++x) {
        // Check cells in drain hole (bottom wall row, if drain is open).
        if (drain_open && x >= drain_start_x && x <= drain_end_x) {
            Cell& drain_cell = data.at(x, bottom_wall_y);
            if (drain_cell.material_type == digit_mat) {
                if (drain_cell.fill_ratio < World::MIN_MATTER_THRESHOLD) {
                    drain_cell = Cell();
                    continue;
                }
                convertCellToBulkWater(world, drain_cell, x, bottom_wall_y);
            }
        }

        // Check cells adjacent to bottom wall (row above it).
        Cell& bottom_cell = data.at(x, above_bottom_y);
        if (bottom_cell.material_type == digit_mat) {
            if (bottom_cell.fill_ratio < World::MIN_MATTER_THRESHOLD) {
                bottom_cell = Cell();
                continue;
            }
            convertCellToBulkWater(world, bottom_cell, x, above_bottom_y);
        }
    }

    // Check if any digit material still exists above the bottom row (still falling).
    for (int y = 1; y < above_bottom_y; ++y) {
        for (int x = 1; x < data.width - 1; ++x) {
            if (data.at(x, y).material_type == digit_mat) {
                any_digit_material_above_bottom = true;
                break;
            }
        }
        if (any_digit_material_above_bottom) break;
    }

    // End early if all digit material has reached the bottom.
    // But wait at least 3 seconds for material to start falling first.
    constexpr double MIN_MELTDOWN_TIME = 3.0;
    double elapsed = event_duration - remaining_time;

    if (!any_digit_material_above_bottom && elapsed >= MIN_MELTDOWN_TIME) {
        remaining_time = 0.0;
    }
}

void endMeltdown(World& world, Material::EnumType digit_material)
{
    WorldData& data = world.getData();

    // Convert all digit material to water, then redraw fresh digits.
    for (int y = 1; y < data.height; ++y) {
        for (int x = 1; x < data.width - 1; ++x) {
            Cell& cell = data.at(x, y);
            if (cell.material_type == digit_material) {
                convertCellToBulkWater(world, cell, x, y);
            }
        }
    }
}

} // namespace ClockEvents
} // namespace DirtSim
