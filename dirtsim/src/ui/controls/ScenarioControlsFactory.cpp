#include "ScenarioControlsFactory.h"
#include "ClockControls.h"
#include "RainingControls.h"
#include "SandboxControls.h"
#include <spdlog/spdlog.h>
#include <type_traits>
#include <variant>

namespace DirtSim {
namespace Ui {

std::unique_ptr<ScenarioControlsBase> ScenarioControlsFactory::create(
    lv_obj_t* parent,
    Network::WebSocketService* wsService,
    const std::string& scenarioId,
    const ScenarioConfig& config,
    DisplayDimensionsGetter dimensionsGetter)
{
    return std::visit(
        [&](auto&& cfg) -> std::unique_ptr<ScenarioControlsBase> {
            using T = std::decay_t<decltype(cfg)>;

            if constexpr (std::is_same_v<T, SandboxConfig>) {
                spdlog::debug("ScenarioControlsFactory: Creating SandboxControls");
                return std::make_unique<SandboxControls>(parent, wsService, cfg);
            }
            else if constexpr (std::is_same_v<T, ClockConfig>) {
                spdlog::debug("ScenarioControlsFactory: Creating ClockControls");
                return std::make_unique<ClockControls>(parent, wsService, cfg, dimensionsGetter);
            }
            else if constexpr (std::is_same_v<T, RainingConfig>) {
                spdlog::debug("ScenarioControlsFactory: Creating RainingControls");
                return std::make_unique<RainingControls>(parent, wsService, cfg);
            }
            else {
                // EmptyConfig, BenchmarkConfig, DamBreakConfig, FallingDirtConfig,
                // WaterEqualizationConfig, etc. - no UI needed yet.
                spdlog::debug(
                    "ScenarioControlsFactory: No controls for scenario '{}'", scenarioId);
                return nullptr;
            }
        },
        config);
}

} // namespace Ui
} // namespace DirtSim
