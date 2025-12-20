#include "OrganismSensoryData.h"
#include "core/Cell.h"
#include "core/World.h"
#include "core/WorldData.h"

namespace DirtSim {
namespace SensoryUtils {

template <int GridSize, int NumMaterials>
void gatherMaterialHistograms(
    const World& world,
    Vector2i center,
    std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    Vector2i& world_offset)
{
    const WorldData& data = world.getData();

    int half_window = GridSize / 2;
    int offset_x = center.x - half_window;
    int offset_y = center.y - half_window;

    // Always center on organism - no clamping.
    // Out-of-bounds cells will be marked as WALL below.
    world_offset = Vector2i{ offset_x, offset_y };

    // Clear histograms.
    for (auto& row : histograms) {
        for (auto& cell : row) {
            cell.fill(0.0);
        }
    }

    // Populate material histograms by sampling world grid.
    for (int ny = 0; ny < GridSize; ny++) {
        for (int nx = 0; nx < GridSize; nx++) {
            int wx = world_offset.x + nx;
            int wy = world_offset.y + ny;

            // Check bounds.
            if (wx < 0 || wy < 0 || static_cast<uint32_t>(wx) >= data.width
                || static_cast<uint32_t>(wy) >= data.height) {
                // Out of bounds - treat as WALL so organisms can detect world edges.
                int wall_idx = static_cast<int>(MaterialType::WALL);
                if (wall_idx >= 0 && wall_idx < NumMaterials) {
                    histograms[ny][nx][wall_idx] = 1.0;
                }
                continue;
            }

            const Cell& cell = data.at(wx, wy);
            int material_idx = static_cast<int>(cell.material_type);
            if (material_idx >= 0 && material_idx < NumMaterials) {
                histograms[ny][nx][material_idx] = cell.fill_ratio;
            }
        }
    }
}

template <int GridSize, int NumMaterials>
MaterialType getDominantMaterial(
    const std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    int gx,
    int gy)
{
    if (gx < 0 || gx >= GridSize || gy < 0 || gy >= GridSize) {
        return MaterialType::AIR;
    }

    double max_fill = 0.0;
    int max_idx = 0;
    for (int i = 0; i < NumMaterials; i++) {
        if (histograms[gy][gx][i] > max_fill) {
            max_fill = histograms[gy][gx][i];
            max_idx = i;
        }
    }
    return static_cast<MaterialType>(max_idx);
}

template <int GridSize, int NumMaterials>
bool isSolid(
    const std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    int gx,
    int gy)
{
    MaterialType mat = getDominantMaterial<GridSize, NumMaterials>(histograms, gx, gy);
    return mat != MaterialType::AIR && mat != MaterialType::WATER;
}

template <int GridSize, int NumMaterials>
bool isEmpty(
    const std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    int gx,
    int gy)
{
    if (gx < 0 || gx >= GridSize || gy < 0 || gy >= GridSize) {
        return true;
    }

    double total_fill = 0.0;
    for (int i = 0; i < NumMaterials; i++) {
        total_fill += histograms[gy][gx][i];
    }
    return total_fill < 0.1;
}

// Explicit instantiations for common sizes.
template void gatherMaterialHistograms<15, 10>(
    const World& world,
    Vector2i center,
    std::array<std::array<std::array<double, 10>, 15>, 15>& histograms,
    Vector2i& world_offset);

template void gatherMaterialHistograms<9, 10>(
    const World& world,
    Vector2i center,
    std::array<std::array<std::array<double, 10>, 9>, 9>& histograms,
    Vector2i& world_offset);

template MaterialType getDominantMaterial<15, 10>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms,
    int gx,
    int gy);

template MaterialType getDominantMaterial<9, 10>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms,
    int gx,
    int gy);

template bool isSolid<15, 10>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms,
    int gx,
    int gy);

template bool isSolid<9, 10>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms,
    int gx,
    int gy);

template bool isEmpty<15, 10>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms,
    int gx,
    int gy);

template bool isEmpty<9, 10>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms,
    int gx,
    int gy);

} // namespace SensoryUtils
} // namespace DirtSim
