#pragma once

#include "ScenarioControlsBase.h"
#include "core/ScenarioConfig.h"
#include "lvgl/lvgl.h"

namespace DirtSim {

namespace Network {
class WebSocketService;
}

namespace Ui {

/**
 * @brief Raining scenario-specific controls.
 *
 * Includes: Rain Rate slider, Puddle Floor toggle.
 */
class RainingControls : public ScenarioControlsBase {
public:
    RainingControls(
        lv_obj_t* container, Network::WebSocketService* wsService, const RainingConfig& config);
    ~RainingControls() override;

    /**
     * @brief Update controls from scenario configuration.
     */
    void updateFromConfig(const ScenarioConfig& config) override;

protected:
    /**
     * @brief Create LVGL widgets for raining controls.
     */
    void createWidgets() override;

private:
    // Widgets.
    lv_obj_t* rainControl_ = nullptr;      // ToggleSlider for rain rate.
    lv_obj_t* puddleFloorSwitch_ = nullptr; // Toggle for puddle floor.

    // Event handlers.
    static void onRainToggled(lv_event_t* e);
    static void onRainSliderChanged(lv_event_t* e);
    static void onPuddleFloorToggled(lv_event_t* e);

    /**
     * @brief Get the current complete config from all controls.
     */
    RainingConfig getCurrentConfig() const;
};

} // namespace Ui
} // namespace DirtSim
