#pragma once

#include "core/ScenarioConfig.h"
#include "lvgl/lvgl.h"

namespace DirtSim {

// Forward declaration.
namespace Network {
class WebSocketService;
}

namespace Ui {

/**
 * @brief Sandbox scenario-specific controls.
 *
 * Includes: Add Seed, Drop Dirt Ball, Quadrant, Water Column, Right Throw toggles.
 */
class SandboxControls {
public:
    SandboxControls(
        lv_obj_t* container, Network::WebSocketService* wsService, const SandboxConfig& config);
    ~SandboxControls();

    /**
     * @brief Update controls from sandbox configuration.
     */
    void updateFromConfig(const SandboxConfig& config);

    /**
     * @brief Update world dimensions for accurate seed placement.
     */
    void updateWorldDimensions(uint32_t width, uint32_t height);

private:
    lv_obj_t* container_;
    Network::WebSocketService* wsService_;

    // Flag to prevent updates during initialization
    bool initializing_ = true;

    // Widgets.
    lv_obj_t* addSeedButton_ = nullptr;
    lv_obj_t* dropDirtBallButton_ = nullptr;
    lv_obj_t* quadrantSwitch_ = nullptr;
    lv_obj_t* waterColumnSwitch_ = nullptr;
    lv_obj_t* rightThrowSwitch_ = nullptr;
    lv_obj_t* rainControl_ = nullptr;

    // World dimensions for seed placement.
    uint32_t worldWidth_ = 28;
    uint32_t worldHeight_ = 28;

    // Event handlers.
    static void onAddSeedClicked(lv_event_t* e);
    static void onDropDirtBallClicked(lv_event_t* e);
    static void onQuadrantToggled(lv_event_t* e);
    static void onWaterColumnToggled(lv_event_t* e);
    static void onRightThrowToggled(lv_event_t* e);
    static void onRainToggled(lv_event_t* e);
    static void onRainSliderChanged(lv_event_t* e);

    /**
     * @brief Get the current complete config from all controls.
     */
    SandboxConfig getCurrentConfig() const;

    /**
     * @brief Send scenario config update to server.
     */
    void sendConfigUpdate(const ScenarioConfig& config);
};

} // namespace Ui
} // namespace DirtSim
