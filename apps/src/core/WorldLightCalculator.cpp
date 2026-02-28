#include "WorldLightCalculator.h"
#include "Assert.h"
#include "Cell.h"
#include "ColorNames.h"
#include "GridOfCells.h"
#include "LightConfig.h"
#include "LightManager.h"
#include "MaterialType.h"
#include "ScopeTimer.h"
#include "Timers.h"
#include "Vector2.h"
#include "World.h"
#include "WorldData.h"

#include <cmath>
#include <vector>

namespace DirtSim {

void WorldLightCalculator::calculate(
    World& world, const GridOfCells& grid, const LightConfig& config, Timers& timers)
{
    auto& data = world.getData();

    // Ensure colors buffer is sized correctly.
    if (data.colors.width != data.width || data.colors.height != data.height) {
        data.colors.resize(data.width, data.height, ColorNames::RgbF{});
    }

    // Ensure emissive overlay is sized correctly.
    if (emissive_overlay_.width != data.width || emissive_overlay_.height != data.height) {
        emissive_overlay_.resize(data.width, data.height, ColorNames::RgbF{});
    }

    // Clear to black before accumulating light.
    {
        ScopeTimer t(timers, "light_clear");
        clearLight(world);
    }

    // Add ambient light (with optional sky access attenuation).
    {
        ScopeTimer t(timers, "light_ambient");
        applyAmbient(world, grid, config);
        ambient_boost_ = {};
    }

    // Add sunlight (top-down).
    if (config.sun_enabled) {
        ScopeTimer t(timers, "light_sunlight");
        applySunlight(world, grid, config.sun_color, config.sun_intensity);
    }

    // Add emissive material contributions.
    {
        ScopeTimer t(timers, "light_emissive");
        applyEmissiveCells(world);
    }

    // Add scenario-controlled emissive overlay.
    {
        ScopeTimer t(timers, "light_emissive_overlay");
        applyEmissiveOverlay(world);
    }

    // Add point light contributions.
    {
        ScopeTimer t(timers, "light_point_lights");
        applyPointLights(world, grid);
    }

    {
        ScopeTimer t(timers, "light_diffusion");
        applyDiffusion(
            world,
            grid,
            config.diffusion_iterations,
            config.diffusion_rate,
            config.air_scatter_rate);
    }

    {
        ScopeTimer t(timers, "light_store_raw");
        storeRawLight(world);
    }

    {
        ScopeTimer t(timers, "light_material_colors");
        applyMaterialColors(world);
    }
}

void WorldLightCalculator::clearLight(World& world)
{
    auto& data = world.getData();
    data.colors.clear(ColorNames::RgbF{});
}

void WorldLightCalculator::applyAmbient(
    World& world, const GridOfCells& grid, const LightConfig& config)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    auto& data = world.getData();
    const RgbF base_ambient =
        toRgbF(config.ambient_color) * config.ambient_intensity + ambient_boost_;

    if (!config.sky_access_enabled) {
        // Simple uniform ambient - parallelize.
        const size_t count = data.colors.size();
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (GridOfCells::USE_OPENMP && count >= 2500)
#endif
        for (size_t i = 0; i < count; ++i) {
            data.colors.data[i] += base_ambient;
        }
        return;
    }

    const int width = data.width;
    const int height = data.height;
    const float falloff = config.sky_access_falloff;

    if (config.sky_access_multi_directional) {
        // Prefix-scan implementation: O(W×H) instead of O(W×H×3×ProbeSteps).
        //
        // For each probe direction, the transmittance at (x, y) is the product of
        // per-cell attenuation factors along the probe path from row 0 down to row y.
        // That product satisfies a prefix-product recurrence that propagates row by row:
        //
        //   attenuate(x, y) = clamp(1 - opacity(x,y) * fill(x,y) * falloff, 0, 1)
        //
        //   tV[x][y]   = tV[x][y-1]     * attenuate(x,   y-1)   // straight up
        //   tUL[x][y]  = tUL[x-1][y-1]  * attenuate(x-1, y-1)   // upper-left diagonal
        //   tUR[x][y]  = tUR[x+1][y-1]  * attenuate(x+1, y-1)   // upper-right diagonal
        //
        // Boundary conditions (probe exits the world = full sky):
        //   tV[x][0] = tUL[x][0] = tUR[x][0] = 1.0  (top row: look up, exit immediately)
        //   tUL[0][y] = 1.0  (left column: UL probe exits world left)
        //   tUR[W-1][y] = 1.0  (right column: UR probe exits world right)
        //
        // Within each row, all x values are independent → safe to parallelize with OMP.

        // Helper: single-cell attenuation factor used in the prefix scan.
        auto attenuate = [falloff](const WorldData& d, int x, int y) -> float {
            const Cell& cell = d.cells[static_cast<size_t>(y) * d.width + static_cast<size_t>(x)];
            const float opacity = Material::getProperties(cell.material_type).light.opacity;
            const float a = 1.0f - opacity * cell.fill_ratio * falloff;
            return (a < 0.0f) ? 0.0f : (a > 1.0f ? 1.0f : a);
        };

        // Two rows of transmittance buffers (prev and curr), each holding tV/tUL/tUR.
        std::vector<float> prev_tV(width, 1.0f);
        std::vector<float> prev_tUL(width, 1.0f);
        std::vector<float> prev_tUR(width, 1.0f);
        std::vector<float> curr_tV(width);
        std::vector<float> curr_tUL(width);
        std::vector<float> curr_tUR(width);

        // Row 0: all transmittances start at 1.0 (probes exit top immediately).
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
        for (int x = 0; x < width; ++x) {
            data.colors.at(x, 0) += base_ambient; // sky_factor = 1.0
        }

        // Rows 1 … height-1: propagate prefix products from the previous row.
        for (int y = 1; y < height; ++y) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
            for (int x = 0; x < width; ++x) {
                // tV: straight upward — depends on cell directly above.
                curr_tV[x] = prev_tV[x] * attenuate(data, x, y - 1);

                // tUL: upper-left diagonal — depends on cell at (x-1, y-1).
                curr_tUL[x] = (x == 0) ? 1.0f : prev_tUL[x - 1] * attenuate(data, x - 1, y - 1);

                // tUR: upper-right diagonal — depends on cell at (x+1, y-1).
                curr_tUR[x] =
                    (x == width - 1) ? 1.0f : prev_tUR[x + 1] * attenuate(data, x + 1, y - 1);

                const float sky_factor =
                    0.5f * curr_tV[x] + 0.25f * curr_tUL[x] + 0.25f * curr_tUR[x];
                data.colors.at(x, y) += base_ambient * sky_factor;
            }

            prev_tV.swap(curr_tV);
            prev_tUL.swap(curr_tUL);
            prev_tUR.swap(curr_tUR);
        }
        return;
    }

    // Helper to apply ambient with sky attenuation for a single cell.
    // Opacity scales with fill ratio - partially filled cells are more transparent.
    auto processCell = [&data, &base_ambient, falloff, width](
                           Material::EnumType mat, int x, int y, float& sky_factor) {
        data.colors.at(x, y) += base_ambient * sky_factor;
        const Cell& cell = data.cells[static_cast<size_t>(y) * width + x];
        const float fill = cell.fill_ratio;
        const float base_opacity = Material::getProperties(mat).light.opacity;
        const float effective_opacity = base_opacity * fill;
        sky_factor *= (1.0f - effective_opacity * falloff);
        if (sky_factor < 0.0f) {
            sky_factor = 0.0f;
        }
    };

    // Sky access attenuation: ambient diminishes with depth based on opacity above.
    // Use material neighborhood cache to process 3 cells per lookup.
    // Columns are independent - parallelize with OpenMP.
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
    for (int x = 0; x < width; ++x) {
        float sky_factor = 1.0f;

        // Process row 0 separately (top edge).
        {
            const uint64_t packed = grid.getMaterialNeighborhood(x, 0).raw();
            const Material::EnumType mat = static_cast<Material::EnumType>((packed >> 16) & 0xF);
            processCell(mat, x, 0, sky_factor);
        }

        // Process 3 cells at a time using the column from each neighborhood lookup.
        int y = 1; // Loop variable, incremented.
        for (; y + 2 < height; y += 3) {
            const uint64_t packed = grid.getMaterialNeighborhood(x, y + 1).raw();
            const Material::EnumType mat0 =
                static_cast<Material::EnumType>((packed >> 4) & 0xF); // y
            const Material::EnumType mat1 =
                static_cast<Material::EnumType>((packed >> 16) & 0xF); // y+1
            const Material::EnumType mat2 =
                static_cast<Material::EnumType>((packed >> 28) & 0xF); // y+2

            processCell(mat0, x, y, sky_factor);
            processCell(mat1, x, y + 1, sky_factor);
            processCell(mat2, x, y + 2, sky_factor);
        }

        // Handle remaining rows.
        for (; y < height; ++y) {
            const uint64_t packed = grid.getMaterialNeighborhood(x, y).raw();
            const Material::EnumType mat = static_cast<Material::EnumType>((packed >> 16) & 0xF);
            processCell(mat, x, y, sky_factor);
        }
    }
}

void WorldLightCalculator::applySunlight(
    World& world, const GridOfCells& grid, uint32_t sun_color, float intensity)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    auto& data = world.getData();
    const RgbF scaled_sun = toRgbF(sun_color) * intensity;
    const int width = data.width;
    const int height = data.height;
    const RgbF white{ 1.0f, 1.0f, 1.0f };

    // Helper to apply sunlight attenuation for a single cell.
    // Opacity and tinting scale with fill ratio - partially filled cells are more transparent.
    auto processCell = [&data, &white, width](Material::EnumType mat, int x, int y, RgbF& sun) {
        data.colors.at(x, y) += sun;
        const auto& light_props = Material::getProperties(mat).light;
        const Cell& cell = data.cells[static_cast<size_t>(y) * width + x];
        const float fill = cell.fill_ratio;

        // Scale opacity by fill ratio - a 50% filled cell blocks 50% as much light.
        const float effective_opacity = light_props.opacity * fill;
        const float transmittance = 1.0f - effective_opacity;
        sun *= transmittance;

        // Scale tinting by fill ratio - a 50% filled cell tints 50% as much.
        const RgbF base_tint = toRgbF(light_props.tint);
        const RgbF effective_tint = ColorNames::lerp(white, base_tint, fill);
        sun *= effective_tint;
    };

    // Cast sunlight from top of world downward per column.
    // Use material neighborhood cache to process 3 cells per lookup.
    // Columns are independent - parallelize with OpenMP.
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
    for (int x = 0; x < width; ++x) {
        RgbF sun = scaled_sun;

        // Process row 0 separately (top edge).
        {
            const uint64_t packed = grid.getMaterialNeighborhood(x, 0).raw();
            const Material::EnumType mat = static_cast<Material::EnumType>((packed >> 16) & 0xF);
            processCell(mat, x, 0, sun);
        }

        // Process 3 cells at a time using the column from each neighborhood lookup.
        // Neighborhood at (x, y) contains: y-1 at bits 4-7, y at bits 16-19, y+1 at bits 28-31.
        int y = 1; // Loop variable, incremented.
        for (; y + 2 < height; y += 3) {
            // Fetch neighborhood centered at y+1 to get y, y+1, y+2.
            const uint64_t packed = grid.getMaterialNeighborhood(x, y + 1).raw();
            const Material::EnumType mat0 =
                static_cast<Material::EnumType>((packed >> 4) & 0xF); // y
            const Material::EnumType mat1 =
                static_cast<Material::EnumType>((packed >> 16) & 0xF); // y+1
            const Material::EnumType mat2 =
                static_cast<Material::EnumType>((packed >> 28) & 0xF); // y+2

            processCell(mat0, x, y, sun);
            processCell(mat1, x, y + 1, sun);
            processCell(mat2, x, y + 2, sun);
        }

        // Handle remaining rows (0-2 cells).
        for (; y < height; ++y) {
            const uint64_t packed = grid.getMaterialNeighborhood(x, y).raw();
            const Material::EnumType mat = static_cast<Material::EnumType>((packed >> 16) & 0xF);
            processCell(mat, x, y, sun);
        }
    }
}

void WorldLightCalculator::applyEmissiveCells(World& world)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    auto& data = world.getData();
    const int width = data.width;
    const int height = data.height;

#ifdef _OPENMP
#pragma omp parallel for collapse(2) \
    schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Cell& cell = data.cells[static_cast<size_t>(y) * width + x];
            const auto& light_props = cell.material().light;

            if (light_props.emission > 0.0f) {
                const RgbF emitted = toRgbF(light_props.emission_color) * light_props.emission;
                data.colors.at(x, y) += emitted;
            }
        }
    }
}

void WorldLightCalculator::applyDiffusion(
    World& world, const GridOfCells& grid, int iterations, float rate, float air_scatter_rate)
{
    using ColorNames::RgbF;

    if (iterations <= 0 || rate <= 0.0f) {
        return;
    }

    auto& data = world.getData();
    const size_t cell_count = static_cast<size_t>(data.width) * data.height;
    light_buffer_.resize(cell_count);

    const CellBitmap& empty = grid.emptyCells();
    const int width = data.width;
    const int height = data.height;

    for (int iter = 0; iter < iterations; ++iter) {
        std::copy(data.colors.begin(), data.colors.end(), light_buffer_.begin());

        // Within each iteration, cells are independent (read from buffer, write to colors).
#ifdef _OPENMP
#pragma omp parallel for collapse(2) \
    schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
        for (int y = 1; y < height - 1; ++y) {
            for (int x = 1; x < width - 1; ++x) {
                float scatter;

                if (empty.isSet(x, y)) {
                    // AIR cell - use air scattering if enabled (rate > 0).
                    if (air_scatter_rate > 0.0f) {
                        scatter = air_scatter_rate;
                    }
                    else {
                        continue;
                    }
                }
                else {
                    // Get material from neighborhood cache instead of Cell struct.
                    const uint64_t packed = grid.getMaterialNeighborhood(x, y).raw();
                    const Material::EnumType mat =
                        static_cast<Material::EnumType>((packed >> 16) & 0xF);
                    scatter = Material::getProperties(mat).light.scatter;

                    if (scatter <= 0.0f) {
                        continue;
                    }
                }

                const size_t idx = static_cast<size_t>(y) * width + x;

                // Cardinal neighbors (distance 1.0, weight 1.0).
                const RgbF& up = light_buffer_[static_cast<size_t>(y - 1) * width + x];
                const RgbF& down = light_buffer_[static_cast<size_t>(y + 1) * width + x];
                const RgbF& left = light_buffer_[static_cast<size_t>(y) * width + (x - 1)];
                const RgbF& right = light_buffer_[static_cast<size_t>(y) * width + (x + 1)];

                // Diagonal neighbors (distance sqrt(2), weight 1/sqrt(2) ≈ 0.707).
                const RgbF& nw = light_buffer_[static_cast<size_t>(y - 1) * width + (x - 1)];
                const RgbF& ne = light_buffer_[static_cast<size_t>(y - 1) * width + (x + 1)];
                const RgbF& sw = light_buffer_[static_cast<size_t>(y + 1) * width + (x - 1)];
                const RgbF& se = light_buffer_[static_cast<size_t>(y + 1) * width + (x + 1)];

                // Weighted average: 4 cardinals × 1.0 + 4 diagonals × 0.707 ≈ 6.828.
                constexpr float diag_weight = 0.7071067811865476f; // 1/sqrt(2)
                constexpr float total_weight = 4.0f + 4.0f * diag_weight;
                constexpr float inv_total = 1.0f / total_weight;

                const RgbF neighbor_avg{
                    (up.r + down.r + left.r + right.r + diag_weight * (nw.r + ne.r + sw.r + se.r))
                        * inv_total,
                    (up.g + down.g + left.g + right.g + diag_weight * (nw.g + ne.g + sw.g + se.g))
                        * inv_total,
                    (up.b + down.b + left.b + right.b + diag_weight * (nw.b + ne.b + sw.b + se.b))
                        * inv_total
                };

                const float blend = scatter * rate;
                data.colors.data[idx] = ColorNames::lerp(light_buffer_[idx], neighbor_avg, blend);
            }
        }
    }
}

namespace {

ColorNames::RgbF getMaterialBaseColor(Material::EnumType mat)
{
    using ColorNames::toRgbF;
    switch (mat) {
        case Material::EnumType::Air:
            return toRgbF(ColorNames::white());
        case Material::EnumType::Dirt:
            return toRgbF(ColorNames::dirt());
        case Material::EnumType::Leaf:
            return toRgbF(ColorNames::leaf());
        case Material::EnumType::Metal:
            return toRgbF(ColorNames::metal());
        case Material::EnumType::Root:
            return toRgbF(ColorNames::root());
        case Material::EnumType::Sand:
            return toRgbF(ColorNames::sand());
        case Material::EnumType::Seed:
            return toRgbF(ColorNames::seed());
        case Material::EnumType::Wall:
            return toRgbF(ColorNames::stone());
        case Material::EnumType::Water:
            return toRgbF(ColorNames::water());
        case Material::EnumType::Wood:
            return toRgbF(ColorNames::wood());
        default:
            return ColorNames::RgbF{ 1.0f, 1.0f, 1.0f };
    }
}

} // namespace

void WorldLightCalculator::applyMaterialColors(World& world)
{
    using ColorNames::RgbF;

    auto& data = world.getData();
    const int width = data.width;
    const int height = data.height;
    const RgbF white{ 1.0f, 1.0f, 1.0f };

#ifdef _OPENMP
#pragma omp parallel for collapse(2) \
    schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Cell& cell = data.cells[static_cast<size_t>(y) * width + x];
            const Material::EnumType mat = cell.getRenderMaterial();
            const float saturation = Material::getProperties(mat).light.saturation;
            const RgbF base_color = getMaterialBaseColor(mat);

            // Blend toward base color based on saturation.
            const RgbF blended = ColorNames::lerp(white, base_color, saturation);
            data.colors.at(x, y) *= blended;
        }
    }
}

std::string WorldLightCalculator::lightMapString(const World& world) const
{
    const char* shades = " .:-=+*#%@"; // 10 levels, dark to bright.
    const auto& data = world.getData();
    std::string result;
    result.reserve((data.width + 1) * data.height);

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const Cell& cell = data.at(x, y);
            const float opacity = cell.material().light.opacity;
            if (opacity > 0.5f) {
                result += 'X';
            }
            else {
                const float b = ColorNames::brightness(data.colors.at(x, y));
                const int idx = std::min(9, static_cast<int>(b * 10));
                result += shades[idx];
            }
        }
        result += '\n';
    }
    return result;
}

void WorldLightCalculator::storeRawLight(World& world)
{
    auto& data = world.getData();
    if (raw_light_.width != data.width || raw_light_.height != data.height) {
        raw_light_.resize(data.width, data.height);
    }

    // Pack RgbF to uint32_t for entity lighting.
    const auto* src = data.colors.begin();
    uint32_t* dst = raw_light_.begin();
    for (size_t i = 0; i < data.colors.size(); ++i) {
        dst[i] = ColorNames::toRgba(src[i]);
    }
}

const LightBuffer& WorldLightCalculator::getRawLightBuffer() const
{
    return raw_light_;
}

void WorldLightCalculator::setAmbientBoost(ColorNames::RgbF boost)
{
    ambient_boost_ = boost;
}

void WorldLightCalculator::setEmissive(int x, int y, uint32_t color, float intensity)
{
    DIRTSIM_ASSERT(
        x >= 0 && y >= 0 && x < emissive_overlay_.width && y < emissive_overlay_.height,
        "Out of bounds");
    emissive_overlay_.at(x, y) = ColorNames::toRgbF(color) * intensity;
}

void WorldLightCalculator::clearEmissive(int x, int y)
{
    DIRTSIM_ASSERT(
        x >= 0 && y >= 0 && x < emissive_overlay_.width && y < emissive_overlay_.height,
        "Out of bounds");
    emissive_overlay_.at(x, y) = ColorNames::RgbF{};
}

void WorldLightCalculator::clearAllEmissive()
{
    emissive_overlay_.clear(ColorNames::RgbF{});
}

void WorldLightCalculator::resize(int width, int height)
{
    DIRTSIM_ASSERT(width >= 0 && height >= 0, "Dimensions must be non-negative");
    if (emissive_overlay_.width != width || emissive_overlay_.height != height) {
        emissive_overlay_.resize(width, height, ColorNames::RgbF{});
    }
    if (raw_light_.width != width || raw_light_.height != height) {
        raw_light_.resize(width, height);
    }
}

void WorldLightCalculator::applyEmissiveOverlay(World& world)
{
    auto& data = world.getData();
    resize(data.width, data.height);

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const ColorNames::RgbF& emission = emissive_overlay_.at(x, y);
            if (emission.r > 0.0f || emission.g > 0.0f || emission.b > 0.0f) {
                data.colors.at(x, y) += emission;
            }
        }
    }
}

ColorNames::RgbF WorldLightCalculator::traceRay(
    const ColorNames::RgbF* optical,
    int width,
    int height,
    float x0,
    float y0,
    int x1,
    int y1,
    ColorNames::RgbF color) const
{
    // DDA grid traversal using raw (unnormalized) direction — normalization is not
    // required because only the tMaxX vs tMaxY comparison matters, and scaling both
    // by the same factor preserves the ordering.
    const float dx = (static_cast<float>(x1) + 0.5f) - x0;
    const float dy = (static_cast<float>(y1) + 0.5f) - y0;

    // Degenerate: source is inside the target cell.
    if (std::abs(dx) < 0.001f && std::abs(dy) < 0.001f) {
        return color;
    }

    // Nudge start off grid boundaries to avoid traversal artefacts.
    constexpr float EPSILON = 1e-5f;
    const float x0_adj = x0 + (dx >= 0.0f ? EPSILON : -EPSILON);
    const float y0_adj = y0 + (dy >= 0.0f ? EPSILON : -EPSILON);

    int cell_x = static_cast<int>(std::floor(x0_adj));
    int cell_y = static_cast<int>(std::floor(y0_adj));

    const int step_x = (dx > 0.0f) ? 1 : -1;
    const int step_y = (dy > 0.0f) ? 1 : -1;

    const float tDeltaX = (dx != 0.0f) ? std::abs(1.0f / dx) : 1e9f;
    const float tDeltaY = (dy != 0.0f) ? std::abs(1.0f / dy) : 1e9f;

    const float tMaxX = (dx > 0.0f) ? (std::floor(x0_adj) + 1.0f - x0_adj) / dx
        : (dx < 0.0f)               ? (x0_adj - std::floor(x0_adj)) / -dx
                                    : 1e9f;
    const float tMaxY = (dy > 0.0f) ? (std::floor(y0_adj) + 1.0f - y0_adj) / dy
        : (dy < 0.0f)               ? (y0_adj - std::floor(y0_adj)) / -dy
                                    : 1e9f;

    float tCurX = tMaxX;
    float tCurY = tMaxY;

    // Geometric bound: at most |Δx|+|Δy|+2 cell crossings to reach target.
    const int max_steps = std::abs(x1 - cell_x) + std::abs(y1 - cell_y) + 2;
    for (int step = 0; step < max_steps; ++step) {
        if (cell_x == x1 && cell_y == y1) {
            break;
        }

        if (cell_x < 0 || cell_x >= width || cell_y < 0 || cell_y >= height) {
            return ColorNames::RgbF{};
        }

        // Single multiply from precomputed optical buffer replaces material lookup,
        // opacity/fill math, tint decode, and lerp.
        color *= optical[static_cast<size_t>(cell_y) * width + cell_x];

        if (color.r < 0.001f && color.g < 0.001f && color.b < 0.001f) {
            return ColorNames::RgbF{};
        }

        if (tCurX < tCurY) {
            tCurX += tDeltaX;
            cell_x += step_x;
        }
        else {
            tCurY += tDeltaY;
            cell_y += step_y;
        }
    }

    return color;
}

void WorldLightCalculator::applyPointLight(const PointLight& light, World& world)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    auto& data = world.getData();
    const int width = data.width;
    const int height = data.height;

    // Keep sub-cell precision for light position.
    const float light_x = light.position.x;
    const float light_y = light.position.y;

    // Bounds check uses truncated position for grid validity.
    const int light_cell_x = static_cast<int>(light_x);
    const int light_cell_y = static_cast<int>(light_y);
    if (light_cell_x < 0 || light_cell_x >= width || light_cell_y < 0 || light_cell_y >= height) {
        return;
    }

    const int radius_int = static_cast<int>(std::ceil(light.radius));
    const float radius_sq = light.radius * light.radius;
    const RgbF light_color = toRgbF(light.color) * light.intensity;
    const RgbF* optical = optical_buffer_.data.data();

    const int min_x = std::max(0, light_cell_x - radius_int);
    const int max_x = std::min(width - 1, light_cell_x + radius_int);
    const int min_y = std::max(0, light_cell_y - radius_int);
    const int max_y = std::min(height - 1, light_cell_y + radius_int);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) - light_x;
            const float dy = (static_cast<float>(y) + 0.5f) - light_y;
            const float dist_sq = dx * dx + dy * dy;

            if (dist_sq > radius_sq) {
                continue;
            }

            const float falloff = 1.0f / (1.0f + dist_sq * light.attenuation);
            const RgbF received =
                traceRay(optical, width, height, light_x, light_y, x, y, light_color);
            data.colors.at(x, y) += received * falloff;
        }
    }
}

void WorldLightCalculator::applySpotLight(const SpotLight& light, World& world)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    auto& data = world.getData();
    const int width = data.width;
    const int height = data.height;

    // Keep sub-cell precision for light position.
    const float light_x = light.position.x;
    const float light_y = light.position.y;

    // Bounds check uses truncated position for grid validity.
    const int light_cell_x = static_cast<int>(light_x);
    const int light_cell_y = static_cast<int>(light_y);
    if (light_cell_x < 0 || light_cell_x >= width || light_cell_y < 0 || light_cell_y >= height) {
        return;
    }

    const int radius_int = static_cast<int>(std::ceil(light.radius));
    const float radius_sq = light.radius * light.radius;
    const RgbF light_color = toRgbF(light.color) * light.intensity;
    const RgbF* optical = optical_buffer_.data.data();

    const int min_x = std::max(0, light_cell_x - radius_int);
    const int max_x = std::min(width - 1, light_cell_x + radius_int);
    const int min_y = std::max(0, light_cell_y - radius_int);
    const int max_y = std::min(height - 1, light_cell_y + radius_int);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) - light_x;
            const float dy = (static_cast<float>(y) + 0.5f) - light_y;
            const float dist_sq = dx * dx + dy * dy;

            if (dist_sq > radius_sq) {
                continue;
            }

            // Angular factor uses sub-cell positions for smooth spotlight direction.
            const float angular_factor = getSpotAngularFactor(
                Vector2f{ light_x, light_y },
                light.direction,
                light.arc_width,
                light.focus,
                Vector2f{ static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f });
            if (angular_factor <= 0.0f) {
                continue;
            }

            const float falloff = angular_factor / (1.0f + dist_sq * light.attenuation);
            const RgbF received =
                traceRay(optical, width, height, light_x, light_y, x, y, light_color);
            data.colors.at(x, y) += received * falloff;
        }
    }
}

void WorldLightCalculator::applyRotatingLight(const RotatingLight& light, World& world)
{
    applySpotLight(
        SpotLight{ .position = light.position,
                   .color = light.color,
                   .intensity = light.intensity,
                   .radius = light.radius,
                   .attenuation = light.attenuation,
                   .direction = light.direction,
                   .arc_width = light.arc_width,
                   .focus = light.focus },
        world);
}

float WorldLightCalculator::getSpotAngularFactor(
    const Vector2f& light_pos,
    float direction,
    float arc_width,
    float focus,
    const Vector2f& target_pos) const
{
    const Vector2f to_target = target_pos - light_pos;
    const float target_angle = std::atan2(to_target.y, to_target.x);

    float angle_diff = target_angle - direction;
    while (angle_diff > M_PI) {
        angle_diff -= 2.0f * static_cast<float>(M_PI);
    }
    while (angle_diff < -M_PI) {
        angle_diff += 2.0f * static_cast<float>(M_PI);
    }

    const float half_arc = arc_width / 2.0f;
    const float abs_diff = std::abs(angle_diff);
    if (abs_diff > half_arc) {
        return 0.0f;
    }

    // Normalized angle: 0 at center, 1 at edge.
    const float norm_angle = abs_diff / half_arc;
    // focus=0 gives uniform, higher values concentrate toward center.
    return std::pow(1.0f - norm_angle, focus);
}

void WorldLightCalculator::buildOpticalBuffer(const WorldData& data)
{
    using ColorNames::lerp;
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    if (optical_buffer_.width != data.width || optical_buffer_.height != data.height) {
        optical_buffer_.resize(data.width, data.height);
    }

    const RgbF white{ 1.0f, 1.0f, 1.0f };
    const size_t count = static_cast<size_t>(data.width) * data.height;

    for (size_t i = 0; i < count; ++i) {
        const Cell& cell = data.cells[i];
        const auto& lp = Material::getProperties(cell.material_type).light;
        const float fill = cell.fill_ratio;
        // Combine transmittance and tint into a single per-cell RgbF multiplier.
        // Ray steps reduce to: color *= optical[idx].
        optical_buffer_.data[i] = lerp(white, toRgbF(lp.tint), fill) * (1.0f - lp.opacity * fill);
    }
}

void WorldLightCalculator::applyPointLights(World& world, const GridOfCells& /*grid*/)
{
    const LightManager& lights = world.getLightManager();
    if (lights.count() == 0) {
        return;
    }

    // Build per-cell optical multiplier once for all ray traces this frame.
    buildOpticalBuffer(world.getData());

    lights.forEachLight([&](LightId, const Light& light) {
        std::visit(
            [&](const auto& typed_light) {
                using T = std::decay_t<decltype(typed_light)>;
                if constexpr (std::is_same_v<T, PointLight>) {
                    applyPointLight(typed_light, world);
                }
                else if constexpr (std::is_same_v<T, SpotLight>) {
                    applySpotLight(typed_light, world);
                }
                else if constexpr (std::is_same_v<T, RotatingLight>) {
                    applyRotatingLight(typed_light, world);
                }
            },
            light.getVariant());
    });
}

} // namespace DirtSim
