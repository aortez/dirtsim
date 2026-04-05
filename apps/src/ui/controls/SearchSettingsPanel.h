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

    static void onSearchDepthChanged(lv_event_t* e);
    static void onMaxSegmentsChanged(lv_event_t* e);
    static void onSegmentFrameBudgetChanged(lv_event_t* e);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* maxSegmentsStepper_ = nullptr;
    lv_obj_t* searchDepthStepper_ = nullptr;
    lv_obj_t* segmentFrameBudgetStepper_ = nullptr;
    UiServices& uiServices_;
    DirtSim::UserSettings settings_{};
    bool updatingUi_ = false;
};

} // namespace Ui
} // namespace DirtSim
