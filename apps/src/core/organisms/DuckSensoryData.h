#pragma once

#include "core/Vector2.h"
#include "core/Vector2i.h"
#include <array>
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {

/**
 * Duck-specific sensory data.
 *
 * Contains a 21x21 grid of material histograms representing the duck's
 * view of the world around it, plus duck-specific state.
 */
struct DuckSensoryData {
    static constexpr int GRID_SIZE = 21;
    static constexpr int NUM_MATERIALS = 10;
    static constexpr int SPECIAL_SENSE_COUNT = 32;

    // Material histogram grid: [y][x][material] = fill contribution.
    std::array<std::array<std::array<float, NUM_MATERIALS>, GRID_SIZE>, GRID_SIZE>
        material_histograms = {};

    // Mapping from neural grid to world coordinates.
    int actual_width = 0;
    int actual_height = 0;
    double scale_factor = 1.0;
    Vector2i world_offset;

    // Coarse integer position in scenario coordinates.
    // World ducks use anchor-cell coordinates; NES adapters typically pin this to view center.
    Vector2i position;

    // Body reference point inside the current visual frame, normalized to [0,1].
    float self_view_x = 0.5f;
    float self_view_y = 0.5f;

    // Physics state.
    Vector2d velocity;
    bool on_ground = false;

    // Facing direction (-1 = left, +1 = right).
    float facing_x = 1.0f;

    // Previous applied control channels from the prior tick.
    // For NES, these are reconstructed from the resolved controller mask.
    float previous_control_x = 0.0f;
    float previous_control_y = 0.0f;
    bool previous_jump = false;
    bool previous_run = false;

    // Scenario-provided special senses. Unused slots stay at zero.
    std::array<double, SPECIAL_SENSE_COUNT> special_senses{};

    // Current energy level [0, 1].
    float energy = 1.0f;

    // Current health level [0, 1].
    float health = 1.0f;

    double delta_time_seconds = 0.0;

    using serialize = zpp::bits::members<19>;
};

void to_json(nlohmann::json& j, const DuckSensoryData& data);
void from_json(const nlohmann::json& j, DuckSensoryData& data);

} // namespace DirtSim
