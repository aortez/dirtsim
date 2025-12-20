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
MaterialType getDominantMaterial(
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
