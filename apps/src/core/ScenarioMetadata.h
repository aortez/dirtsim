#pragma once

#include <array>
#include <string>
#include <string_view>

namespace DirtSim {

/**
 * @brief Metadata about a scenario for UI display.
 */
struct ScenarioMetadata {
    std::string_view id;          // Scenario ID (e.g., "sandbox").
    std::string_view name;        // Display name (e.g., "Sandbox").
    std::string_view description; // Brief description.
    std::string_view category;    // Category (sandbox, demo, organisms, benchmark).
};

/**
 * @brief Static registry of all scenarios.
 * Single source of truth shared between server and UI.
 *
 * Order matters for UI dropdown index mapping.
 */
constexpr std::array<ScenarioMetadata, 8> SCENARIO_METADATA = {{
    { "sandbox", "Sandbox", "Default sandbox with dirt quadrant and particle streams", "sandbox" },
    { "clock", "Clock", "Digital clock displaying system time (HH:MM:SS)", "demo" },
    { "dam_break", "Dam Break", "Water column held by wall dam that breaks", "demo" },
    { "empty", "Empty", "A completely empty world with no particles", "sandbox" },
    { "falling_dirt", "Falling Dirt", "Dirt particles falling from the sky", "demo" },
    { "raining", "Raining", "Rain falling from the sky", "demo" },
    { "tree_germination", "Tree Germination", "Seed growing into balanced tree", "organisms" },
    { "water_equalization",
      "Water Equalization",
      "Water flows through opening to equalize",
      "demo" },
}};

/**
 * @brief Find scenario metadata by ID.
 * @return Pointer to metadata, or nullptr if not found.
 */
inline const ScenarioMetadata* findScenarioMetadata(std::string_view id)
{
    for (const auto& meta : SCENARIO_METADATA) {
        if (meta.id == id) {
            return &meta;
        }
    }
    return nullptr;
}

/**
 * @brief Get scenario index in SCENARIO_METADATA array.
 * @return Index, or 0 if not found.
 */
inline size_t getScenarioIndex(std::string_view id)
{
    for (size_t i = 0; i < SCENARIO_METADATA.size(); ++i) {
        if (SCENARIO_METADATA[i].id == id) {
            return i;
        }
    }
    return 0; // Default to first scenario (sandbox).
}

} // namespace DirtSim
