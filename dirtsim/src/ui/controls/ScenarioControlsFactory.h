#pragma once

#include "ScenarioControlsBase.h"
#include "core/ScenarioConfig.h"
#include <memory>
#include <string>

namespace DirtSim {

namespace Network {
class WebSocketService;
}

namespace Ui {

/**
 * @brief Factory for creating scenario-specific controls.
 *
 * Uses std::visit on ScenarioConfig variant to create appropriate controls.
 * Returns nullptr for configs with no UI (EmptyConfig, BenchmarkConfig, etc.).
 */
class ScenarioControlsFactory {
public:
    /**
     * @brief Create controls for the given scenario config.
     * @param parent Parent LVGL container.
     * @param wsService WebSocket service for server communication.
     * @param scenarioId The scenario ID string.
     * @param config The scenario configuration variant.
     * @return Unique pointer to controls, or nullptr if scenario has no UI.
     */
    static std::unique_ptr<ScenarioControlsBase> create(
        lv_obj_t* parent,
        Network::WebSocketService* wsService,
        const std::string& scenarioId,
        const ScenarioConfig& config);
};

} // namespace Ui
} // namespace DirtSim
