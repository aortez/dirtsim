#pragma once

#include "server/UserSettings.h"
#include <lvgl/lvgl.h>

namespace DirtSim {
namespace Ui {

class UiServices;

class SearchSettingsPanel {
public:
    SearchSettingsPanel(lv_obj_t* container, UiServices& uiServices);
    ~SearchSettingsPanel();

    void applySettings(const DirtSim::UserSettings& settings);
    void setSearchSettingsAndPersist(const SearchSettings& settings);

private:
    void createControls();
    void patchCurrentSettings();

    static void onMaxSearchedNodeCountChanged(lv_event_t* e);
    static void onStallFrameLimitChanged(lv_event_t* e);
    static void onVelocityPruningChanged(lv_event_t* e);
    static void onBelowScreenPruningChanged(lv_event_t* e);
    static void onGroundedVerticalJumpPrioritizationChanged(lv_event_t* e);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* maxSearchedNodeCountStepper_ = nullptr;
    lv_obj_t* stallFrameLimitStepper_ = nullptr;
    lv_obj_t* velocityPruningSwitch_ = nullptr;
    lv_obj_t* belowScreenPruningSwitch_ = nullptr;
    lv_obj_t* groundedVerticalJumpPrioritizationSwitch_ = nullptr;
    UiServices& uiServices_;
    DirtSim::UserSettings settings_{};
    bool updatingUi_ = false;
};

} // namespace Ui
} // namespace DirtSim
