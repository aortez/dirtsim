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

            ColorNames::RgbF color = ColorNames::toRgbF(getLegacyMaterialColor(renderMaterial));
            color += ambient_boost_;

            const ColorNames::RgbF& overlay = emissive_overlay_.at(x, y);
            if (overlay.r > 0.0f || overlay.g > 0.0f || overlay.b > 0.0f) {
                color += overlay;
            }

            data.colors.at(x, y) = color;
        }
    }
}

void LightPropagator::clearPropagatedState()
{
    light_field_.clear();
    light_field_next_.clear();
    clearLocalSpillState();
}

void LightPropagator::clearLocalSpillState()
{
    spill_field_.clear();
    spill_field_next_.clear();
    has_spill_seed_ = false;
}

void LightPropagator::ensureBufferSizes(int width, int height)
{
    if (light_field_.width != width || light_field_.height != height) {
        light_field_.resize(width, height);
        light_field_next_.resize(width, height);
    }
    if (spill_field_.width != width || spill_field_.height != height) {
        spill_field_.resize(width, height);
        spill_field_next_.resize(width, height);
    }
    if (emissive_overlay_.width != width || emissive_overlay_.height != height) {
        emissive_overlay_.resize(width, height, ColorNames::RgbF{});
    }
}

void LightPropagator::propagateFieldStep(
    const WorldData& data,
    bool air_fast_path,
    const GridBuffer<DirectionalLight>& src,
    GridBuffer<DirectionalLight>& dst)
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

            auto& dst_cell = dst.at(x, y);

            // Fast path: near-transparent cells just forward light with no scatter.
            if (air_fast_path && eff_opacity < kTransparentThreshold) {
                for (int di = 0; di < 8; ++di) {
                    const Vector2i up = upstream(static_cast<LightDir>(di));
                    const int ux = x + up.x;
                    const int uy = y + up.y;

                    if (ux < 0 || ux >= width || uy < 0 || uy >= height) {
                        continue;
                    }

                    const ColorNames::RgbF& incoming = src.at(ux, uy).channel[di];
                    dst_cell.channel[di].r += incoming.r;
                    dst_cell.channel[di].g += incoming.g;
                    dst_cell.channel[di].b += incoming.b;
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

                const ColorNames::RgbF incoming = src.at(ux, uy).channel[di];

                if (incoming.r < 0.001f && incoming.g < 0.001f && incoming.b < 0.001f) {
                    continue;
                }

                // Forward transmission.
                auto& fwd = dst_cell.channel[di];
                fwd.r += incoming.r * transmit_tint.r;
                fwd.g += incoming.g * transmit_tint.g;
                fwd.b += incoming.b * transmit_tint.b;

                // Specular reflection.
                auto& spec = dst_cell.channel[static_cast<int>(opposite(d))];
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
                    dst_cell.channel[dj].r += diff_r * kWeights[dj];
                    dst_cell.channel[dj].g += diff_g * kWeights[dj];
                    dst_cell.channel[dj].b += diff_b * kWeights[dj];
                }
            }
        }
    }
}

void LightPropagator::propagateStep(const WorldData& data, bool air_fast_path)
{
    propagateFieldStep(data, air_fast_path, light_field_, light_field_next_);
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

    // LightManager local lights use a direct pass for smooth cone/radius shaping.
}

void LightPropagator::applyDirectLocalLights(
    World& world, const GridOfCells& grid, float indirect_scale)
{
    const LightManager& lights = world.getLightManager();
    if (lights.count() == 0) {
        return;
    }

    lights.forEachLight([&](LightId /*id*/, const Light& light) {
        std::visit(
            [&](const auto& typed_light) {
                using LightType = std::decay_t<decltype(typed_light)>;
                if constexpr (std::is_same_v<LightType, PointLight>) {
                    applyDirectPointLight(typed_light, world, grid, indirect_scale);
                }
                else if constexpr (std::is_same_v<LightType, SpotLight>) {
                    applyDirectSpotLight(typed_light, world, grid, indirect_scale);
                }
                else if constexpr (std::is_same_v<LightType, RotatingLight>) {
                    applyDirectRotatingLight(typed_light, world, grid, indirect_scale);
                }
            },
            light.getVariant());
    });
}

void LightPropagator::applyDirectPointLight(
    const PointLight& light, World& world, const GridOfCells& grid, float indirect_scale)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    if (light.intensity <= 0.0f || light.radius <= 0.0f) {
        return;
    }

    auto& data = world.getData();
    const int width = data.width;
    const int height = data.height;
    const float light_x = light.position.x;
    const float light_y = light.position.y;
    const int light_cell_x = static_cast<int>(light_x);
    const int light_cell_y = static_cast<int>(light_y);
    if (light_cell_x < 0 || light_cell_x >= width || light_cell_y < 0 || light_cell_y >= height) {
        return;
    }

    const int radius_int = static_cast<int>(std::ceil(light.radius));
    const RgbF light_color = toRgbF(light.color) * light.intensity;
    const int min_x = std::max(0, light_cell_x - radius_int);
    const int max_x = std::min(width - 1, light_cell_x + radius_int);
    const int min_y = std::max(0, light_cell_y - radius_int);
    const int max_y = std::min(height - 1, light_cell_y + radius_int);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) - light_x;
            const float dy = (static_cast<float>(y) + 0.5f) - light_y;
            const float dist = std::sqrt(dx * dx + dy * dy);

            if (dist > light.radius) {
                continue;
            }

            const float falloff = 1.0f / (1.0f + dist * dist * light.attenuation);
            const RgbF received = traceRay(grid, data, light_x, light_y, x, y, light_color);
            const RgbF direct = received * falloff;
            data.colors.at(x, y) += direct;
            seedIndirectSpill(
                x, y, direct, data.at(x, y), light.indirect_strength * indirect_scale);
        }
    }
}

void LightPropagator::applyDirectRotatingLight(
    const RotatingLight& light, World& world, const GridOfCells& grid, float indirect_scale)
{
    applyDirectSpotLight(
        SpotLight{ .position = light.position,
                   .color = light.color,
                   .intensity = light.intensity,
                   .radius = light.radius,
                   .attenuation = light.attenuation,
                   .indirect_strength = light.indirect_strength,
                   .direction = light.direction,
                   .arc_width = light.arc_width,
                   .focus = light.focus },
        world,
        grid,
        indirect_scale);
}

void LightPropagator::applyDirectSpotLight(
    const SpotLight& light, World& world, const GridOfCells& grid, float indirect_scale)
{
    using ColorNames::RgbF;
    using ColorNames::toRgbF;

    if (light.intensity <= 0.0f || light.radius <= 0.0f || light.arc_width <= 0.0f) {
        return;
    }

    auto& data = world.getData();
    const int width = data.width;
    const int height = data.height;
    const float light_x = light.position.x;
    const float light_y = light.position.y;
    const int light_cell_x = static_cast<int>(light_x);
    const int light_cell_y = static_cast<int>(light_y);
    if (light_cell_x < 0 || light_cell_x >= width || light_cell_y < 0 || light_cell_y >= height) {
        return;
    }

    const int radius_int = static_cast<int>(std::ceil(light.radius));
    const RgbF light_color = toRgbF(light.color) * light.intensity;
    const int min_x = std::max(0, light_cell_x - radius_int);
    const int max_x = std::min(width - 1, light_cell_x + radius_int);
    const int min_y = std::max(0, light_cell_y - radius_int);
    const int max_y = std::min(height - 1, light_cell_y + radius_int);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) - light_x;
            const float dy = (static_cast<float>(y) + 0.5f) - light_y;
            const float dist = std::sqrt(dx * dx + dy * dy);

            if (dist > light.radius) {
                continue;
            }

            const float angular_factor = getSpotAngularFactor(
                Vector2f{ light_x, light_y },
                light.direction,
                light.arc_width,
                light.focus,
                Vector2f{ static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f });
            if (angular_factor <= 0.0f) {
                continue;
            }

            const float falloff = angular_factor / (1.0f + dist * dist * light.attenuation);
            const RgbF received = traceRay(grid, data, light_x, light_y, x, y, light_color);
            const RgbF direct = received * falloff;
            data.colors.at(x, y) += direct;
            seedIndirectSpill(
                x, y, direct, data.at(x, y), light.indirect_strength * indirect_scale);
        }
    }
}

void LightPropagator::applyLocalIndirectSpill(WorldData& data, bool air_fast_path)
{
    if (!has_spill_seed_) {
        return;
    }

    constexpr int kSpillSteps = 2;
    const int width = data.width;
    const int height = data.height;
    for (int step = 0; step < kSpillSteps; ++step) {
        spill_field_next_.clear();
        propagateFieldStep(data, air_fast_path, spill_field_, spill_field_next_);
        std::swap(spill_field_, spill_field_next_);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                data.colors.at(x, y) += spill_field_.at(x, y).total();
            }
        }
    }
}

void LightPropagator::seedIndirectSpill(
    int x, int y, const ColorNames::RgbF& direct_light, const Cell& cell, float indirect_strength)
{
    if (indirect_strength <= 0.0f) {
        return;
    }

    const auto& props = cell.material().light;
    const float fill = cell.fill_ratio;
    const float effective_opacity = props.opacity * fill;
    if (effective_opacity < 0.02f || props.scatter <= 0.0f) {
        return;
    }

    const ColorNames::RgbF white{ 1.0f, 1.0f, 1.0f };
    const ColorNames::RgbF tint = ColorNames::lerp(white, ColorNames::toRgbF(props.tint), fill);
    const float scatter_strength = effective_opacity * props.scatter * indirect_strength;
    const ColorNames::RgbF spill = direct_light * tint * scatter_strength;
    if (spill.r < 0.001f && spill.g < 0.001f && spill.b < 0.001f) {
        return;
    }

    auto& dst = spill_field_.at(x, y);
    const ColorNames::RgbF per_dir = spill * (1.0f / 8.0f);
    for (int di = 0; di < 8; ++di) {
        dst.channel[di] += per_dir;
    }
    has_spill_seed_ = true;
}

float LightPropagator::getSpotAngularFactor(
    const Vector2f& light_pos,
    float direction,
    float arc_width,
    float focus,
    const Vector2f& target_pos) const
{
    if (arc_width <= 0.0f) {
        return 0.0f;
    }

    const Vector2f to_target = target_pos - light_pos;
    const float target_angle = std::atan2(to_target.y, to_target.x);

    float angle_diff = target_angle - direction;
    while (angle_diff > M_PI) {
        angle_diff -= 2.0f * static_cast<float>(M_PI);
    }
    while (angle_diff < -M_PI) {
        angle_diff += 2.0f * static_cast<float>(M_PI);
    }

    const float half_arc = arc_width * 0.5f;
    const float abs_diff = std::abs(angle_diff);
    if (abs_diff > half_arc) {
        return 0.0f;
    }

    const float norm_angle = abs_diff / half_arc;
    return std::pow(1.0f - norm_angle, focus);
}

ColorNames::RgbF LightPropagator::traceRay(
    const GridOfCells& grid,
    const WorldData& data,
    float x0,
    float y0,
    int x1,
    int y1,
    ColorNames::RgbF color) const
{
    const int width = grid.getWidth();
    const int height = grid.getHeight();
    const ColorNames::RgbF white{ 1.0f, 1.0f, 1.0f };
    const float target_x = static_cast<float>(x1) + 0.5f;
    const float target_y = static_cast<float>(y1) + 0.5f;
    const float dx = target_x - x0;
    const float dy = target_y - y0;
    const float dist = std::sqrt(dx * dx + dy * dy);

    if (dist < 0.001f) {
        return color;
    }

    const float dir_x = dx / dist;
    const float dir_y = dy / dist;
    constexpr float kEpsilon = 1e-5f;
    const float x0_adj = x0 + dir_x * kEpsilon;
    const float y0_adj = y0 + dir_y * kEpsilon;

    int cell_x = static_cast<int>(std::floor(x0_adj));
    int cell_y = static_cast<int>(std::floor(y0_adj));
    const int step_x = (dir_x > 0.0f) ? 1 : -1;
    const int step_y = (dir_y > 0.0f) ? 1 : -1;
    const float tDeltaX = (dir_x != 0.0f) ? std::abs(1.0f / dir_x) : 1e9f;
    const float tDeltaY = (dir_y != 0.0f) ? std::abs(1.0f / dir_y) : 1e9f;

    float tMaxX = 1e9f;
    if (dir_x > 0.0f) {
        tMaxX = (std::floor(x0_adj) + 1.0f - x0_adj) / dir_x;
    }
    else if (dir_x < 0.0f) {
        tMaxX = (x0_adj - std::floor(x0_adj)) / -dir_x;
    }

    float tMaxY = 1e9f;
    if (dir_y > 0.0f) {
        tMaxY = (std::floor(y0_adj) + 1.0f - y0_adj) / dir_y;
    }
    else if (dir_y < 0.0f) {
        tMaxY = (y0_adj - std::floor(y0_adj)) / -dir_y;
    }

    const int max_steps = width + height;
    for (int step = 0; step < max_steps; ++step) {
        if (cell_x == x1 && cell_y == y1) {
            break;
        }

        if (cell_x < 0 || cell_x >= width || cell_y < 0 || cell_y >= height) {
            return {};
        }

        const uint64_t packed = grid.getMaterialNeighborhood(cell_x, cell_y).raw();
        const Material::EnumType mat = static_cast<Material::EnumType>((packed >> 16) & 0xF);
        const auto& light_props = Material::getProperties(mat).light;
        const Cell& cell = data.cells[static_cast<size_t>(cell_y) * width + cell_x];
        const float fill = cell.fill_ratio;
        const float effective_opacity = light_props.opacity * fill;
        const float transmittance = 1.0f - effective_opacity;
        color *= transmittance;

        const ColorNames::RgbF base_tint = ColorNames::toRgbF(light_props.tint);
        const ColorNames::RgbF effective_tint = ColorNames::lerp(white, base_tint, fill);
        color *= effective_tint;

        if (color.r < 0.001f && color.g < 0.001f && color.b < 0.001f) {
            return {};
        }

        if (tMaxX < tMaxY) {
            tMaxX += tDeltaX;
            cell_x += step_x;
        }
        else {
            tMaxY += tDeltaY;
            cell_y += step_y;
        }
    }

    return color;
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
    World& world, const GridOfCells& grid, const LightConfig& config, Timers& timers)
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
            clearLocalSpillState();

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

            ScopeTimer directTimer(timers, "light_direct_local");
            applyDirectLocalLights(world, grid, config.local_light_indirect_scale);

            ScopeTimer spillTimer(timers, "light_direct_local_spill");
            applyLocalIndirectSpill(data, config.air_fast_path);
            ambient_boost_ = {};
            break;
        }
        case LightMode::FlatBasic: {
            ScopeTimer t(timers, "light_flat_basic");
            applyFlatBasic(data);
            ambient_boost_ = {};
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
