#include "ColorShowcaseEvent.h"
#include "core/MaterialType.h"
#include "spdlog/spdlog.h"

namespace DirtSim {
namespace ClockEvents {

MaterialType startColorShowcase(
    ColorShowcaseEventState& state,
    const std::vector<MaterialType>& showcase_materials,
    std::mt19937& rng,
    const std::string& current_time)
{
    // Initialize time tracking to current time so we don't change color immediately.
    state.last_seen_time = current_time;

    if (showcase_materials.empty()) {
        state.current_index = 0;
        return MaterialType::METAL;
    }

    // Start on a random color each time.
    std::uniform_int_distribution<size_t> color_dist(0, showcase_materials.size() - 1);
    state.current_index = color_dist(rng);
    return showcase_materials[state.current_index];
}

std::optional<MaterialType> updateColorShowcase(
    ColorShowcaseEventState& state,
    const std::vector<MaterialType>& showcase_materials,
    const std::string& current_time)
{
    // Use state's own time tracking to avoid interaction with other events
    // that might take over rendering (like digit slide or marquee).
    if (current_time != state.last_seen_time && !showcase_materials.empty()) {
        state.last_seen_time = current_time;

        // Advance to next showcase color.
        state.current_index = (state.current_index + 1) % showcase_materials.size();
        MaterialType new_material = showcase_materials[state.current_index];

        spdlog::info("ClockScenario: COLOR_SHOWCASE changed to {} (time changed to {})",
            getMaterialName(new_material), current_time);

        return new_material;
    }

    return std::nullopt;
}

} // namespace ClockEvents
} // namespace DirtSim
