#pragma once

#include "ScenarioControlsBase.h"
#include "core/ScenarioConfig.h"
#include "lvgl/lvgl.h"

namespace DirtSim {

namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

/**
 * @brief Sandbox scenario-specific controls.
 *
 * Includes: Add Seed, Drop Dirt Ball, Quadrant, Water Column, Right Throw toggles.
 */
class SandboxControls : public ScenarioControlsBase {
public:
    SandboxControls(
        lv_obj_t* container,
        Network::WebSocketServiceInterface* wsService,
        UserSettingsManager& userSettingsManager,
        const Config::Sandbox& config);
    ~SandboxControls() override;

    /**
     * @brief Update controls from scenario configuration.
     */
    void updateFromConfig(const ScenarioConfig& config) override;

    /**
     * @brief Update world dimensions for accurate seed placement.
     */
    void updateWorldDimensions(uint32_t width, uint32_t height);

protected:
    /**
     * @brief Create LVGL widgets for sandbox controls.
     */
    void createWidgets() override;

private:
    // Widgets.
    lv_obj_t* addSeedButton_ = nullptr;
    lv_obj_t* dropDirtBallButton_ = nullptr;
    lv_obj_t* quadrantSwitch_ = nullptr;
    lv_obj_t* waterColumnSwitch_ = nullptr;
    lv_obj_t* rightThrowSwitch_ = nullptr;
    lv_obj_t* rainStepper_ = nullptr;

    // World dimensions for seed placement.
    uint32_t worldWidth_ = 28;
    uint32_t worldHeight_ = 28;

    // Event handlers.
    static void onAddSeedClicked(lv_event_t* e);
    static void onDropDirtBallClicked(lv_event_t* e);
    static void onQuadrantToggled(lv_event_t* e);
    static void onWaterColumnToggled(lv_event_t* e);
    static void onRightThrowToggled(lv_event_t* e);
    static void onRainChanged(lv_event_t* e);

    /**
     * @brief Get the current complete config from all controls.
     */
    Config::Sandbox getCurrentConfig() const;
};

} // namespace Ui
} // namespace DirtSim
