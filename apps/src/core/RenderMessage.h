#pragma once

#include "Entity.h"
#include "ReflectSerializer.h"
#include "RenderFormat.h"
#include "Vector2.h"
#include "organisms/TreeSensoryData.h"
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Basic cell data for rendering (7 bytes).
 *
 * Contains material type, fill ratio, render-as override, and lit color.
 * Fill ratio is quantized to 8-bit precision (256 levels).
 */
struct BasicCell {
    uint8_t material_type; // Material::EnumType enum value (0-9).
    uint8_t fill_ratio;    // Quantized [0.0, 1.0] → [0, 255].
    int8_t render_as; // Render override: -1 = use material_type, 0+ = Material::EnumType value.
    uint32_t color;   // Lit color (packed RGBA from WorldLightCalculator).

    using serialize = zpp::bits::members<4>;
};

/**
 * @brief Debug cell data for physics visualization.
 *
 * Includes material, fill ratio, and quantized physics data for debug overlays.
 * All floating-point values are converted to fixed-point integers.
 */
struct DebugCell {
    uint8_t material_type; // Material::EnumType enum value (0-9).
    uint8_t fill_ratio;    // Quantized [0.0, 1.0] → [0, 255].
    int8_t render_as; // Render override: -1 = use material_type, 0+ = Material::EnumType value.

    int16_t com_x;      // Center of mass X: [-1.0, 1.0] → [-32767, 32767].
    int16_t com_y;      // Center of mass Y: [-1.0, 1.0] → [-32767, 32767].
    int16_t velocity_x; // Velocity X: [-10.0, 10.0] → [-32767, 32767].
    int16_t velocity_y; // Velocity Y: [-10.0, 10.0] → [-32767, 32767].

    uint16_t pressure_hydro;   // Hydrostatic pressure: [0, 1000] → [0, 65535].
    uint16_t pressure_dynamic; // Dynamic pressure: [0, 1000] → [0, 65535].

    Vector2<float> pressure_gradient; // Pressure gradient vector.

    using serialize = zpp::bits::members<10>;
};

/**
 * @brief Sparse organism data.
 *
 * Instead of sending organism_id for every cell (mostly zeros), we send a sparse
 * representation: organism ID + list of cells it occupies.
 *
 * Example: 1 tree with 100 cells:
 *   Dense: 22,500 bytes (1 byte per cell)
 *   Sparse: ~200 bytes (1 byte ID + 100 × 2 byte indices)
 */
struct OrganismData {
    uint8_t organism_id;                // Organism identifier (1-255, 0 = none).
    std::vector<uint16_t> cell_indices; // Flat grid indices (y * width + x).

    using serialize = zpp::bits::members<2>;
};

/**
 * @brief Bone connection data for organism structural visualization.
 *
 * Represents spring connections between organism cells.
 * Rendered as lines to show the organism's internal structure.
 */
struct BoneData {
    Vector2i cell_a; // First cell position.
    Vector2i cell_b; // Second cell position.

    using serialize = zpp::bits::members<2>;
};

/**
 * @brief Generic scenario-provided video frame payload.
 *
 * Pixels are encoded as packed RGB565 (little-endian), row-major.
 */
struct ScenarioVideoFrame {
    uint16_t width = 0;
    uint16_t height = 0;
    uint64_t frame_id = 0;
    std::vector<std::byte> pixels;

    using serialize = zpp::bits::members<4>;
};

/**
 * @brief Render message containing optimized world state.
 *
 * Replaces full WorldData serialization for frame streaming.
 * Format determines payload structure (BasicCell or DebugCell).
 */
struct RenderMessage {
    RenderFormat::EnumType format; // Which format is payload encoded in?

    // Grid dimensions and simulation state.
    int16_t width = 0;
    int16_t height = 0;
    int32_t timestep = 0;
    double fps_server = 0.0;

    // Format-specific cell data (either BasicCell[] or DebugCell[]).
    std::vector<std::byte> payload;

    // Sparse organism tracking (only cells with organism_id != 0).
    std::vector<OrganismData> organisms;

    // Bone connections for structural visualization.
    std::vector<BoneData> bones;

    // Tree organism data (optional - only present when showing a tree's vision).
    std::optional<TreeSensoryData> tree_vision;

    // Entities (duck, butterfly, etc.) - sprite-based world overlays.
    std::vector<Entity> entities;

    // Optional scenario-native video frame (RGB565) for direct display.
    std::optional<ScenarioVideoFrame> scenario_video_frame;

    using serialize = zpp::bits::members<11>;
};

void to_json(nlohmann::json& j, const BasicCell& cell);
void from_json(const nlohmann::json& j, BasicCell& cell);
void to_json(nlohmann::json& j, const BoneData& bone);
void from_json(const nlohmann::json& j, BoneData& bone);
void to_json(nlohmann::json& j, const DebugCell& cell);
void from_json(const nlohmann::json& j, DebugCell& cell);
void to_json(nlohmann::json& j, const OrganismData& org);
void from_json(const nlohmann::json& j, OrganismData& org);
void to_json(nlohmann::json& j, const ScenarioVideoFrame& frame);
void from_json(const nlohmann::json& j, ScenarioVideoFrame& frame);

} // namespace DirtSim
