#include "OrganismSensoryData.h"
#include "core/Cell.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/water/WaterVolumeView.h"

#include <algorithm>

namespace DirtSim {
namespace SensoryUtils {

namespace {

bool tryGetUsableWaterVolumeView(
    const World& world, const WorldData& data, WaterVolumeView& waterView)
{
    return world.tryGetWaterVolumeView(waterView) && waterView.width == data.width
        && waterView.height == data.height;
}

float getWaterVolumeAt(const WaterVolumeView& waterView, int x, int y)
{
    constexpr float kWaterVolumeEpsilon = 0.0001f;

    if (x < 0 || y < 0 || x >= waterView.width || y >= waterView.height) {
        return 0.0f;
    }

    const size_t idx =
        static_cast<size_t>(y) * static_cast<size_t>(waterView.width) + static_cast<size_t>(x);
    if (idx >= waterView.volume.size()) {
        return 0.0f;
    }

    const float waterVolume = std::clamp(waterView.volume[idx], 0.0f, 1.0f);
    if (waterVolume <= kWaterVolumeEpsilon) {
        return 0.0f;
    }

    return waterVolume;
}

} // namespace

template <int GridSize, int NumMaterials, typename HistogramValueType>
TemplateMatch findTemplate(
    const std::array<std::array<std::array<HistogramValueType, NumMaterials>, GridSize>, GridSize>&
        histograms,
    const SensoryTemplate& template_pattern)
{
    for (int row = 0; row <= GridSize - template_pattern.height; ++row) {
        for (int col = 0; col <= GridSize - template_pattern.width; ++col) {
            if (matchesTemplate<GridSize, NumMaterials, HistogramValueType>(
                    histograms, template_pattern, col, row)) {
                return TemplateMatch{ .found = true, .col = col, .row = row };
            }
        }
    }
    return TemplateMatch{ .found = false };
}

template <int GridSize, int NumMaterials, typename HistogramValueType>
bool matchesTemplate(
    const std::array<std::array<std::array<HistogramValueType, NumMaterials>, GridSize>, GridSize>&
        histograms,
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

template <int GridSize, int NumMaterials, typename HistogramValueType>
void gatherMaterialHistograms(
    const World& world,
    Vector2i center,
    std::array<std::array<std::array<HistogramValueType, NumMaterials>, GridSize>, GridSize>&
        histograms,
    Vector2i& world_offset)
{
    const WorldData& data = world.getData();
    WaterVolumeView waterView{};
    const bool hasWaterVolume = tryGetUsableWaterVolumeView(world, data, waterView);

    int half_window = GridSize / 2;
    int offset_x = center.x - half_window;
    int offset_y = center.y - half_window;

    // Always center on organism - no clamping.
    // Out-of-bounds cells will be marked as WALL below.
    world_offset = Vector2i{ offset_x, offset_y };

    // Clear histograms.
    for (auto& row : histograms) {
        for (auto& cell : row) {
            cell.fill(static_cast<HistogramValueType>(0.0f));
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
                    histograms[ny][nx][wall_idx] = static_cast<HistogramValueType>(1.0f);
                }
                continue;
            }

            const Cell& cell = data.at(wx, wy);
            if (hasWaterVolume) {
                const float waterVolume = getWaterVolumeAt(waterView, wx, wy);
                if (waterVolume > 0.0f) {
                    const int waterIdx = static_cast<int>(Material::EnumType::Water);
                    if (waterIdx >= 0 && waterIdx < NumMaterials) {
                        histograms[ny][nx][waterIdx] = static_cast<HistogramValueType>(waterVolume);
                    }
                }
            }

            int material_idx = static_cast<int>(cell.material_type);
            if (material_idx >= 0 && material_idx < NumMaterials) {
                const HistogramValueType existingValue = histograms[ny][nx][material_idx];
                const HistogramValueType cellValue =
                    static_cast<HistogramValueType>(cell.fill_ratio);
                histograms[ny][nx][material_idx] = std::max(existingValue, cellValue);
            }
        }
    }
}

template <int GridSize, int NumMaterials, typename HistogramValueType>
Material::EnumType getDominantMaterial(
    const std::array<std::array<std::array<HistogramValueType, NumMaterials>, GridSize>, GridSize>&
        histograms,
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

template <int GridSize, int NumMaterials, typename HistogramValueType>
bool isSolid(
    const std::array<std::array<std::array<HistogramValueType, NumMaterials>, GridSize>, GridSize>&
        histograms,
    int gx,
    int gy)
{
    Material::EnumType mat =
        getDominantMaterial<GridSize, NumMaterials, HistogramValueType>(histograms, gx, gy);
    return mat != Material::EnumType::Air && mat != Material::EnumType::Water;
}

template <int GridSize, int NumMaterials, typename HistogramValueType>
bool isEmpty(
    const std::array<std::array<std::array<HistogramValueType, NumMaterials>, GridSize>, GridSize>&
        histograms,
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
template void gatherMaterialHistograms<15, 10, double>(
    const World& world,
    Vector2i center,
    std::array<std::array<std::array<double, 10>, 15>, 15>& histograms,
    Vector2i& world_offset);

template void gatherMaterialHistograms<21, 10, float>(
    const World& world,
    Vector2i center,
    std::array<std::array<std::array<float, 10>, 21>, 21>& histograms,
    Vector2i& world_offset);

template void gatherMaterialHistograms<9, 10, double>(
    const World& world,
    Vector2i center,
    std::array<std::array<std::array<double, 10>, 9>, 9>& histograms,
    Vector2i& world_offset);

template Material::EnumType getDominantMaterial<15, 10, double>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms, int gx, int gy);

template Material::EnumType getDominantMaterial<21, 10, float>(
    const std::array<std::array<std::array<float, 10>, 21>, 21>& histograms, int gx, int gy);

template Material::EnumType getDominantMaterial<9, 10, double>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms, int gx, int gy);

template bool isSolid<15, 10, double>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms, int gx, int gy);

template bool isSolid<21, 10, float>(
    const std::array<std::array<std::array<float, 10>, 21>, 21>& histograms, int gx, int gy);

template bool isSolid<9, 10, double>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms, int gx, int gy);

template bool isEmpty<15, 10, double>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms, int gx, int gy);

template bool isEmpty<21, 10, float>(
    const std::array<std::array<std::array<float, 10>, 21>, 21>& histograms, int gx, int gy);

template bool isEmpty<9, 10, double>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms, int gx, int gy);

template bool matchesTemplate<15, 10, double>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms,
    const SensoryTemplate& template_pattern,
    int start_col,
    int start_row);

template bool matchesTemplate<21, 10, float>(
    const std::array<std::array<std::array<float, 10>, 21>, 21>& histograms,
    const SensoryTemplate& template_pattern,
    int start_col,
    int start_row);

template bool matchesTemplate<9, 10, double>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms,
    const SensoryTemplate& template_pattern,
    int start_col,
    int start_row);

template TemplateMatch findTemplate<15, 10, double>(
    const std::array<std::array<std::array<double, 10>, 15>, 15>& histograms,
    const SensoryTemplate& template_pattern);

template TemplateMatch findTemplate<21, 10, float>(
    const std::array<std::array<std::array<float, 10>, 21>, 21>& histograms,
    const SensoryTemplate& template_pattern);

template TemplateMatch findTemplate<9, 10, double>(
    const std::array<std::array<std::array<double, 10>, 9>, 9>& histograms,
    const SensoryTemplate& template_pattern);

} // namespace SensoryUtils
} // namespace DirtSim
