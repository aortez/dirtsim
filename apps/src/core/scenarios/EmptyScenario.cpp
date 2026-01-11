#include "EmptyScenario.h"
#include "core/Cell.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"

namespace DirtSim {

EmptyScenario::EmptyScenario()
{
    metadata_.name = "Empty";
    metadata_.description = "A completely empty world with no particles";
    metadata_.category = "sandbox";
}

const ScenarioMetadata& EmptyScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig EmptyScenario::getConfig() const
{
    return config_;
}

void EmptyScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    // Validate type and update.
    if (std::holds_alternative<Config::Empty>(newConfig)) {
        config_ = std::get<Config::Empty>(newConfig);
        spdlog::info("EmptyScenario: Config updated");
    }
    else {
        spdlog::error("EmptyScenario: Invalid config type provided");
    }
}

void EmptyScenario::setup(World& world)
{
    spdlog::info("EmptyScenario::setup - clearing world");

    // Clear world to empty state.
    for (int y = 0; y < world.getData().height; ++y) {
        for (int x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell(); // Reset to empty cell.
        }
    }

    spdlog::info("EmptyScenario::setup complete");
}

void EmptyScenario::reset(World& world)
{
    spdlog::info("EmptyScenario::reset");
    setup(world);
}

void EmptyScenario::tick(World& /*world*/, double /*deltaTime*/)
{
    // Intentionally empty - no dynamic particles.
}

} // namespace DirtSim
