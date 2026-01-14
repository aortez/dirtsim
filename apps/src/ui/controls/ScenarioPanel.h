#pragma once

#include "ClockControls.h"
#include "ScenarioControlsBase.h"
#include "core/ScenarioConfig.h"
#include "lvgl/lvgl.h"
#include "ui/PanelViewController.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirtSim {

namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

/**
 * @brief Scenario panel with modal navigation.
 *
 * Provides a scenario selector button and scenario-specific controls.
 * Clicking the scenario button opens a full-panel modal view with all available scenarios.
 */
class ScenarioPanel {
public:
    ScenarioPanel(
        lv_obj_t* container,
        Network::WebSocketServiceInterface* wsService,
        Scenario::EnumType initialScenarioId,
        const ScenarioConfig& initialConfig,
        DisplayDimensionsGetter dimensionsGetter);
    ~ScenarioPanel();

    void updateFromConfig(Scenario::EnumType scenarioId, const ScenarioConfig& config);

private:
    lv_obj_t* container_;
    Network::WebSocketServiceInterface* wsService_;
    DisplayDimensionsGetter dimensionsGetter_;

    // View controller for modal navigation.
    std::unique_ptr<PanelViewController> viewController_;

    // Current scenario state.
    Scenario::EnumType currentScenarioId_ = Scenario::EnumType::Empty;
    ScenarioConfig currentScenarioConfig_;

    // Scenario-specific controls.
    std::unique_ptr<ScenarioControlsBase> scenarioControls_;

    // Scenario button (in main view).
    lv_obj_t* scenarioButton_ = nullptr;

    // Scenario button to index mapping.
    std::unordered_map<lv_obj_t*, int> buttonToScenarioIndex_;

    // View creation.
    void createMainView(lv_obj_t* view);
    void createScenarioSelectionView(lv_obj_t* view);

    // Callbacks.
    static void onScenarioButtonClicked(lv_event_t* e);
    static void onScenarioSelected(lv_event_t* e);
    static void onBackClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
