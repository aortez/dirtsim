#include "ScenarioControlsFactory.h"
#include "ClockControls.h"
#include "RainingControls.h"
#include "SandboxControls.h"
#include "TreeGerminationControls.h"

#include <spdlog/spdlog.h>
#include <type_traits>
#include <variant>

namespace DirtSim {
namespace Ui {

std::unique_ptr<ScenarioControlsBase> ScenarioControlsFactory::create(
    lv_obj_t* parent,
    Network::WebSocketServiceInterface* wsService,
    Scenario::EnumType scenarioId,
    const ScenarioConfig& config,
    DisplayDimensionsGetter dimensionsGetter)
{
    return std::visit(
        [&](auto&& cfg) -> std::unique_ptr<ScenarioControlsBase> {
            using T = std::decay_t<decltype(cfg)>;

            if constexpr (std::is_same_v<T, Config::Sandbox>) {
                spdlog::debug("ScenarioControlsFactory: Creating SandboxControls");
                return std::make_unique<SandboxControls>(parent, wsService, cfg);
            }
            else if constexpr (std::is_same_v<T, Config::Clock>) {
                spdlog::debug("ScenarioControlsFactory: Creating ClockControls");
                return std::make_unique<ClockControls>(parent, wsService, cfg, dimensionsGetter);
            }
            else if constexpr (std::is_same_v<T, Config::Raining>) {
                spdlog::debug("ScenarioControlsFactory: Creating RainingControls");
                return std::make_unique<RainingControls>(parent, wsService, cfg);
            }
            else if constexpr (std::is_same_v<T, Config::TreeGermination>) {
                spdlog::debug("ScenarioControlsFactory: Creating TreeGerminationControls");
                return std::make_unique<TreeGerminationControls>(parent, wsService, cfg);
            }
            else {
                // Config::Empty, Config::Benchmark, Config::DamBreak, Config::FallingDirt,
                // Config::WaterEqualization, etc. - no UI needed yet.
                spdlog::debug(
                    "ScenarioControlsFactory: No controls for scenario '{}'", toString(scenarioId));
                return nullptr;
            }
        },
        config);
}

} // namespace Ui
} // namespace DirtSim
