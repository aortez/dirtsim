#pragma once

#include "ScenarioControlsBase.h"
#include "core/ScenarioConfig.h"
#include "lvgl/lvgl.h"
#include "ui/PanelViewController.h"
#include <functional>
#include <memory>
#include <unordered_map>

namespace DirtSim {

namespace Network {
class WebSocketServiceInterface;
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
 * Includes: Font selector (modal), Timezone selector (modal), Show Seconds toggle.
 */
class ClockControls : public ScenarioControlsBase {
public:
    ClockControls(
        lv_obj_t* container,
        Network::WebSocketServiceInterface* wsService,
        const Config::Clock& config,
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
    // View controller for modal navigation.
    std::unique_ptr<PanelViewController> viewController_;

    // Widgets.
    lv_obj_t* fontButton_ = nullptr;
    lv_obj_t* targetDigitHeightPercentStepper_ = nullptr;
    lv_obj_t* timezoneButton_ = nullptr;
    lv_obj_t* digitMaterialButton_ = nullptr;
    lv_obj_t* emissivenessStepper_ = nullptr;
    lv_obj_t* secondsSwitch_ = nullptr;
    lv_obj_t* meltdownSwitch_ = nullptr;
    lv_obj_t* colorCycleSwitch_ = nullptr;
    lv_obj_t* colorShowcaseSwitch_ = nullptr;
    lv_obj_t* digitSlideSwitch_ = nullptr;
    lv_obj_t* marqueeSwitch_ = nullptr;
    lv_obj_t* rainSwitch_ = nullptr;
    lv_obj_t* duckSwitch_ = nullptr;

    // Button to option index mappings.
    std::unordered_map<lv_obj_t*, int> buttonToFontIndex_;
    std::unordered_map<lv_obj_t*, int> buttonToTimezoneIndex_;
    std::unordered_map<lv_obj_t*, int> buttonToMaterialIndex_;

    // Current selections.
    int currentFontIndex_ = 0;
    int currentTimezoneIndex_ = 0;
    int currentMaterialIndex_ = static_cast<int>(Material::EnumType::Metal);

    // Current config (cached from last updateFromConfig call).
    Config::Clock currentConfig_;

    // Callback to get current display dimensions for auto-scaling.
    DisplayDimensionsGetter dimensionsGetter_;

    // View creation.
    void createMainView(lv_obj_t* view);
    void createFontSelectionView(lv_obj_t* view);
    void createTimezoneSelectionView(lv_obj_t* view);
    void createDigitMaterialSelectionView(lv_obj_t* view);

    // Static LVGL callbacks.
    static void onFontButtonClicked(lv_event_t* e);
    static void onFontSelected(lv_event_t* e);
    static void onFontBackClicked(lv_event_t* e);
    static void onTimezoneButtonClicked(lv_event_t* e);
    static void onTimezoneSelected(lv_event_t* e);
    static void onTimezoneBackClicked(lv_event_t* e);
    static void onTargetDigitHeightPercentChanged(lv_event_t* e);
    static void onDigitMaterialButtonClicked(lv_event_t* e);
    static void onDigitMaterialSelected(lv_event_t* e);
    static void onDigitMaterialBackClicked(lv_event_t* e);
    static void onEmissivenessChanged(lv_event_t* e);
    static void onSecondsToggled(lv_event_t* e);
    static void onMeltdownToggled(lv_event_t* e);
    static void onColorCycleToggled(lv_event_t* e);
    static void onColorShowcaseToggled(lv_event_t* e);
    static void onDigitSlideToggled(lv_event_t* e);
    static void onMarqueeToggled(lv_event_t* e);
    static void onRainToggled(lv_event_t* e);
    static void onDuckToggled(lv_event_t* e);

    Config::Clock getCurrentConfig() const;
};

} // namespace Ui
} // namespace DirtSim
