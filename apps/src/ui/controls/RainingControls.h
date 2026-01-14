#pragma once

#include "ScenarioControlsBase.h"
#include "ToggleSlider.h"
#include "core/ScenarioConfig.h"
#include "lvgl/lvgl.h"
#include <memory>

namespace DirtSim {

namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

/**
 * @brief Raining scenario-specific controls.
 *
 * Includes: Rain Rate slider, Drain Rate slider, Max Fill slider.
 */
class RainingControls : public ScenarioControlsBase {
public:
    RainingControls(
        lv_obj_t* container,
        Network::WebSocketServiceInterface* wsService,
        const Config::Raining& config);
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
    std::unique_ptr<ToggleSlider> rainControl_;
    std::unique_ptr<ToggleSlider> drainSizeControl_;
    std::unique_ptr<ToggleSlider> maxFillControl_;

    // ToggleSlider callbacks (member functions, not static LVGL callbacks).
    void onRainToggled(bool enabled);
    void onRainSliderChanged(int value);
    void onDrainSizeToggled(bool enabled);
    void onDrainSizeSliderChanged(int value);
    void onMaxFillToggled(bool enabled);
    void onMaxFillSliderChanged(int value);

    /**
     * @brief Get the current complete config from all controls.
     */
    Config::Raining getCurrentConfig() const;
};

} // namespace Ui
} // namespace DirtSim
