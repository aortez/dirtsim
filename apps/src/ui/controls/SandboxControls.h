#pragma once

#include "ScenarioControlsBase.h"
#include "ToggleSlider.h"
#include "core/ScenarioConfig.h"
#include "lvgl/lvgl.h"
#include <memory>

namespace DirtSim {

namespace Network {
class WebSocketService;
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
        lv_obj_t* container, Network::WebSocketService* wsService, const SandboxConfig& config);
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
    std::unique_ptr<ToggleSlider> rainControl_;

    // World dimensions for seed placement.
    uint32_t worldWidth_ = 28;
    uint32_t worldHeight_ = 28;

    // Event handlers.
    static void onAddSeedClicked(lv_event_t* e);
    static void onDropDirtBallClicked(lv_event_t* e);
    static void onQuadrantToggled(lv_event_t* e);
    static void onWaterColumnToggled(lv_event_t* e);
    static void onRightThrowToggled(lv_event_t* e);

    // ToggleSlider callbacks (member functions, not static LVGL callbacks).
    void onRainToggled(bool enabled);
    void onRainSliderChanged(int value);

    /**
     * @brief Get the current complete config from all controls.
     */
    SandboxConfig getCurrentConfig() const;
};

} // namespace Ui
} // namespace DirtSim
