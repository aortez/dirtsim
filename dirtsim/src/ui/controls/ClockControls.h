#pragma once

#include "ScenarioControlsBase.h"
#include "core/ScenarioConfig.h"
#include "lvgl/lvgl.h"
#include <functional>

namespace DirtSim {

namespace Network {
class WebSocketService;
}

namespace Ui {

/**
 * @brief Display dimensions for auto-scaling scenarios.
 */
struct DisplayDimensions {
    uint32_t width = 0;
    uint32_t height = 0;
};

/**
 * @brief Callback to get current world display area dimensions.
 */
using DisplayDimensionsGetter = std::function<DisplayDimensions()>;

/**
 * @brief Clock scenario-specific controls.
 *
 * Includes: Font dropdown, Timezone dropdown, Show Seconds toggle.
 */
class ClockControls : public ScenarioControlsBase {
public:
    ClockControls(
        lv_obj_t* container,
        Network::WebSocketService* wsService,
        const ClockConfig& config,
        DisplayDimensionsGetter dimensionsGetter = nullptr);
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

    // Current config (cached from last updateFromConfig call).
    ClockConfig currentConfig_;

    // Callback to get current display dimensions for auto-scaling.
    DisplayDimensionsGetter dimensionsGetter_;

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
