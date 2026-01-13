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
#include "World.h"
#include "WorldData.h"

#include <cmath>

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
    const RgbF base_ambient = toRgbF(config.ambient_color) * config.ambient_intensity;

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
        case Material::EnumType::AIR:
            return toRgbF(ColorNames::white());
        case Material::EnumType::DIRT:
            return toRgbF(ColorNames::dirt());
        case Material::EnumType::LEAF:
            return toRgbF(ColorNames::leaf());
        case Material::EnumType::METAL:
            return toRgbF(ColorNames::metal());
        case Material::EnumType::ROOT:
            return toRgbF(ColorNames::root());
        case Material::EnumType::SAND:
            return toRgbF(ColorNames::sand());
        case Material::EnumType::SEED:
            return toRgbF(ColorNames::seed());
        case Material::EnumType::WALL:
            return toRgbF(ColorNames::stone());
        case Material::EnumType::WATER:
            return toRgbF(ColorNames::water());
        case Material::EnumType::WOOD:
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
            const float opacity = Material::getProperties(mat).light.opacity;
            const RgbF base_color = getMaterialBaseColor(mat);

            // Blend toward base color based on opacity.
            // Transparent materials (low opacity) stay closer to pure light color.
            // Opaque materials get full material coloring.
            const RgbF blended = ColorNames::lerp(white, base_color, opacity);
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
            const float b = ColorNames::brightness(data.colors.at(x, y));
            const int idx = std::min(9, static_cast<int>(b * 10));
            result += shades[idx];
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
    const GridOfCells& grid,
    const WorldData& data,
    int x0,
    int y0,
    int x1,
    int y1,
    ColorNames::RgbF color) const
{
    // Bresenham's line algorithm to trace from light source to target.
    // Accumulates opacity and tinting as light passes through materials.
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy; // Modified during line stepping.

    int x = x0; // Current position, modified during stepping.
    int y = y0;
    const int width = grid.getWidth();
    const int height = grid.getHeight();
    const ColorNames::RgbF white{ 1.0f, 1.0f, 1.0f };

    // Skip the source cell itself.
    while (x != x1 || y != y1) {
        const int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }

        // Stop if we've reached the target.
        if (x == x1 && y == y1) {
            break;
        }

        // Bounds check - if we step outside the grid, stop tracing.
        if (x < 0 || x >= width || y < 0 || y >= height) {
            return ColorNames::RgbF{};
        }

        // Get material and fill ratio at this cell.
        const uint64_t packed = grid.getMaterialNeighborhood(x, y).raw();
        const Material::EnumType mat = static_cast<Material::EnumType>((packed >> 16) & 0xF);
        const auto& light_props = Material::getProperties(mat).light;
        const Cell& cell = data.cells[static_cast<size_t>(y) * width + x];
        const float fill = cell.fill_ratio;

        // Scale opacity by fill ratio - partially filled cells are more transparent.
        const float effective_opacity = light_props.opacity * fill;
        const float transmittance = 1.0f - effective_opacity;
        color *= transmittance;

        // Scale tinting by fill ratio - partially filled cells tint less.
        const ColorNames::RgbF base_tint = ColorNames::toRgbF(light_props.tint);
        const ColorNames::RgbF effective_tint = ColorNames::lerp(white, base_tint, fill);
        color *= effective_tint;

        // Early exit if light is fully absorbed.
        if (color.r < 0.001f && color.g < 0.001f && color.b < 0.001f) {
            return ColorNames::RgbF{};
        }
    }

    return color;
}

void WorldLightCalculator::applyPointLight(
    const PointLight& light, World& world, const GridOfCells& grid)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    auto& data = world.getData();
    const int width = data.width;
    const int height = data.height;

    const int light_x = static_cast<int>(light.position.x);
    const int light_y = static_cast<int>(light.position.y);

    if (light_x < 0 || light_x >= width || light_y < 0 || light_y >= height) {
        return;
    }

    const int radius_int = static_cast<int>(std::ceil(light.radius));
    const RgbF light_color = toRgbF(light.color) * light.intensity;

    const int min_x = std::max(0, light_x - radius_int);
    const int max_x = std::min(width - 1, light_x + radius_int);
    const int min_y = std::max(0, light_y - radius_int);
    const int max_y = std::min(height - 1, light_y + radius_int);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = static_cast<float>(x - light_x);
            const float dy = static_cast<float>(y - light_y);
            const float dist = std::sqrt(dx * dx + dy * dy);

            if (dist > light.radius) {
                continue;
            }

            const float falloff = 1.0f / (1.0f + dist * dist * light.attenuation);
            const RgbF received = traceRay(grid, data, light_x, light_y, x, y, light_color);
            data.colors.at(x, y) += received * falloff;
        }
    }
}

void WorldLightCalculator::applySpotLight(
    const SpotLight& light, World& world, const GridOfCells& grid)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    auto& data = world.getData();
    const int width = data.width;
    const int height = data.height;

    const int light_x = static_cast<int>(light.position.x);
    const int light_y = static_cast<int>(light.position.y);

    if (light_x < 0 || light_x >= width || light_y < 0 || light_y >= height) {
        return;
    }

    const int radius_int = static_cast<int>(std::ceil(light.radius));
    const RgbF light_color = toRgbF(light.color) * light.intensity;

    const int min_x = std::max(0, light_x - radius_int);
    const int max_x = std::min(width - 1, light_x + radius_int);
    const int min_y = std::max(0, light_y - radius_int);
    const int max_y = std::min(height - 1, light_y + radius_int);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = static_cast<float>(x - light_x);
            const float dy = static_cast<float>(y - light_y);
            const float dist = std::sqrt(dx * dx + dy * dy);

            if (dist > light.radius) {
                continue;
            }

            const float angular_factor = getSpotAngularFactor(
                light.position,
                light.direction,
                light.arc_width,
                light.focus,
                Vector2d{ static_cast<double>(x), static_cast<double>(y) });
            if (angular_factor <= 0.0f) {
                continue;
            }

            const float falloff = angular_factor / (1.0f + dist * dist * light.attenuation);
            const RgbF received = traceRay(grid, data, light_x, light_y, x, y, light_color);
            data.colors.at(x, y) += received * falloff;
        }
    }
}

void WorldLightCalculator::applyRotatingLight(
    const RotatingLight& light, World& world, const GridOfCells& grid)
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
        world,
        grid);
}

float WorldLightCalculator::getSpotAngularFactor(
    const Vector2d& light_pos,
    float direction,
    float arc_width,
    float focus,
    const Vector2d& target_pos) const
{
    const Vector2d to_target = target_pos - light_pos;
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

void WorldLightCalculator::applyPointLights(World& world, const GridOfCells& grid)
{
    const LightManager& lights = world.getLightManager();
    if (lights.count() == 0) {
        return;
    }

    lights.forEachLight([&](LightId, const Light& light) {
        std::visit(
            [&](const auto& typed_light) {
                using T = std::decay_t<decltype(typed_light)>;
                if constexpr (std::is_same_v<T, PointLight>) {
                    applyPointLight(typed_light, world, grid);
                }
                else if constexpr (std::is_same_v<T, SpotLight>) {
                    applySpotLight(typed_light, world, grid);
                }
                else if constexpr (std::is_same_v<T, RotatingLight>) {
                    applyRotatingLight(typed_light, world, grid);
                }
            },
            light.getVariant());
    });
}

} // namespace DirtSim
