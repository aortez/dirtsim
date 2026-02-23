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
 * Contains a 15x15 grid of material histograms representing the duck's
 * view of the world around it, plus duck-specific state.
 *
 * The 15x15 grid matches tree sensory coverage so duck and tree brains can
 * share a wider world context.
 */
struct DuckSensoryData {
    static constexpr int GRID_SIZE = 15;
    static constexpr int NUM_MATERIALS = 10;
    static constexpr int SPECIAL_SENSE_COUNT = 32;

    // Material histogram grid: [y][x][material] = fill contribution.
    std::array<std::array<std::array<double, NUM_MATERIALS>, GRID_SIZE>, GRID_SIZE>
        material_histograms = {};

    // Mapping from neural grid to world coordinates.
    int actual_width = 0;
    int actual_height = 0;
    double scale_factor = 1.0;
    Vector2i world_offset;

    // Duck's current position in world coordinates.
    Vector2i position;

    // Physics state.
    Vector2d velocity;
    bool on_ground = false;

    // Facing direction (-1 = left, +1 = right).
    float facing_x = 1.0f;

    // Scenario-provided special senses. Unused slots stay at zero.
    std::array<double, SPECIAL_SENSE_COUNT> special_senses{};

    double delta_time_seconds = 0.0;

    using serialize = zpp::bits::members<13>;
};

void to_json(nlohmann::json& j, const DuckSensoryData& data);
void from_json(const nlohmann::json& j, DuckSensoryData& data);

} // namespace DirtSim
