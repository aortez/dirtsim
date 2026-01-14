#pragma once

#include "TreeCommands.h"
#include "core/Vector2i.h"
#include <array>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {

enum class GrowthStage : uint8_t { SEED, GERMINATION, SAPLING, MATURE, DECLINE };

/**
 * Tree-specific sensory data.
 *
 * Contains a 15x15 grid of material histograms representing the tree's
 * view of the world around it, plus tree-specific state and action feedback.
 */
struct TreeSensoryData {
    static constexpr int GRID_SIZE = 15;
    static constexpr int NUM_MATERIALS = 10;

    // Material histogram grid: [y][x][material] = fill contribution.
    std::array<std::array<std::array<double, NUM_MATERIALS>, GRID_SIZE>, GRID_SIZE>
        material_histograms = {};

    // Mapping from neural grid to world coordinates.
    int actual_width = 0;
    int actual_height = 0;
    double scale_factor = 1.0;
    Vector2i world_offset;
    Vector2i seed_position;

    // Tree-specific state.
    double age_seconds = 0.0;
    GrowthStage stage = GrowthStage::SEED;
    double total_energy = 0.0;
    double total_water = 0.0;
    std::string current_thought;

    // Current action state.
    std::optional<TreeCommandType> current_action; // nullopt if idle.
    double action_progress = 0.0;                  // 0.0 to 1.0, how far along current action is.

    using serialize = zpp::bits::members<13>;
};

void to_json(nlohmann::json& j, const GrowthStage& stage);
void from_json(const nlohmann::json& j, GrowthStage& stage);

void to_json(nlohmann::json& j, const TreeSensoryData& data);
void from_json(const nlohmann::json& j, TreeSensoryData& data);

} // namespace DirtSim
