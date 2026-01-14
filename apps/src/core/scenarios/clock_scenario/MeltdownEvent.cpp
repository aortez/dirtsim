#include "MeltdownEvent.h"
#include "core/Cell.h"
#include "core/FragmentationParams.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldCollisionCalculator.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace DirtSim {
namespace ClockEvents {

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

    // Fragmentation params for digit material spraying up from drain.
    static const FragmentationParams melt_frag_params{
        .radial_bias = 0.3,
        .min_arc = M_PI / 4.0,
        .max_arc = M_PI / 3.0,
        .edge_speed_factor = 1.0,
        .base_speed = 40.0,
        .spray_fraction = 1.0,
    };

    // Scan for digit material that has reached the bottom or fallen into drain.
    bool any_digit_material_above_bottom = false;

    Vector2d spray_direction(0.0, -1.0);
    constexpr int NUM_FRAGS = 4;
    constexpr double ARC_WIDTH = M_PI / 2.0;

    for (int x = 1; x < data.width - 1; ++x) {
        // Check cells in drain hole (bottom wall row, if drain is open).
        if (drain_open && x >= drain_start_x && x <= drain_end_x) {
            Cell& drain_cell = data.at(x, bottom_wall_y);
            if (drain_cell.material_type == digit_mat) {
                // Convert to water and spray upward.
                drain_cell.replaceMaterial(Material::EnumType::Water, drain_cell.fill_ratio);

                world.getCollisionCalculator().fragmentSingleCell(
                    world,
                    drain_cell,
                    x,
                    bottom_wall_y,
                    x,
                    bottom_wall_y,
                    spray_direction,
                    NUM_FRAGS,
                    ARC_WIDTH,
                    melt_frag_params);

                drain_cell = Cell();
            }
        }

        // Check cells adjacent to bottom wall (row above it).
        Cell& bottom_cell = data.at(x, above_bottom_y);
        if (bottom_cell.material_type == digit_mat) {
            // Convert to water and splash upward.
            bottom_cell.replaceMaterial(Material::EnumType::Water, bottom_cell.fill_ratio);

            world.getCollisionCalculator().fragmentSingleCell(
                world,
                bottom_cell,
                x,
                above_bottom_y,
                x,
                above_bottom_y,
                spray_direction,
                NUM_FRAGS,
                ARC_WIDTH,
                melt_frag_params);

            bottom_cell = Cell();
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
                cell.replaceMaterial(Material::EnumType::Water, cell.fill_ratio);
            }
        }
    }
}

} // namespace ClockEvents
} // namespace DirtSim
