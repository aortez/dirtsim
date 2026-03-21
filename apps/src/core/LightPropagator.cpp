#include "LightPropagator.h"
#include "Assert.h"
#include "Cell.h"
#include "ColorNames.h"
#include "GridOfCells.h"
#include "LightConfig.h"
#include "LightManager.h"
#include "LightTypes.h"
#include "MaterialColor.h"
#include "MaterialType.h"
#include "ScopeTimer.h"
#include "Timers.h"
#include "World.h"
#include "WorldData.h"

#include <algorithm>
#include <cmath>

namespace DirtSim {

namespace {

// Uniform diffuse distribution weights (cardinal=1.0, diagonal=0.707).
// Normalized so they sum to 1.0.
constexpr float kCardinalWeight = 1.0f;
constexpr float kDiagonalWeight = 0.707f;
constexpr float kTotalWeight = 4.0f * kCardinalWeight + 4.0f * kDiagonalWeight;

ColorNames::RgbF getAmbientMaterialBaseColor(Material::EnumType mat)
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
            return { 0.7f, 0.7f, 0.7f };
        case Material::EnumType::Water:
            return toRgbF(ColorNames::water());
        case Material::EnumType::Wood:
            return toRgbF(ColorNames::wood());
    }
    return { 1.0f, 1.0f, 1.0f };
}

} // namespace

void LightPropagator::applyFlatBasic(WorldData& data)
{
    const int width = data.width;
    const int height = data.height;

#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Cell& cell = data.cells[static_cast<size_t>(y) * width + x];
            const Material::EnumType renderMaterial =
                cell.isEmpty() ? Material::EnumType::Air : cell.getRenderMaterial();
            data.colors.at(x, y) = ColorNames::toRgbF(getLegacyMaterialColor(renderMaterial));
        }
    }
}

void LightPropagator::clearPropagatedState()
{
    light_field_.clear();
    light_field_next_.clear();
    ambient_boost_ = {};
}

void LightPropagator::ensureBufferSizes(int width, int height)
{
    if (light_field_.width != width || light_field_.height != height) {
        light_field_.resize(width, height);
        light_field_next_.resize(width, height);
    }
    if (emissive_overlay_.width != width || emissive_overlay_.height != height) {
        emissive_overlay_.resize(width, height, ColorNames::RgbF{});
    }
}

void LightPropagator::propagateStep(const WorldData& data, bool air_fast_path)
{
    const int width = data.width;
    const int height = data.height;

    // Precomputed uniform diffuse weights per direction.
    static constexpr float kWeights[8] = {
        kCardinalWeight / kTotalWeight, kDiagonalWeight / kTotalWeight,
        kCardinalWeight / kTotalWeight, kDiagonalWeight / kTotalWeight,
        kCardinalWeight / kTotalWeight, kDiagonalWeight / kTotalWeight,
        kCardinalWeight / kTotalWeight, kDiagonalWeight / kTotalWeight,
    };

    // Threshold below which a cell is considered fully transparent (air-like).
    constexpr float kTransparentThreshold = 0.01f;

#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Cell& cell = data.cells[static_cast<size_t>(y) * width + x];
            const auto& props = cell.material().light;
            const float fill = cell.fill_ratio;
            const float eff_opacity = props.opacity * fill;

            auto& dst = light_field_next_.at(x, y);

            // Fast path: near-transparent cells just forward light with no scatter.
            if (air_fast_path && eff_opacity < kTransparentThreshold) {
                for (int di = 0; di < 8; ++di) {
                    const Vector2i up = upstream(static_cast<LightDir>(di));
                    const int ux = x + up.x;
                    const int uy = y + up.y;

                    if (ux < 0 || ux >= width || uy < 0 || uy >= height) {
                        continue;
                    }

                    const ColorNames::RgbF& incoming = light_field_.at(ux, uy).channel[di];
                    dst.channel[di].r += incoming.r;
                    dst.channel[di].g += incoming.g;
                    dst.channel[di].b += incoming.b;
                }
                continue;
            }

            const float transmit = 1.0f - eff_opacity;
            const ColorNames::RgbF tint_rgb = ColorNames::toRgbF(props.tint);
            const ColorNames::RgbF eff_tint =
                ColorNames::lerp({ 1.0f, 1.0f, 1.0f }, tint_rgb, fill);

            // Precompute combined tint factors to reduce per-direction work.
            const ColorNames::RgbF transmit_tint = eff_tint * transmit;
            const float scatter_factor = eff_opacity * props.scatter;
            const ColorNames::RgbF specular_tint = eff_tint * (scatter_factor * props.specularity);
            const ColorNames::RgbF diffuse_tint =
                eff_tint * (scatter_factor * (1.0f - props.specularity));

            // Accumulate total diffuse across all incoming directions, distribute once.
            float diff_r = 0.0f, diff_g = 0.0f, diff_b = 0.0f;

            for (int di = 0; di < 8; ++di) {
                const auto d = static_cast<LightDir>(di);
                const Vector2i up = upstream(d);
                const int ux = x + up.x;
                const int uy = y + up.y;

                if (ux < 0 || ux >= width || uy < 0 || uy >= height) {
                    continue;
                }

                const ColorNames::RgbF incoming = light_field_.at(ux, uy).channel[di];

                if (incoming.r < 0.001f && incoming.g < 0.001f && incoming.b < 0.001f) {
                    continue;
                }

                // Forward transmission.
                auto& fwd = dst.channel[di];
                fwd.r += incoming.r * transmit_tint.r;
                fwd.g += incoming.g * transmit_tint.g;
                fwd.b += incoming.b * transmit_tint.b;

                // Specular reflection.
                auto& spec = dst.channel[static_cast<int>(opposite(d))];
                spec.r += incoming.r * specular_tint.r;
                spec.g += incoming.g * specular_tint.g;
                spec.b += incoming.b * specular_tint.b;

                // Accumulate diffuse for single distribution pass.
                diff_r += incoming.r * diffuse_tint.r;
                diff_g += incoming.g * diffuse_tint.g;
                diff_b += incoming.b * diffuse_tint.b;
            }

            // Distribute accumulated diffuse once across all 8 directions.
            if (diff_r > 0.0f || diff_g > 0.0f || diff_b > 0.0f) {
                for (int dj = 0; dj < 8; ++dj) {
                    dst.channel[dj].r += diff_r * kWeights[dj];
                    dst.channel[dj].g += diff_g * kWeights[dj];
                    dst.channel[dj].b += diff_b * kWeights[dj];
                }
            }
        }
    }
}

void LightPropagator::injectSources(World& world, const LightConfig& config)
{
    const auto& data = world.getData();
    const int width = data.width;
    const int height = data.height;

    const ColorNames::RgbF sun_rgb = ColorNames::toRgbF(config.sun_color) * config.sun_intensity;
    const ColorNames::RgbF sky_rgb = ColorNames::toRgbF(config.sky_color) * config.sky_intensity;

    // Sunlight and sky dome enter from above, filtered by material at the top row.
    if (config.sun_intensity > 0.0f || config.sky_intensity > 0.0f) {
        for (int x = 0; x < width; ++x) {
            const Cell& cell = data.cells[static_cast<size_t>(x)];
            const float transmit = 1.0f - cell.material().light.opacity * cell.fill_ratio;
            if (transmit < 0.001f) {
                continue;
            }

            auto& top = light_field_next_.at(x, 0);

            if (config.sun_intensity > 0.0f) {
                top.channel[static_cast<int>(LightDir::S)] += sun_rgb * transmit;
            }

            if (config.sky_intensity > 0.0f) {
                top.channel[static_cast<int>(LightDir::S)] += sky_rgb * 0.5f * transmit;
                top.channel[static_cast<int>(LightDir::SE)] += sky_rgb * 0.25f * transmit;
                top.channel[static_cast<int>(LightDir::SW)] += sky_rgb * 0.25f * transmit;
            }
        }
    }

    // Emissive materials: cells with emission > 0 inject light in all directions.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Cell& cell = data.cells[static_cast<size_t>(y) * width + x];
            const auto& props = cell.material().light;
            if (props.emission > 0.0f && cell.fill_ratio > 0.0f) {
                const ColorNames::RgbF emission =
                    ColorNames::toRgbF(props.emission_color) * props.emission * cell.fill_ratio;
                const ColorNames::RgbF per_dir = emission * (1.0f / 8.0f);
                auto& dst = light_field_next_.at(x, y);
                for (int di = 0; di < 8; ++di) {
                    dst.channel[di] += per_dir;
                }
            }
        }
    }

    // Emissive overlay: small fraction propagates for subtle local glow.
    constexpr float kOverlayGlowFraction = 0.03f;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const ColorNames::RgbF& overlay = emissive_overlay_.at(x, y);
            if (overlay.r > 0.0f || overlay.g > 0.0f || overlay.b > 0.0f) {
                const ColorNames::RgbF per_dir = overlay * kOverlayGlowFraction * (1.0f / 8.0f);
                auto& dst = light_field_next_.at(x, y);
                for (int di = 0; di < 8; ++di) {
                    dst.channel[di] += per_dir;
                }
            }
        }
    }

    // Point lights, spot lights, rotating lights from LightManager.
    world.getLightManager().forEachLight([&](LightId /*id*/, const Light& light) {
        std::visit(
            [&](const auto& l) {
                const int lx = static_cast<int>(l.position.x);
                const int ly = static_cast<int>(l.position.y);
                if (lx < 0 || lx >= width || ly < 0 || ly >= height) {
                    return;
                }

                const ColorNames::RgbF color = ColorNames::toRgbF(l.color) * l.intensity;

                using LightType = std::decay_t<decltype(l)>;

                if constexpr (std::is_same_v<LightType, PointLight>) {
                    // Point light: inject into all 8 directions.
                    const ColorNames::RgbF per_dir = color * (1.0f / 8.0f);
                    auto& dst = light_field_next_.at(lx, ly);
                    for (int di = 0; di < 8; ++di) {
                        dst.channel[di] += per_dir;
                    }
                }
                else if constexpr (
                    std::is_same_v<LightType, SpotLight>
                    || std::is_same_v<LightType, RotatingLight>) {
                    // Spot/rotating light: inject into directions within the cone.
                    auto& dst = light_field_next_.at(lx, ly);
                    for (int di = 0; di < 8; ++di) {
                        // Direction angle for each compass direction.
                        constexpr float dir_angles[] = {
                            static_cast<float>(M_PI * 0.5),   // N: 90 degrees.
                            static_cast<float>(M_PI * 0.25),  // NE: 45 degrees.
                            0.0f,                             // E: 0 degrees.
                            static_cast<float>(-M_PI * 0.25), // SE: -45 degrees.
                            static_cast<float>(-M_PI * 0.5),  // S: -90 degrees.
                            static_cast<float>(-M_PI * 0.75), // SW: -135 degrees.
                            static_cast<float>(M_PI),         // W: 180 degrees.
                            static_cast<float>(M_PI * 0.75),  // NW: 135 degrees.
                        };
                        float angle_diff = std::fabs(
                            std::remainder(
                                dir_angles[di] - l.direction, 2.0f * static_cast<float>(M_PI)));
                        const float half_arc = l.arc_width * 0.5f;
                        if (angle_diff <= half_arc) {
                            // Angular falloff within cone.
                            float factor = 1.0f - (angle_diff / half_arc) * l.focus;
                            factor = std::max(0.0f, factor);
                            dst.channel[di] += color * factor;
                        }
                    }
                }
            },
            light.getVariant());
    });
}

void LightPropagator::applyAmbient(WorldData& data, const LightConfig& config)
{
    using ColorNames::RgbF;

    const RgbF base_ambient =
        ColorNames::toRgbF(config.ambient_color) * config.ambient_intensity + ambient_boost_;
    const RgbF white{ 1.0f, 1.0f, 1.0f };

    const int width = data.width;
    const int height = data.height;

#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (GridOfCells::USE_OPENMP && width * height >= 2500)
#endif
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Propagated light already carries material tint from transport.
            const RgbF propagated = light_field_.at(x, y).total();

            // Ambient light needs material coloring since it bypasses transport.
            const Cell& cell = data.cells[static_cast<size_t>(y) * width + x];
            const Material::EnumType mat = cell.getRenderMaterial();
            const float saturation = Material::getProperties(mat).light.saturation;
            const RgbF base_color = getAmbientMaterialBaseColor(mat);
            const RgbF ambient_tinted =
                base_ambient * ColorNames::lerp(white, base_color, saturation);

            RgbF color = propagated + ambient_tinted;

            // Emissive overlay adds directly to display brightness.
            const RgbF& overlay = emissive_overlay_.at(x, y);
            if (overlay.r > 0.0f || overlay.g > 0.0f || overlay.b > 0.0f) {
                color += overlay;
            }

            data.colors.at(x, y) = color;
        }
    }
}

void LightPropagator::storeRawLight(WorldData& data)
{
    if (raw_light_.width != data.width || raw_light_.height != data.height) {
        raw_light_.resize(data.width, data.height);
    }

    const auto* src = data.colors.begin();
    uint32_t* dst = raw_light_.begin();
    for (size_t i = 0; i < data.colors.size(); ++i) {
        dst[i] = ColorNames::toRgba(src[i]);
    }
}

void LightPropagator::calculate(
    World& world, const GridOfCells& /*grid*/, const LightConfig& config, Timers& timers)
{
    ScopeTimer total_timer(timers, "light_calculation");

    auto& data = world.getData();

    // Ensure colors buffer is sized correctly.
    if (data.colors.width != data.width || data.colors.height != data.height) {
        data.colors.resize(data.width, data.height, ColorNames::RgbF{});
    }

    ensureBufferSizes(data.width, data.height);
    const bool inFlatBasicMode = config.mode == LightMode::FlatBasic;
    if (inFlatBasicMode && !inFlatBasicMode_) {
        clearPropagatedState();
    }
    inFlatBasicMode_ = inFlatBasicMode;

    switch (config.mode) {
        case LightMode::Propagated:
        case LightMode::Fast: {
            ScopeTimer t(timers, "light_propagate");

            if (config.temporal_persistence) {
                // Temporal mode: decay existing field and run one correction step.
                const float decay = config.temporal_decay;
                auto* field = light_field_.begin();
                const size_t count = light_field_.size() * 8;
                float* raw = reinterpret_cast<float*>(field);
                for (size_t i = 0; i < count * 3; ++i) {
                    raw[i] *= decay;
                }

                light_field_next_.clear();
                propagateStep(data, config.air_fast_path);
                injectSources(world, config);
                std::swap(light_field_, light_field_next_);
            }
            else {
                for (int step = 0; step < config.steps_per_frame; ++step) {
                    light_field_next_.clear();
                    propagateStep(data, config.air_fast_path);
                    injectSources(world, config);
                    std::swap(light_field_, light_field_next_);
                }
            }

            ScopeTimer ambientTimer(timers, "light_ambient");
            applyAmbient(data, config);
            ambient_boost_ = {};
            break;
        }
        case LightMode::FlatBasic: {
            ScopeTimer t(timers, "light_flat_basic");
            applyFlatBasic(data);
            break;
        }
    }

    {
        ScopeTimer t(timers, "light_store_raw");
        storeRawLight(data);
    }
}

std::string LightPropagator::lightMapString(const World& world) const
{
    const char* shades = " .:-=+*#%@";
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

const LightBuffer& LightPropagator::getRawLightBuffer() const
{
    return raw_light_;
}

void LightPropagator::setEmissive(int x, int y, uint32_t color, float intensity)
{
    DIRTSIM_ASSERT(
        x >= 0 && y >= 0 && x < emissive_overlay_.width && y < emissive_overlay_.height,
        "Out of bounds");
    emissive_overlay_.at(x, y) = ColorNames::toRgbF(color) * intensity;
}

void LightPropagator::clearEmissive(int x, int y)
{
    DIRTSIM_ASSERT(
        x >= 0 && y >= 0 && x < emissive_overlay_.width && y < emissive_overlay_.height,
        "Out of bounds");
    emissive_overlay_.at(x, y) = ColorNames::RgbF{};
}

void LightPropagator::clearAllEmissive()
{
    emissive_overlay_.clear(ColorNames::RgbF{});
}

void LightPropagator::resize(int width, int height)
{
    DIRTSIM_ASSERT(width >= 0 && height >= 0, "Dimensions must be non-negative");
    ensureBufferSizes(width, height);
    if (raw_light_.width != width || raw_light_.height != height) {
        raw_light_.resize(width, height);
    }
}

void LightPropagator::setAmbientBoost(ColorNames::RgbF boost)
{
    ambient_boost_ = boost;
}

} // namespace DirtSim
