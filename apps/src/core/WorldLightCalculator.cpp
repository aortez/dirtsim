#include "WorldLightCalculator.h"
#include "Cell.h"
#include "ColorNames.h"
#include "LightConfig.h"
#include "MaterialType.h"
#include "World.h"
#include "WorldData.h"

namespace DirtSim {

void WorldLightCalculator::calculate(World& world, const LightConfig& config)
{
    auto& data = world.getData();

    // Ensure colors buffer is sized correctly.
    if (data.colors.width != data.width || data.colors.height != data.height) {
        data.colors.resize(data.width, data.height, ColorNames::black());
    }

    // Clear to black before accumulating light.
    clearLight(world);

    // Add ambient light (with optional sky access attenuation).
    applyAmbient(world, config);

    // Add sunlight (top-down).
    if (config.sun_enabled) {
        applySunlight(world, config.sun_color, config.sun_intensity);
    }

    // Add emissive material contributions.
    applyEmissiveCells(world);

    applyDiffusion(world, config.diffusion_iterations, config.diffusion_rate);
    storeRawLight(world);
    applyMaterialColors(world);
}

void WorldLightCalculator::clearLight(World& world)
{
    auto& data = world.getData();
    data.colors.clear(ColorNames::black());
}

void WorldLightCalculator::applyAmbient(World& world, const LightConfig& config)
{
    auto& data = world.getData();
    uint32_t base_ambient = ColorNames::scale(config.ambient_color, config.ambient_intensity);

    if (!config.sky_access_enabled) {
        // Uniform ambient everywhere.
        for (uint32_t* c = data.colors.begin(); c != data.colors.end(); ++c) {
            *c = ColorNames::add(*c, base_ambient);
        }
        return;
    }

    // Sky access attenuation: ambient diminishes with depth based on opacity above.
    for (uint32_t x = 0; x < data.width; ++x) {
        float sky_factor = 1.0f;

        for (uint32_t y = 0; y < data.height; ++y) {
            const Cell& cell = data.cells[y * data.width + x];

            // Apply ambient scaled by sky access.
            uint32_t ambient = ColorNames::scale(base_ambient, sky_factor);
            data.colors.at(x, y) = ColorNames::add(data.colors.at(x, y), ambient);

            // Attenuate sky access by material opacity.
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
    auto& data = world.getData();
    uint32_t scaled_sun = ColorNames::scale(sun_color, intensity);

    // Cast sunlight from top of world downward per column.
    for (uint32_t x = 0; x < data.width; ++x) {
        uint32_t sun = scaled_sun;

        for (uint32_t y = 0; y < data.height; ++y) {
            const Cell& cell = data.cells[y * data.width + x];

            // Add current sun level to cell.
            data.colors.at(x, y) = ColorNames::add(data.colors.at(x, y), sun);

            // Attenuate by material opacity.
            const auto& light_props = cell.material().light;
            float transmittance = 1.0f - light_props.opacity;
            sun = ColorNames::scale(sun, transmittance);

            // Apply material tint to transmitted light.
            sun = ColorNames::multiply(sun, light_props.tint);
        }
    }
}

void WorldLightCalculator::applyEmissiveCells(World& world)
{
    auto& data = world.getData();
    for (uint32_t y = 0; y < data.height; ++y) {
        for (uint32_t x = 0; x < data.width; ++x) {
            const Cell& cell = data.cells[y * data.width + x];
            const auto& light_props = cell.material().light;

            if (light_props.emission > 0.0f) {
                uint32_t emitted =
                    ColorNames::scale(light_props.emission_color, light_props.emission);
                data.colors.at(x, y) = ColorNames::add(data.colors.at(x, y), emitted);
            }
        }
    }
}

void WorldLightCalculator::applyDiffusion(World& world, int iterations, float rate)
{
    if (iterations <= 0 || rate <= 0.0f) {
        return;
    }

    auto& data = world.getData();
    size_t cell_count = static_cast<size_t>(data.width) * data.height;
    light_buffer_.resize(cell_count);

    for (int iter = 0; iter < iterations; ++iter) {
        // Copy current colors to buffer (contiguous memory copy).
        std::copy(data.colors.begin(), data.colors.end(), light_buffer_.begin());

        // Apply diffusion.
        for (uint32_t y = 1; y < data.height - 1; ++y) {
            for (uint32_t x = 1; x < data.width - 1; ++x) {
                size_t idx = y * data.width + x;
                const Cell& cell = data.cells[idx];
                float scatter = cell.material().light.scatter;

                if (scatter <= 0.0f) {
                    continue;
                }

                // Average of 4 cardinal neighbors.
                uint32_t up = light_buffer_[(y - 1) * data.width + x];
                uint32_t down = light_buffer_[(y + 1) * data.width + x];
                uint32_t left = light_buffer_[y * data.width + (x - 1)];
                uint32_t right = light_buffer_[y * data.width + (x + 1)];

                // Simple average of neighbor colors.
                float r = (ColorNames::getRf(up) + ColorNames::getRf(down) + ColorNames::getRf(left)
                           + ColorNames::getRf(right))
                    / 4.0f;
                float g = (ColorNames::getGf(up) + ColorNames::getGf(down) + ColorNames::getGf(left)
                           + ColorNames::getGf(right))
                    / 4.0f;
                float b = (ColorNames::getBf(up) + ColorNames::getBf(down) + ColorNames::getBf(left)
                           + ColorNames::getBf(right))
                    / 4.0f;

                uint32_t neighbor_avg = ColorNames::rgbaF(r, g, b, 1.0f);
                uint32_t current = light_buffer_[idx];

                // Blend toward neighbor average based on scatter rate.
                float blend = scatter * rate;
                data.colors.data[idx] = ColorNames::lerp(current, neighbor_avg, blend);
            }
        }
    }
}

namespace {

uint32_t getMaterialBaseColor(MaterialType mat)
{
    switch (mat) {
        case MaterialType::AIR:
            return ColorNames::black();
        case MaterialType::DIRT:
            return ColorNames::dirt();
        case MaterialType::LEAF:
            return ColorNames::leaf();
        case MaterialType::METAL:
            return ColorNames::metal();
        case MaterialType::ROOT:
            return ColorNames::root();
        case MaterialType::SAND:
            return ColorNames::sand();
        case MaterialType::SEED:
            return ColorNames::seed();
        case MaterialType::WALL:
            return ColorNames::stone();
        case MaterialType::WATER:
            return ColorNames::water();
        case MaterialType::WOOD:
            return ColorNames::wood();
        default:
            return ColorNames::white();
    }
}

} // namespace

void WorldLightCalculator::applyMaterialColors(World& world)
{
    auto& data = world.getData();
    for (uint32_t y = 0; y < data.height; ++y) {
        for (uint32_t x = 0; x < data.width; ++x) {
            const Cell& cell = data.cells[y * data.width + x];
            // Use getRenderMaterial() to respect render_as override.
            uint32_t base_color = getMaterialBaseColor(cell.getRenderMaterial());
            data.colors.at(x, y) = ColorNames::multiply(data.colors.at(x, y), base_color);
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
            float brightness = ColorNames::brightness(data.colors.at(x, y));
            int idx = std::min(9, static_cast<int>(brightness * 10));
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

    // Direct copy from colors buffer to raw_light buffer.
    std::copy(data.colors.begin(), data.colors.end(), raw_light_.begin());
}

const LightBuffer& WorldLightCalculator::getRawLightBuffer() const
{
    return raw_light_;
}

} // namespace DirtSim
