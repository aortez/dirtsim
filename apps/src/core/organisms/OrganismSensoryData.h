#pragma once

#include "core/MaterialType.h"
#include "core/Vector2i.h"
#include <array>
#include <cstdint>

namespace DirtSim {

// Forward declarations.
class World;

/**
 * Utility functions for gathering organism sensory data.
 *
 * These functions help organisms perceive their environment by sampling
 * the world grid into material histograms.
 */
namespace SensoryUtils {

/**
 * Match mode for template cell patterns.
 */
enum class MatchMode {
    Is,         // Must be one of the specified materials.
    IsNot,      // Must NOT be any of the specified materials.
    Any,        // Wildcard - matches anything.
    IsEmpty,    // Air or empty (low fill).
    IsNotEmpty, // Has material.
    IsSolid,    // Solid material (not fluid).
    IsLiquid    // Fluid material (water, air).
};

/**
 * Pattern constraint for a single cell.
 */
struct CellPattern {
    MatchMode mode = MatchMode::Any;
    std::vector<Material::EnumType> materials;

    CellPattern() = default;
    CellPattern(MatchMode m) : mode(m) {}
    CellPattern(MatchMode m, std::vector<Material::EnumType> mats)
        : mode(m), materials(std::move(mats))
    {}
};

/**
 * Sensory template for pattern matching.
 *
 * 2D pattern of cell constraints.
 */
struct SensoryTemplate {
    int width;
    int height;
    std::vector<std::vector<CellPattern>> pattern;

    SensoryTemplate(int w, int h) : width(w), height(h), pattern(h, std::vector<CellPattern>(w)) {}
};

/**
 * Result of a template search.
 */
struct TemplateMatch {
    bool found = false;
    int col = -1;
    int row = -1;
};

/**
 * Find a template in the sensory grid.
 *
 * Scans the entire sensory grid for the pattern and returns the first match.
 *
 * @param histograms The sensory material histograms.
 * @param template_pattern The pattern to find.
 * @return Match result with position (or found=false if not found).
 */
template <int GridSize, int NumMaterials>
TemplateMatch findTemplate(
    const std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    const SensoryTemplate& template_pattern);

/**
 * Match a template against the sensory grid at a specific position.
 *
 * @param histograms The sensory material histograms.
 * @param template_pattern The pattern to match.
 * @param start_col Starting column in sensory grid.
 * @param start_row Starting row in sensory grid.
 * @return True if pattern matches at this position.
 */
template <int GridSize, int NumMaterials>
bool matchesTemplate(
    const std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    const SensoryTemplate& template_pattern,
    int start_col,
    int start_row);

/**
 * Gather material histograms from the world centered on a position.
 *
 * Always centers on the organism - out-of-bounds cells are marked as WALL
 * so organisms can detect world edges.
 *
 * @tparam GridSize The size of the perception grid.
 * @tparam NumMaterials The number of material types.
 * @param world The world to sample from.
 * @param center The center position in world coordinates.
 * @param histograms Output array to fill with material histograms.
 * @param world_offset Output: offset from neural grid to world coordinates.
 */
template <int GridSize, int NumMaterials>
void gatherMaterialHistograms(
    const World& world,
    Vector2i center,
    std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    Vector2i& world_offset);

/**
 * Get the dominant material at a grid position.
 *
 * @tparam GridSize The size of the perception grid.
 * @tparam NumMaterials The number of material types.
 * @param histograms The material histograms.
 * @param gx Grid x coordinate (0 to GridSize-1).
 * @param gy Grid y coordinate (0 to GridSize-1).
 * @return The material type with the highest fill ratio.
 */
template <int GridSize, int NumMaterials>
Material::EnumType getDominantMaterial(
    const std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    int gx,
    int gy);

/**
 * Check if a grid position is solid (non-AIR, non-WATER).
 */
template <int GridSize, int NumMaterials>
bool isSolid(
    const std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    int gx,
    int gy);

/**
 * Check if a grid position is empty (very low total fill).
 */
template <int GridSize, int NumMaterials>
bool isEmpty(
    const std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    int gx,
    int gy);

// Explicit instantiation declarations for common sizes.
extern template void gatherMaterialHistograms<15, 10>(
    const World& world,
    Vector2i center,
    std::array<std::array<std::array<double, 10>, 15>, 15>& histograms,
    Vector2i& world_offset);

extern template void gatherMaterialHistograms<9, 10>(
    const World& world,
    Vector2i center,
    std::array<std::array<std::array<double, 10>, 9>, 9>& histograms,
    Vector2i& world_offset);

} // namespace SensoryUtils

} // namespace DirtSim
