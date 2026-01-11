#include "WorldLightCalculator.h"
#include "Cell.h"
#include "ColorNames.h"
#include "LightConfig.h"
#include "MaterialType.h"
#include "ScopeTimer.h"
#include "Timers.h"
#include "World.h"
#include "WorldData.h"

namespace DirtSim {

void WorldLightCalculator::calculate(World& world, const LightConfig& config, Timers& timers)
{
    auto& data = world.getData();

    // Ensure colors buffer is sized correctly.
    if (data.colors.width != data.width || data.colors.height != data.height) {
        data.colors.resize(data.width, data.height, ColorNames::RgbF{});
    }

    // Clear to black before accumulating light.
    {
        ScopeTimer t(timers, "light_clear");
        clearLight(world);
    }

    // Add ambient light (with optional sky access attenuation).
    {
        ScopeTimer t(timers, "light_ambient");
        applyAmbient(world, config);
    }

    // Add sunlight (top-down).
    if (config.sun_enabled) {
        ScopeTimer t(timers, "light_sunlight");
        applySunlight(world, config.sun_color, config.sun_intensity);
    }

    // Add emissive material contributions.
    {
        ScopeTimer t(timers, "light_emissive");
        applyEmissiveCells(world);
    }

    {
        ScopeTimer t(timers, "light_diffusion");
        applyDiffusion(world, config.diffusion_iterations, config.diffusion_rate);
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

void WorldLightCalculator::applyAmbient(World& world, const LightConfig& config)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    auto& data = world.getData();
    RgbF base_ambient = toRgbF(config.ambient_color) * config.ambient_intensity;

    if (!config.sky_access_enabled) {
        for (RgbF* c = data.colors.begin(); c != data.colors.end(); ++c) {
            *c += base_ambient;
        }
        return;
    }

    // Sky access attenuation: ambient diminishes with depth based on opacity above.
    for (uint32_t x = 0; x < data.width; ++x) {
        float sky_factor = 1.0f;

        for (uint32_t y = 0; y < data.height; ++y) {
            const Cell& cell = data.cells[y * data.width + x];

            data.colors.at(x, y) += base_ambient * sky_factor;

            float opacity = cell.material().light.opacity;
            sky_factor *= (1.0f - opacity * config.sky_access_falloff);
            if (sky_factor < 0.0f) {
                sky_factor = 0.0f;
            }
        }
    }
}

void WorldLightCalculator::applySunlight(World& world, uint32_t sun_color, float intensity)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    auto& data = world.getData();
    RgbF scaled_sun = toRgbF(sun_color) * intensity;

    // Cast sunlight from top of world downward per column.
    for (uint32_t x = 0; x < data.width; ++x) {
        RgbF sun = scaled_sun;

        for (uint32_t y = 0; y < data.height; ++y) {
            const Cell& cell = data.cells[y * data.width + x];

            data.colors.at(x, y) += sun;

            const auto& light_props = cell.material().light;
            float transmittance = 1.0f - light_props.opacity;
            sun *= transmittance;

            // Apply material tint.
            RgbF tint = toRgbF(light_props.tint);
            sun *= tint;
        }
    }
}

void WorldLightCalculator::applyEmissiveCells(World& world)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    auto& data = world.getData();
    for (uint32_t y = 0; y < data.height; ++y) {
        for (uint32_t x = 0; x < data.width; ++x) {
            const Cell& cell = data.cells[y * data.width + x];
            const auto& light_props = cell.material().light;

            if (light_props.emission > 0.0f) {
                RgbF emitted = toRgbF(light_props.emission_color) * light_props.emission;
                data.colors.at(x, y) += emitted;
            }
        }
    }
}

void WorldLightCalculator::applyDiffusion(World& world, int iterations, float rate)
{
    using ColorNames::RgbF;

    if (iterations <= 0 || rate <= 0.0f) {
        return;
    }

    auto& data = world.getData();
    size_t cell_count = static_cast<size_t>(data.width) * data.height;
    light_buffer_.resize(cell_count);

    for (int iter = 0; iter < iterations; ++iter) {
        std::copy(data.colors.begin(), data.colors.end(), light_buffer_.begin());

        for (uint32_t y = 1; y < data.height - 1; ++y) {
            for (uint32_t x = 1; x < data.width - 1; ++x) {
                size_t idx = y * data.width + x;
                const Cell& cell = data.cells[idx];
                float scatter = cell.material().light.scatter;

                if (scatter <= 0.0f) {
                    continue;
                }

                const RgbF& up = light_buffer_[(y - 1) * data.width + x];
                const RgbF& down = light_buffer_[(y + 1) * data.width + x];
                const RgbF& left = light_buffer_[y * data.width + (x - 1)];
                const RgbF& right = light_buffer_[y * data.width + (x + 1)];

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
    for (uint32_t y = 0; y < data.height; ++y) {
        for (uint32_t x = 0; x < data.width; ++x) {
            const Cell& cell = data.cells[y * data.width + x];
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

    for (uint32_t y = 0; y < data.height; ++y) {
        for (uint32_t x = 0; x < data.width; ++x) {
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

} // namespace DirtSim
