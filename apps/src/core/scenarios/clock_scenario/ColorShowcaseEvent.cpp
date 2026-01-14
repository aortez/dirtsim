#include "ColorShowcaseEvent.h"
#include "core/MaterialType.h"
#include "spdlog/spdlog.h"

namespace DirtSim {
namespace ClockEvents {

Material::EnumType startColorShowcase(
    ColorShowcaseEventState& state,
    const std::vector<Material::EnumType>& showcase_materials,
    std::mt19937& rng)
{
    if (showcase_materials.empty()) {
        state.current_index = 0;
        return Material::EnumType::Metal;
    }

    // Start on a random color each time.
    std::uniform_int_distribution<size_t> color_dist(0, showcase_materials.size() - 1);
    state.current_index = color_dist(rng);
    return showcase_materials[state.current_index];
}

std::optional<Material::EnumType> updateColorShowcase(
    ColorShowcaseEventState& state,
    const std::vector<Material::EnumType>& showcase_materials,
    bool time_changed)
{
    // Advance to next showcase color when time changes.
    if (time_changed && !showcase_materials.empty()) {
        state.current_index = (state.current_index + 1) % showcase_materials.size();
        Material::EnumType new_material = showcase_materials[state.current_index];

        spdlog::info("ClockScenario: COLOR_SHOWCASE changed to {}", toString(new_material));

        return new_material;
    }

    return std::nullopt;
}

} // namespace ClockEvents
} // namespace DirtSim
