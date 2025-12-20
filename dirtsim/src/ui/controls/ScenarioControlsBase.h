#pragma once

#include "core/ScenarioConfig.h"
#include "lvgl/lvgl.h"
#include <string>

namespace DirtSim {

namespace Network {
class WebSocketService;
}

namespace Ui {

/**
 * @brief Base class for scenario-specific controls.
 *
 * Provides:
 * - Container management with automatic cleanup.
 * - Initialization flag to prevent update loops.
 * - Common sendConfigUpdate() implementation with rate limiting.
 * - Template method pattern for widget creation.
 */
class ScenarioControlsBase {
public:
    ScenarioControlsBase(
        lv_obj_t* parentContainer,
        Network::WebSocketService* wsService,
        const std::string& scenarioId);

    virtual ~ScenarioControlsBase();

    // Prevent copying.
    ScenarioControlsBase(const ScenarioControlsBase&) = delete;
    ScenarioControlsBase& operator=(const ScenarioControlsBase&) = delete;

    /**
     * @brief Update controls from server config.
     * Called when WorldData arrives with new config.
     */
    virtual void updateFromConfig(const ScenarioConfig& config) = 0;

    /**
     * @brief Get the scenario ID this controls instance manages.
     */
    const std::string& getScenarioId() const { return scenarioId_; }

protected:
    lv_obj_t* parentContainer_;
    lv_obj_t* controlsContainer_; // Our container, deleted in destructor.
    Network::WebSocketService* wsService_;
    std::string scenarioId_;

    // Flag to prevent update loops during UI sync.
    bool initializing_ = true;

    /**
     * @brief Create the LVGL container for controls.
     * Called by constructor. Sets up flex layout.
     */
    void createContainer();

    /**
     * @brief Subclasses implement this to create their widgets.
     * Called at end of constructor after container is created.
     */
    virtual void createWidgets() = 0;

    /**
     * @brief Send config update to server.
     * Handles rate limiting and WebSocket communication.
     */
    void sendConfigUpdate(const ScenarioConfig& config);

    /**
     * @brief Mark initialization complete. Call at end of subclass constructor.
     */
    void finishInitialization() { initializing_ = false; }

    /**
     * @brief Check if currently initializing (prevents callback loops).
     */
    bool isInitializing() const { return initializing_; }
};

} // namespace Ui
} // namespace DirtSim
