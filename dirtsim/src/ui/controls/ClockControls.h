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
 * @brief Clock scenario-specific controls.
 *
 * Includes: Timezone dropdown, Show Seconds toggle.
 */
class ClockControls : public ScenarioControlsBase {
public:
    ClockControls(
        lv_obj_t* container, Network::WebSocketService* wsService, const ClockConfig& config);
    ~ClockControls() override;

    /**
     * @brief Update controls from scenario configuration.
     */
    void updateFromConfig(const ScenarioConfig& config) override;

protected:
    /**
     * @brief Create LVGL widgets for clock controls.
     */
    void createWidgets() override;

private:
    // Widgets.
    lv_obj_t* fontDropdown_ = nullptr;
    lv_obj_t* timezoneDropdown_ = nullptr;
    lv_obj_t* secondsSwitch_ = nullptr;

    // Static LVGL callbacks.
    static void onFontChanged(lv_event_t* e);
    static void onTimezoneChanged(lv_event_t* e);
    static void onSecondsToggled(lv_event_t* e);

    /**
     * @brief Get the current complete config from all controls.
     */
    ClockConfig getCurrentConfig() const;
};

} // namespace Ui
} // namespace DirtSim
