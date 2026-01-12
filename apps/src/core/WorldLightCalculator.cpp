#include "WorldLightCalculator.h"
#include "Assert.h"
#include "Cell.h"
#include "ColorNames.h"
#include "GridOfCells.h"
#include "LightConfig.h"
#include "MaterialType.h"
#include "PointLight.h"
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
        applyDiffusion(world, grid, config.diffusion_iterations, config.diffusion_rate);
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
    RgbF base_ambient = toRgbF(config.ambient_color) * config.ambient_intensity;

    if (!config.sky_access_enabled) {
        // Simple uniform ambient - parallelize.
        size_t count = data.colors.size();
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (GridOfCells::USE_OPENMP && count >= 2500)
#endif
        for (size_t i = 0; i < count; ++i) {
            data.colors.data[i] += base_ambient;
        }
        return;
    }

    int width = data.width;
    int height = data.height;
    float falloff = config.sky_access_falloff;

    // Helper to apply ambient with sky attenuation for a single material.
    auto processCell =
        [&data, &base_ambient, falloff](MaterialType mat, int x, int y, float& sky_factor) {
            data.colors.at(x, y) += base_ambient * sky_factor;
            float opacity = getMaterialProperties(mat).light.opacity;
            sky_factor *= (1.0f - opacity * falloff);
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
            uint64_t packed = grid.getMaterialNeighborhood(x, 0).raw();
            MaterialType mat = static_cast<MaterialType>((packed >> 16) & 0xF);
            processCell(mat, x, 0, sky_factor);
        }

        // Process 3 cells at a time using the column from each neighborhood lookup.
        int y = 1;
        for (; y + 2 < height; y += 3) {
            uint64_t packed = grid.getMaterialNeighborhood(x, y + 1).raw();
            MaterialType mat0 = static_cast<MaterialType>((packed >> 4) & 0xF);  // y
            MaterialType mat1 = static_cast<MaterialType>((packed >> 16) & 0xF); // y+1
            MaterialType mat2 = static_cast<MaterialType>((packed >> 28) & 0xF); // y+2

            processCell(mat0, x, y, sky_factor);
            processCell(mat1, x, y + 1, sky_factor);
            processCell(mat2, x, y + 2, sky_factor);
        }

        // Handle remaining rows.
        for (; y < height; ++y) {
            uint64_t packed = grid.getMaterialNeighborhood(x, y).raw();
            MaterialType mat = static_cast<MaterialType>((packed >> 16) & 0xF);
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
    RgbF scaled_sun = toRgbF(sun_color) * intensity;
    int width = data.width;
    int height = data.height;

    // Helper to apply sunlight attenuation for a single material.
    auto processCell = [&data](MaterialType mat, int x, int y, RgbF& sun) {
        data.colors.at(x, y) += sun;
        const auto& light_props = getMaterialProperties(mat).light;
        float transmittance = 1.0f - light_props.opacity;
        sun *= transmittance;
        sun *= ColorNames::toRgbF(light_props.tint);
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
            uint64_t packed = grid.getMaterialNeighborhood(x, 0).raw();
            MaterialType mat = static_cast<MaterialType>((packed >> 16) & 0xF);
            processCell(mat, x, 0, sun);
        }

        // Process 3 cells at a time using the column from each neighborhood lookup.
        // Neighborhood at (x, y) contains: y-1 at bits 4-7, y at bits 16-19, y+1 at bits 28-31.
        int y = 1;
        for (; y + 2 < height; y += 3) {
            // Fetch neighborhood centered at y+1 to get y, y+1, y+2.
            uint64_t packed = grid.getMaterialNeighborhood(x, y + 1).raw();
            MaterialType mat0 = static_cast<MaterialType>((packed >> 4) & 0xF);  // y
            MaterialType mat1 = static_cast<MaterialType>((packed >> 16) & 0xF); // y+1
            MaterialType mat2 = static_cast<MaterialType>((packed >> 28) & 0xF); // y+2

            processCell(mat0, x, y, sun);
            processCell(mat1, x, y + 1, sun);
            processCell(mat2, x, y + 2, sun);
        }

        // Handle remaining rows (0-2 cells).
        for (; y < height; ++y) {
            uint64_t packed = grid.getMaterialNeighborhood(x, y).raw();
            MaterialType mat = static_cast<MaterialType>((packed >> 16) & 0xF);
            processCell(mat, x, y, sun);
        }
    }
}

void WorldLightCalculator::applyEmissiveCells(World& world)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    auto& data = world.getData();
    int width = data.width;
    int height = data.height;

#ifdef _OPENMP
#pragma omp parallel for collapse(2) \
    schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Cell& cell = data.cells[static_cast<size_t>(y) * width + x];
            const auto& light_props = cell.material().light;

            if (light_props.emission > 0.0f) {
                RgbF emitted = toRgbF(light_props.emission_color) * light_props.emission;
                data.colors.at(x, y) += emitted;
            }
        }
    }
}

void WorldLightCalculator::applyDiffusion(
    World& world, const GridOfCells& grid, int iterations, float rate)
{
    using ColorNames::RgbF;

    if (iterations <= 0 || rate <= 0.0f) {
        return;
    }

    auto& data = world.getData();
    size_t cell_count = static_cast<size_t>(data.width) * data.height;
    light_buffer_.resize(cell_count);

    const CellBitmap& empty = grid.emptyCells();
    int width = data.width;
    int height = data.height;

    for (int iter = 0; iter < iterations; ++iter) {
        std::copy(data.colors.begin(), data.colors.end(), light_buffer_.begin());

        // Within each iteration, cells are independent (read from buffer, write to colors).
#ifdef _OPENMP
#pragma omp parallel for collapse(2) \
    schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
        for (int y = 1; y < height - 1; ++y) {
            for (int x = 1; x < width - 1; ++x) {
                // Skip empty (AIR) cells - they have scatter = 0.
                if (empty.isSet(x, y)) {
                    continue;
                }

                // Get material from neighborhood cache instead of Cell struct.
                uint64_t packed = grid.getMaterialNeighborhood(x, y).raw();
                MaterialType mat = static_cast<MaterialType>((packed >> 16) & 0xF);
                float scatter = getMaterialProperties(mat).light.scatter;

                if (scatter <= 0.0f) {
                    continue;
                }

                size_t idx = static_cast<size_t>(y) * width + x;
                const RgbF& up = light_buffer_[static_cast<size_t>(y - 1) * width + x];
                const RgbF& down = light_buffer_[static_cast<size_t>(y + 1) * width + x];
                const RgbF& left = light_buffer_[static_cast<size_t>(y) * width + (x - 1)];
                const RgbF& right = light_buffer_[static_cast<size_t>(y) * width + (x + 1)];

                RgbF neighbor_avg{ (up.r + down.r + left.r + right.r) * 0.25f,
                                   (up.g + down.g + left.g + right.g) * 0.25f,
                                   (up.b + down.b + left.b + right.b) * 0.25f };

                float blend = scatter * rate;
                data.colors.data[idx] = ColorNames::lerp(light_buffer_[idx], neighbor_avg, blend);
            }
        }
    }
}

namespace {

ColorNames::RgbF getMaterialBaseColor(MaterialType mat)
{
    using ColorNames::toRgbF;
    switch (mat) {
        case MaterialType::AIR:
            return toRgbF(ColorNames::black());
        case MaterialType::DIRT:
            return toRgbF(ColorNames::dirt());
        case MaterialType::LEAF:
            return toRgbF(ColorNames::leaf());
        case MaterialType::METAL:
            return toRgbF(ColorNames::metal());
        case MaterialType::ROOT:
            return toRgbF(ColorNames::root());
        case MaterialType::SAND:
            return toRgbF(ColorNames::sand());
        case MaterialType::SEED:
            return toRgbF(ColorNames::seed());
        case MaterialType::WALL:
            return toRgbF(ColorNames::stone());
        case MaterialType::WATER:
            return toRgbF(ColorNames::water());
        case MaterialType::WOOD:
            return toRgbF(ColorNames::wood());
        default:
            return ColorNames::RgbF{ 1.0f, 1.0f, 1.0f };
    }
}

} // namespace

void WorldLightCalculator::applyMaterialColors(World& world)
{
    auto& data = world.getData();
    int width = data.width;
    int height = data.height;

#ifdef _OPENMP
#pragma omp parallel for collapse(2) \
    schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Cell& cell = data.cells[static_cast<size_t>(y) * width + x];
            data.colors.at(x, y) *= getMaterialBaseColor(cell.getRenderMaterial());
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
            float b = ColorNames::brightness(data.colors.at(x, y));
            int idx = std::min(9, static_cast<int>(b * 10));
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
    const GridOfCells& grid, int x0, int y0, int x1, int y1, ColorNames::RgbF color) const
{
    // Bresenham's line algorithm to trace from light source to target.
    // Accumulates opacity and tinting as light passes through materials.
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    int x = x0;
    int y = y0;
    int width = grid.getWidth();
    int height = grid.getHeight();

    // Skip the source cell itself.
    while (x != x1 || y != y1) {
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err -= dx;
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

        // Get material at this cell and apply opacity/tinting.
        uint64_t packed = grid.getMaterialNeighborhood(x, y).raw();
        MaterialType mat = static_cast<MaterialType>((packed >> 16) & 0xF);
        const auto& light_props = getMaterialProperties(mat).light;

        float transmittance = 1.0f - light_props.opacity;
        color *= transmittance;
        color *= ColorNames::toRgbF(light_props.tint);

        // Early exit if light is fully absorbed.
        if (color.r < 0.001f && color.g < 0.001f && color.b < 0.001f) {
            return ColorNames::RgbF{};
        }
    }

    return color;
}

void WorldLightCalculator::applyPointLights(World& world, const GridOfCells& grid)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    const auto& point_lights = world.getPointLights();
    if (point_lights.empty()) {
        return;
    }

    auto& data = world.getData();
    int width = data.width;
    int height = data.height;

    for (const PointLight& light : point_lights) {
        int light_x = static_cast<int>(light.position.x);
        int light_y = static_cast<int>(light.position.y);

        // Skip lights outside the world.
        if (light_x < 0 || light_x >= width || light_y < 0 || light_y >= height) {
            continue;
        }

        int radius_int = static_cast<int>(std::ceil(light.radius));
        RgbF light_color = toRgbF(light.color) * light.intensity;

        // Iterate over cells within the light's radius.
        int min_x = std::max(0, light_x - radius_int);
        int max_x = std::min(width - 1, light_x + radius_int);
        int min_y = std::max(0, light_y - radius_int);
        int max_y = std::min(height - 1, light_y + radius_int);

        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                // Calculate distance.
                float dx = static_cast<float>(x - light_x);
                float dy = static_cast<float>(y - light_y);
                float dist = std::sqrt(dx * dx + dy * dy);

                // Skip cells outside radius.
                if (dist > light.radius) {
                    continue;
                }

                // Apply distance falloff: 1 / (1 + dist² * attenuation).
                float falloff = 1.0f / (1.0f + dist * dist * light.attenuation);

                // Trace ray from light to cell.
                RgbF received = traceRay(grid, light_x, light_y, x, y, light_color);

                // Apply falloff and add to cell.
                data.colors.at(x, y) += received * falloff;
            }
        }
    }
}

} // namespace DirtSim
