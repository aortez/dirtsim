#pragma once

#include "ClockControls.h"
#include "ScenarioControlsBase.h"
#include "core/ScenarioConfig.h"
#include <memory>
#include <string>

namespace DirtSim {

namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

/**
 * @brief Factory for creating scenario-specific controls.
 *
 * Uses std::visit on ScenarioConfig variant to create appropriate controls.
 * Returns nullptr for configs with no UI (Config::Empty, Config::Benchmark, etc.).
 */
class ScenarioControlsFactory {
public:
    /**
     * @brief Create controls for the given scenario config.
     * @param parent Parent LVGL container.
     * @param wsService WebSocket service for server communication.
     * @param scenarioId The scenario identifier.
     * @param config The scenario configuration variant.
     * @param dimensionsGetter Optional callback for display dimensions (used by Clock).
     * @return Unique pointer to controls, or nullptr if scenario has no UI.
     */
    static std::unique_ptr<ScenarioControlsBase> create(
        lv_obj_t* parent,
        Network::WebSocketServiceInterface* wsService,
        Scenario::EnumType scenarioId,
        const ScenarioConfig& config,
        DisplayDimensionsGetter dimensionsGetter = nullptr);
};

} // namespace Ui
} // namespace DirtSim
