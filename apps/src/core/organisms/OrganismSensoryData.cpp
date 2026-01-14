#include "OrganismSensoryData.h"
#include "core/Cell.h"
#include "core/World.h"
#include "core/WorldData.h"

namespace DirtSim {
namespace SensoryUtils {

template <int GridSize, int NumMaterials>
TemplateMatch findTemplate(
    const std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    const SensoryTemplate& template_pattern)
{
    for (int row = 0; row <= GridSize - template_pattern.height; ++row) {
        for (int col = 0; col <= GridSize - template_pattern.width; ++col) {
            if (matchesTemplate<GridSize, NumMaterials>(histograms, template_pattern, col, row)) {
                return TemplateMatch{ .found = true, .col = col, .row = row };
            }
        }
    }
    return TemplateMatch{ .found = false };
}

template <int GridSize, int NumMaterials>
bool matchesTemplate(
    const std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    const SensoryTemplate& template_pattern,
    int start_col,
    int start_row)
{
    constexpr double MATERIAL_THRESHOLD = 0.5;
    constexpr double EMPTY_THRESHOLD = 0.1;

    // Check each cell in the template pattern.
    for (int ty = 0; ty < template_pattern.height; ++ty) {
        for (int tx = 0; tx < template_pattern.width; ++tx) {
            int grid_row = start_row + ty;
            int grid_col = start_col + tx;

            // Bounds check.
            if (grid_row < 0 || grid_row >= GridSize || grid_col < 0 || grid_col >= GridSize) {
                return false;
            }

            const CellPattern& cell_pattern = template_pattern.pattern[ty][tx];
            const auto& cell_histogram = histograms[grid_row][grid_col];

            switch (cell_pattern.mode) {
                case MatchMode::Any:
                    continue;

                case MatchMode::IsEmpty: {
                    double total_fill = 0.0;
                    for (int m = 0; m < NumMaterials; ++m) {
                        total_fill += cell_histogram[m];
                    }
                    if (total_fill >= EMPTY_THRESHOLD) {
                        return false;
                    }
                    break;
                }

                case MatchMode::IsNotEmpty: {
                    double total_fill = 0.0;
                    for (int m = 0; m < NumMaterials; ++m) {
                        total_fill += cell_histogram[m];
                    }
                    if (total_fill < EMPTY_THRESHOLD) {
                        return false;
                    }
                    break;
                }

                case MatchMode::IsSolid: {
                    // Find dominant material and check if it's solid (not fluid).
                    Material::EnumType dominant = Material::EnumType::Air;
                    double max_fill = 0.0;
                    for (int m = 0; m < NumMaterials; ++m) {
                        if (cell_histogram[m] > max_fill) {
                            max_fill = cell_histogram[m];
                            dominant = static_cast<Material::EnumType>(m);
                        }
                    }
                    if (max_fill < MATERIAL_THRESHOLD) {
                        return false;
                    }
                    const Material::Properties& props = Material::getProperties(dominant);
                    if (props.is_fluid) {
                        return false;
                    }
                    break;
                }

                case MatchMode::IsLiquid: {
                    // Find dominant material and check if it's fluid.
                    Material::EnumType dominant = Material::EnumType::Air;
                    double max_fill = 0.0;
                    for (int m = 0; m < NumMaterials; ++m) {
                        if (cell_histogram[m] > max_fill) {
                            max_fill = cell_histogram[m];
                            dominant = static_cast<Material::EnumType>(m);
                        }
                    }
                    if (max_fill < MATERIAL_THRESHOLD) {
                        return false;
                    }
                    const Material::Properties& props = Material::getProperties(dominant);
                    if (!props.is_fluid) {
                        return false;
                    }
                    break;
                }

                case MatchMode::Is: {
                    // Must be one of the specified materials.
                    bool matched = false;
                    for (Material::EnumType mat : cell_pattern.materials) {
                        int mat_idx = static_cast<int>(mat);
                        if (mat_idx >= 0 && mat_idx < NumMaterials) {
                            if (cell_histogram[mat_idx] >= MATERIAL_THRESHOLD) {
                                matched = true;
                                break;
                            }
                        }
                    }
                    if (!matched) {
                        return false;
                    }
                    break;
                }

                case MatchMode::IsNot: {
                    // Must NOT be any of the specified materials.
                    for (Material::EnumType mat : cell_pattern.materials) {
                        int mat_idx = static_cast<int>(mat);
                        if (mat_idx >= 0 && mat_idx < NumMaterials) {
                            if (cell_histogram[mat_idx] >= MATERIAL_THRESHOLD) {
                                return false;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }

    return true;
}

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
            if (!data.inBounds(wx, wy)) {
                // Out of bounds - treat as WALL so organisms can detect world edges.
                int wall_idx = static_cast<int>(Material::EnumType::Wall);
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
Material::EnumType getDominantMaterial(
    const std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    int gx,
    int gy)
{
    if (gx < 0 || gx >= GridSize || gy < 0 || gy >= GridSize) {
        return Material::EnumType::Air;
    }

    double max_fill = 0.0;
    int max_idx = 0;
    for (int i = 0; i < NumMaterials; i++) {
        if (histograms[gy][gx][i] > max_fill) {
            max_fill = histograms[gy][gx][i];
            max_idx = i;
        }
    }
    return static_cast<Material::EnumType>(max_idx);
}

template <int GridSize, int NumMaterials>
bool isSolid(
    const std::array<std::array<std::array<double, NumMaterials>, GridSize>, GridSize>& histograms,
    int gx,
    int gy)
{
    Material::EnumType mat = getDominantMaterial<GridSize, NumMaterials>(histograms, gx, gy);
    return mat != Material::EnumType::Air && mat != Material::EnumType::Water;
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

template Material::EnumType getDominantMaterial<15, 10>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms, int gx, int gy);

template Material::EnumType getDominantMaterial<9, 10>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms, int gx, int gy);

template bool isSolid<15, 10>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms, int gx, int gy);

template bool isSolid<9, 10>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms, int gx, int gy);

template bool isEmpty<15, 10>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms, int gx, int gy);

template bool isEmpty<9, 10>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms, int gx, int gy);

template bool matchesTemplate<15, 10>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms,
    const SensoryTemplate& template_pattern,
    int start_col,
    int start_row);

template bool matchesTemplate<9, 10>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms,
    const SensoryTemplate& template_pattern,
    int start_col,
    int start_row);

template TemplateMatch findTemplate<15, 10>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms,
    const SensoryTemplate& template_pattern);

template TemplateMatch findTemplate<9, 10>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms,
    const SensoryTemplate& template_pattern);

} // namespace SensoryUtils
} // namespace DirtSim
