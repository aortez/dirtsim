#pragma once

#include "server/UserSettings.h"
#include <lvgl/lvgl.h>
#include <memory>
#include <unordered_map>

namespace DirtSim {
namespace Ui {
class PanelViewController;
class UiServices;

class StartMenuSettingsPanel {
public:
    StartMenuSettingsPanel(lv_obj_t* container, UiServices& uiServices);
    ~StartMenuSettingsPanel();

    void applySettings(const DirtSim::UserSettings& settings);

private:
    void createMainView(lv_obj_t* view);
    void createScenarioSelectionView(lv_obj_t* view);
    void createTimezoneSelectionView(lv_obj_t* view);

    void updateDefaultScenarioButtonText();
    void updateIdleTimeoutControl();
    void updateResetButtonEnabled();
    void updateTimezoneButtonText();

    static void onBackToMainClicked(lv_event_t* e);
    static void onDefaultScenarioButtonClicked(lv_event_t* e);
    static void onDefaultScenarioSelected(lv_event_t* e);
    static void onIdleTimeoutChanged(lv_event_t* e);
    static void onResetConfirmToggled(lv_event_t* e);
    static void onResetClicked(lv_event_t* e);
    static void onTrainingTargetChanged(lv_event_t* e);
    static void onTimezoneButtonClicked(lv_event_t* e);
    static void onTimezoneSelected(lv_event_t* e);
    static void onVolumeChanged(lv_event_t* e);

    static void onIdleActionChanged(lv_event_t* e);
    void updateIdleActionDropdown();
    void updateTrainingTargetDropdown();

    lv_obj_t* container_ = nullptr;
    lv_obj_t* defaultScenarioButton_ = nullptr;
    lv_obj_t* idleActionDropdown_ = nullptr;
    lv_obj_t* idleTimeoutControl_ = nullptr;
    lv_obj_t* idleTimeoutSlider_ = nullptr;
    lv_obj_t* idleTimeoutValueLabel_ = nullptr;
    lv_obj_t* resetButton_ = nullptr;
    lv_obj_t* resetConfirmCheckbox_ = nullptr;
    lv_obj_t* trainingTargetDropdown_ = nullptr;
    lv_obj_t* timezoneButton_ = nullptr;
    lv_obj_t* volumeStepper_ = nullptr;
    UiServices& uiServices_;
    std::unordered_map<lv_obj_t*, Scenario::EnumType> buttonToScenarioId_;
    std::unordered_map<lv_obj_t*, int> buttonToTimezoneIndex_;
    std::unique_ptr<PanelViewController> viewController_;
    DirtSim::UserSettings settings_{};
    bool updatingUi_ = false;
};

} // namespace Ui
} // namespace DirtSim
