#include "ScenarioPanel.h"
#include "ScenarioControlsFactory.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/ScenarioSwitch.h"
#include "ui/ScenarioMetadataManager.h"
#include "ui/UiServices.h"
#include "ui/ui_builders/LVGLBuilder.h"

#include <atomic>

namespace DirtSim {
namespace Ui {

namespace {

std::string getScenarioDisplayName(
    const ScenarioMetadataManager& scenarioMetadataManager, Scenario::EnumType scenarioId)
{
    for (const auto& meta : scenarioMetadataManager.scenarios()) {
        if (meta.id == scenarioId) {
            return meta.name;
        }
    }

    return Scenario::toString(scenarioId);
}

} // namespace

ScenarioPanel::ScenarioPanel(
    lv_obj_t* container,
    Network::WebSocketServiceInterface* wsService,
    UiServices& uiServices,
    EventSink& eventSink,
    Scenario::EnumType initialScenarioId,
    const ScenarioConfig& initialConfig,
    DisplayDimensionsGetter dimensionsGetter)
    : container_(container),
      wsService_(wsService),
      uiServices_(uiServices),
      eventSink_(eventSink),
      dimensionsGetter_(dimensionsGetter),
      currentScenarioId_(initialScenarioId),
      currentScenarioConfig_(initialConfig)
{
    // Create view controller.
    viewController_ = std::make_unique<PanelViewController>(container_);

    // Create main view.
    lv_obj_t* mainView = viewController_->createView("main");
    createMainView(mainView);

    // Create scenario selection view.
    lv_obj_t* selectionView = viewController_->createView("selection");
    createScenarioSelectionView(selectionView);

    // Show main view initially.
    viewController_->showView("main");

    LOG_INFO(
        Controls, "ScenarioPanel: Initialized with scenario '{}'", toString(initialScenarioId));
}

ScenarioPanel::~ScenarioPanel()
{
    LOG_INFO(Controls, "ScenarioPanel: Destroyed");
}

void ScenarioPanel::createMainView(lv_obj_t* view)
{
    // Scenario selection button.
    std::string buttonText = "Scenario: "
        + getScenarioDisplayName(uiServices_.scenarioMetadataManager(), currentScenarioId_);
    scenarioButton_ = LVGLBuilder::actionButton(view)
                          .text(buttonText.c_str())
                          .icon(LV_SYMBOL_RIGHT)
                          .width(LV_PCT(95))
                          .height(LVGLBuilder::Style::ACTION_SIZE)
                          .layoutRow()
                          .alignLeft()
                          .callback(onScenarioButtonClicked, this)
                          .buildOrLog();

    // Create scenario-specific controls.
    scenarioControls_ = ScenarioControlsFactory::create(
        view,
        wsService_,
        uiServices_.userSettingsManager(),
        &eventSink_,
        currentScenarioId_,
        currentScenarioConfig_,
        dimensionsGetter_);
}

void ScenarioPanel::createScenarioSelectionView(lv_obj_t* view)
{
    // Back button.
    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onBackClicked, this)
        .buildOrLog();

    // Title.
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Scenario");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    // Scenario option buttons.
    buttonToScenarioId_.clear();
    const auto& scenarios = uiServices_.scenarioMetadataManager().scenarios();
    for (const auto& scenario : scenarios) {
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(scenario.name.c_str())
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();

        if (container) {
            // Get the inner button.
            lv_obj_t* button = lv_obj_get_child(container, 0);
            if (button) {
                buttonToScenarioId_[button] = scenario.id;
                lv_obj_add_event_cb(button, onScenarioSelected, LV_EVENT_CLICKED, this);
            }
        }
    }
}

void ScenarioPanel::updateFromConfig(Scenario::EnumType scenarioId, const ScenarioConfig& config)
{
    // Handle scenario changes.
    if (scenarioId != currentScenarioId_) {
        LOG_INFO(Controls, "ScenarioPanel: Scenario changed to '{}'", toString(scenarioId));

        // Update button text.
        if (scenarioButton_) {
            lv_obj_t* button = lv_obj_get_child(scenarioButton_, 0);
            if (button) {
                lv_obj_t* label =
                    lv_obj_get_child(button, 1); // Second child is text (first is icon).
                if (label) {
                    std::string buttonText = "Scenario: "
                        + getScenarioDisplayName(uiServices_.scenarioMetadataManager(), scenarioId);
                    lv_label_set_text(label, buttonText.c_str());
                }
            }
        }

        // Clear old scenario controls.
        scenarioControls_.reset();

        currentScenarioId_ = scenarioId;
    }

    // Store the config.
    currentScenarioConfig_ = config;

    // Create scenario controls if they don't exist.
    if (!scenarioControls_) {
        lv_obj_t* mainView = viewController_->getView("main");
        if (mainView) {
            scenarioControls_ = ScenarioControlsFactory::create(
                mainView,
                wsService_,
                uiServices_.userSettingsManager(),
                &eventSink_,
                currentScenarioId_,
                currentScenarioConfig_,
                dimensionsGetter_);
        }
    }

    // Always update scenario controls with latest config.
    if (scenarioControls_) {
        scenarioControls_->updateFromConfig(config);
    }
}

void ScenarioPanel::onScenarioButtonClicked(lv_event_t* e)
{
    ScenarioPanel* self = static_cast<ScenarioPanel*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    LOG_DEBUG(Controls, "ScenarioPanel: Scenario button clicked");
    self->viewController_->showView("selection");
}

void ScenarioPanel::onScenarioSelected(lv_event_t* e)
{
    ScenarioPanel* self = static_cast<ScenarioPanel*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Look up scenario index from button mapping.
    auto it = self->buttonToScenarioId_.find(btn);
    if (it == self->buttonToScenarioId_.end()) {
        LOG_ERROR(Controls, "ScenarioPanel: Unknown scenario button clicked");
        return;
    }

    Scenario::EnumType scenarioId = it->second;
    LOG_INFO(Controls, "ScenarioPanel: Scenario changed to '{}'", toString(scenarioId));

    // Return to main view.
    if (self->viewController_) {
        self->viewController_->showView("main");
    }

    // Send ScenarioSwitch to server.
    if (self->wsService_ && self->wsService_->isConnected()) {
        static std::atomic<uint64_t> nextId{ 1 };
        const DirtSim::Api::ScenarioSwitch::Command cmd{ .scenario_id = scenarioId };

        auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);
        auto result = self->wsService_->sendBinary(Network::serialize_envelope(envelope));
        if (result.isError()) {
            LOG_ERROR(
                Controls, "ScenarioPanel: Failed to send ScenarioSwitch: {}", result.errorValue());
        }
    }
    else {
        LOG_WARN(Controls, "ScenarioPanel: WebSocket not connected, cannot switch scenario");
    }
}

void ScenarioPanel::onBackClicked(lv_event_t* e)
{
    ScenarioPanel* self = static_cast<ScenarioPanel*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    LOG_DEBUG(Controls, "ScenarioPanel: Back button clicked");
    self->viewController_->showView("main");
}

} // namespace Ui
} // namespace DirtSim
