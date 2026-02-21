#include "StartMenuSettingsPanel.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketServiceInterface.h"
#include "core/scenarios/ClockScenario.h"
#include "server/api/UserSettingsGet.h"
#include "server/api/UserSettingsReset.h"
#include "server/api/UserSettingsSet.h"
#include "ui/PanelViewController.h"
#include "ui/ScenarioMetadataCache.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <algorithm>
#include <string>

namespace DirtSim {
namespace Ui {

namespace {

constexpr int kIdleTimeoutMinSeconds = 5;
constexpr int kIdleTimeoutMaxSeconds = 3600;

int timeoutMsToSeconds(int timeoutMs)
{
    const int roundedSeconds = (timeoutMs + 500) / 1000;
    return std::clamp(roundedSeconds, kIdleTimeoutMinSeconds, kIdleTimeoutMaxSeconds);
}

void setActionButtonText(lv_obj_t* buttonContainer, const std::string& text)
{
    if (!buttonContainer) {
        return;
    }

    lv_obj_t* button = lv_obj_get_child(buttonContainer, 0);
    if (!button) {
        return;
    }

    const uint32_t childCount = lv_obj_get_child_cnt(button);
    if (childCount == 0) {
        return;
    }

    // Action buttons with icons have icon label first, then text label.
    const uint32_t textIndex = childCount > 1 ? 1 : 0;
    lv_obj_t* label = lv_obj_get_child(button, textIndex);
    if (!label) {
        return;
    }

    lv_label_set_text(label, text.c_str());
}

void setControlEnabled(lv_obj_t* control, bool enabled)
{
    if (!control) {
        return;
    }

    if (enabled) {
        lv_obj_clear_state(control, LV_STATE_DISABLED);
        lv_obj_set_style_opa(control, LV_OPA_COVER, 0);
        return;
    }

    lv_obj_add_state(control, LV_STATE_DISABLED);
    lv_obj_set_style_opa(control, LV_OPA_50, 0);
}

} // namespace

StartMenuSettingsPanel::StartMenuSettingsPanel(
    lv_obj_t* container, Network::WebSocketServiceInterface* wsService, EventSink& eventSink)
    : container_(container), wsService_(wsService), eventSink_(eventSink)
{
    viewController_ = std::make_unique<PanelViewController>(container_);

    lv_obj_t* mainView = viewController_->createView("main");
    createMainView(mainView);

    lv_obj_t* timezoneView = viewController_->createView("timezone");
    createTimezoneSelectionView(timezoneView);

    lv_obj_t* scenarioView = viewController_->createView("scenario");
    createScenarioSelectionView(scenarioView);

    viewController_->showView("main");

    updateTimezoneButtonText();
    updateDefaultScenarioButtonText();
    updateIdleTimeoutControl();
    updateTrainingTargetDropdown();
    updateResetButtonEnabled();

    LOG_INFO(Controls, "StartMenuSettingsPanel created");
}

StartMenuSettingsPanel::~StartMenuSettingsPanel()
{
    LOG_INFO(Controls, "StartMenuSettingsPanel destroyed");
}

void StartMenuSettingsPanel::createMainView(lv_obj_t* view)
{
    if (!view) {
        return;
    }

    auto createRow = [view]() {
        lv_obj_t* row = lv_obj_create(view);
        lv_obj_set_size(row, LV_PCT(95), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        return row;
    };

    lv_obj_t* resetRow = createRow();
    resetButton_ = LVGLBuilder::actionButton(resetRow)
                       .text("Reset")
                       .mode(LVGLBuilder::ActionMode::Push)
                       .width(120)
                       .height(LVGLBuilder::Style::ACTION_SIZE)
                       .layoutRow()
                       .alignLeft()
                       .backgroundColor(0xCC0000)
                       .callback(onResetClicked, this)
                       .buildOrLog();

    resetConfirmCheckbox_ = lv_checkbox_create(resetRow);
    lv_checkbox_set_text(resetConfirmCheckbox_, "Confirm");
    lv_obj_set_style_text_font(resetConfirmCheckbox_, &lv_font_montserrat_12, 0);
    lv_obj_clear_flag(resetConfirmCheckbox_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(resetConfirmCheckbox_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(resetConfirmCheckbox_, 0, 0);
    lv_obj_set_style_pad_all(resetConfirmCheckbox_, 0, 0);
    lv_obj_set_style_pad_column(resetConfirmCheckbox_, 8, 0);
    lv_obj_add_event_cb(resetConfirmCheckbox_, onResetConfirmToggled, LV_EVENT_VALUE_CHANGED, this);

    timezoneButton_ = LVGLBuilder::actionButton(view)
                          .text("Timezone")
                          .icon(LV_SYMBOL_RIGHT)
                          .width(LV_PCT(95))
                          .height(LVGLBuilder::Style::ACTION_SIZE)
                          .layoutRow()
                          .alignLeft()
                          .callback(onTimezoneButtonClicked, this)
                          .buildOrLog();

    volumeStepper_ = LVGLBuilder::actionStepper(view)
                         .label("Volume")
                         .range(0, 100)
                         .step(1)
                         .value(settings_.volumePercent)
                         .valueFormat("%.0f")
                         .valueScale(1.0)
                         .width(LV_PCT(95))
                         .callback(onVolumeChanged, this)
                         .buildOrLog();

    idleActionDropdown_ = LVGLBuilder::actionDropdown(view)
                              .label("Idle Action:")
                              .options("Clock Scenario\nNone\nTraining Session")
                              .selected(0)
                              .width(LV_PCT(95))
                              .callback(onIdleActionChanged, this)
                              .buildOrLog();

    idleTimeoutControl_ = lv_obj_create(view);
    lv_obj_set_size(idleTimeoutControl_, LV_PCT(95), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(idleTimeoutControl_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        idleTimeoutControl_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(
        idleTimeoutControl_, lv_color_hex(LVGLBuilder::Style::TROUGH_COLOR), 0);
    lv_obj_set_style_bg_opa(idleTimeoutControl_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(idleTimeoutControl_, 0, 0);
    lv_obj_set_style_pad_all(idleTimeoutControl_, LVGLBuilder::Style::TROUGH_PADDING, 0);
    lv_obj_set_style_pad_row(idleTimeoutControl_, 6, 0);
    lv_obj_clear_flag(idleTimeoutControl_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* idleTimeoutHeader = lv_obj_create(idleTimeoutControl_);
    lv_obj_set_size(idleTimeoutHeader, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(idleTimeoutHeader, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        idleTimeoutHeader, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(idleTimeoutHeader, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(idleTimeoutHeader, 0, 0);
    lv_obj_set_style_pad_all(idleTimeoutHeader, 0, 0);
    lv_obj_set_style_pad_column(idleTimeoutHeader, 8, 0);
    lv_obj_clear_flag(idleTimeoutHeader, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* idleTimeoutLabel = lv_label_create(idleTimeoutHeader);
    lv_label_set_text(idleTimeoutLabel, "Idle Timeout:");
    lv_obj_set_style_text_color(idleTimeoutLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(idleTimeoutLabel, LVGLBuilder::Style::CONTROL_FONT, 0);

    idleTimeoutValueLabel_ = lv_label_create(idleTimeoutHeader);
    lv_obj_set_style_text_color(idleTimeoutValueLabel_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(idleTimeoutValueLabel_, LVGLBuilder::Style::CONTROL_FONT, 0);

    idleTimeoutSlider_ = lv_slider_create(idleTimeoutControl_);
    lv_obj_set_size(idleTimeoutSlider_, LV_PCT(100), LVGLBuilder::Style::SLIDER_TRACK_HEIGHT);
    lv_slider_set_range(idleTimeoutSlider_, kIdleTimeoutMinSeconds, kIdleTimeoutMaxSeconds);
    lv_slider_set_value(
        idleTimeoutSlider_, timeoutMsToSeconds(settings_.startMenuIdleTimeoutMs), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        idleTimeoutSlider_, lv_color_hex(LVGLBuilder::Style::TROUGH_INNER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_color(idleTimeoutSlider_, lv_color_hex(0x3399FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(idleTimeoutSlider_, lv_color_hex(0x3399FF), LV_PART_KNOB);
    lv_obj_set_style_radius(
        idleTimeoutSlider_, LVGLBuilder::Style::SLIDER_TRACK_HEIGHT / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(
        idleTimeoutSlider_, LVGLBuilder::Style::SLIDER_TRACK_HEIGHT / 2, LV_PART_INDICATOR);
    lv_obj_set_style_radius(
        idleTimeoutSlider_, LVGLBuilder::Style::SLIDER_KNOB_RADIUS, LV_PART_KNOB);
    lv_obj_set_style_pad_all(
        idleTimeoutSlider_,
        LVGLBuilder::Style::SLIDER_KNOB_SIZE / 2 - LVGLBuilder::Style::SLIDER_TRACK_HEIGHT / 2,
        LV_PART_KNOB);
    lv_obj_add_event_cb(idleTimeoutSlider_, onIdleTimeoutChanged, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(idleTimeoutSlider_, onIdleTimeoutChanged, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(idleTimeoutSlider_, onIdleTimeoutChanged, LV_EVENT_PRESS_LOST, this);

    trainingTargetDropdown_ =
        LVGLBuilder::actionDropdown(view)
            .label("Trainer Target:")
            .options("Trees (Germination)\nDucks (Clock Scenario)\nNes Flappy Bird")
            .selected(0)
            .width(LV_PCT(95))
            .callback(onTrainingTargetChanged, this)
            .buildOrLog();

    defaultScenarioButton_ = LVGLBuilder::actionButton(view)
                                 .text("Default Scenario")
                                 .icon(LV_SYMBOL_RIGHT)
                                 .width(LV_PCT(95))
                                 .height(LVGLBuilder::Style::ACTION_SIZE)
                                 .layoutRow()
                                 .alignLeft()
                                 .callback(onDefaultScenarioButtonClicked, this)
                                 .buildOrLog();
}

void StartMenuSettingsPanel::createScenarioSelectionView(lv_obj_t* view)
{
    if (!view) {
        return;
    }

    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onBackToMainClicked, this)
        .buildOrLog();

    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Default Scenario");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    buttonToScenarioIndex_.clear();
    if (!ScenarioMetadataCache::hasScenarios()) {
        lv_obj_t* emptyLabel = lv_label_create(view);
        lv_label_set_text(emptyLabel, "No scenarios loaded.");
        lv_obj_set_style_text_color(emptyLabel, lv_color_hex(0xBBBBBB), 0);
        lv_obj_set_style_text_font(emptyLabel, &lv_font_montserrat_14, 0);
        return;
    }
    const std::vector<std::string> options = ScenarioMetadataCache::buildOptionsList();

    for (size_t i = 0; i < options.size(); ++i) {
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(options[i].c_str())
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();
        if (!container) {
            continue;
        }

        lv_obj_t* button = lv_obj_get_child(container, 0);
        if (!button) {
            continue;
        }

        buttonToScenarioIndex_[button] = static_cast<int>(i);
        lv_obj_add_event_cb(button, onDefaultScenarioSelected, LV_EVENT_CLICKED, this);
    }
}

void StartMenuSettingsPanel::createTimezoneSelectionView(lv_obj_t* view)
{
    if (!view) {
        return;
    }

    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onBackToMainClicked, this)
        .buildOrLog();

    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Timezone");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    buttonToTimezoneIndex_.clear();
    for (size_t i = 0; i < ClockScenario::TIMEZONES.size(); ++i) {
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(ClockScenario::TIMEZONES[i].label)
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();
        if (!container) {
            continue;
        }

        lv_obj_t* button = lv_obj_get_child(container, 0);
        if (!button) {
            continue;
        }

        buttonToTimezoneIndex_[button] = static_cast<int>(i);
        lv_obj_add_event_cb(button, onTimezoneSelected, LV_EVENT_CLICKED, this);
    }
}

void StartMenuSettingsPanel::refreshFromServer()
{
    if (!wsService_ || !wsService_->isConnected()) {
        LOG_WARN(Controls, "StartMenuSettingsPanel: Cannot refresh settings, server disconnected");
        return;
    }

    Api::UserSettingsGet::Command cmd{};
    const auto result =
        wsService_->sendCommandAndGetResponse<Api::UserSettingsGet::Okay>(cmd, 1000);
    if (result.isError()) {
        LOG_WARN(Controls, "UserSettingsGet failed: {}", result.errorValue());
        return;
    }

    if (result.value().isError()) {
        LOG_WARN(Controls, "UserSettingsGet error: {}", result.value().errorValue().message);
        return;
    }

    eventSink_.queueEvent(UserSettingsUpdatedEvent{ .settings = result.value().value().settings });
}

void StartMenuSettingsPanel::applySettings(const DirtSim::UserSettings& settings)
{
    updatingUi_ = true;
    settings_ = settings;

    if (volumeStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(volumeStepper_, settings_.volumePercent);
    }

    if (resetConfirmCheckbox_) {
        lv_obj_clear_state(resetConfirmCheckbox_, LV_STATE_CHECKED);
    }

    updateTimezoneButtonText();
    updateDefaultScenarioButtonText();
    updateIdleActionDropdown();
    updateIdleTimeoutControl();
    updateTrainingTargetDropdown();
    updateResetButtonEnabled();

    updatingUi_ = false;
}

void StartMenuSettingsPanel::sendSettingsUpdate()
{
    if (!wsService_ || !wsService_->isConnected()) {
        LOG_WARN(
            Controls, "StartMenuSettingsPanel: Cannot send settings update, server disconnected");
        return;
    }

    Api::UserSettingsSet::Command cmd{ .settings = settings_ };
    const auto result =
        wsService_->sendCommandAndGetResponse<Api::UserSettingsSet::Okay>(cmd, 1000);
    if (result.isError()) {
        LOG_WARN(Controls, "UserSettingsSet failed: {}", result.errorValue());
        return;
    }

    if (result.value().isError()) {
        LOG_WARN(Controls, "UserSettingsSet error: {}", result.value().errorValue().message);
        return;
    }
}

void StartMenuSettingsPanel::sendSettingsReset()
{
    if (!wsService_ || !wsService_->isConnected()) {
        LOG_WARN(Controls, "StartMenuSettingsPanel: Cannot reset settings, server disconnected");
        return;
    }

    Api::UserSettingsReset::Command cmd{};
    const auto result =
        wsService_->sendCommandAndGetResponse<Api::UserSettingsReset::Okay>(cmd, 1000);
    if (result.isError()) {
        LOG_WARN(Controls, "UserSettingsReset failed: {}", result.errorValue());
        return;
    }

    if (result.value().isError()) {
        LOG_WARN(Controls, "UserSettingsReset error: {}", result.value().errorValue().message);
        return;
    }
}

void StartMenuSettingsPanel::updateDefaultScenarioButtonText()
{
    std::string scenarioName = toString(settings_.defaultScenario);
    const auto info = ScenarioMetadataCache::getScenarioInfo(settings_.defaultScenario);
    if (info.has_value()) {
        scenarioName = info->name;
    }

    setActionButtonText(defaultScenarioButton_, "Default Scenario: " + scenarioName);
}

void StartMenuSettingsPanel::updateIdleTimeoutControl()
{
    if (!idleTimeoutSlider_ || !idleTimeoutValueLabel_) {
        return;
    }

    const int timeoutSeconds = timeoutMsToSeconds(settings_.startMenuIdleTimeoutMs);
    lv_slider_set_value(idleTimeoutSlider_, timeoutSeconds, LV_ANIM_OFF);
    lv_label_set_text(idleTimeoutValueLabel_, (std::to_string(timeoutSeconds) + " s").c_str());

    const bool enabled = settings_.startMenuIdleAction != StartMenuIdleAction::None;
    setControlEnabled(idleTimeoutControl_, enabled);
}

void StartMenuSettingsPanel::updateResetButtonEnabled()
{
    const bool confirmed =
        resetConfirmCheckbox_ && lv_obj_has_state(resetConfirmCheckbox_, LV_STATE_CHECKED);
    setControlEnabled(resetButton_, confirmed);
}

void StartMenuSettingsPanel::updateTimezoneButtonText()
{
    const int maxIndex = static_cast<int>(ClockScenario::TIMEZONES.size()) - 1;
    const int clampedIndex = std::clamp(settings_.timezoneIndex, 0, maxIndex);
    const char* label = ClockScenario::TIMEZONES[clampedIndex].label;
    setActionButtonText(timezoneButton_, std::string("Timezone: ") + label);
}

void StartMenuSettingsPanel::updateIdleActionDropdown()
{
    if (!idleActionDropdown_) {
        return;
    }

    const auto index = static_cast<uint16_t>(settings_.startMenuIdleAction);
    LVGLBuilder::ActionDropdownBuilder::setSelected(idleActionDropdown_, index);
}

void StartMenuSettingsPanel::updateTrainingTargetDropdown()
{
    if (!trainingTargetDropdown_) {
        return;
    }

    uint16_t index = 0;
    if (settings_.trainingSpec.organismType == OrganismType::DUCK) {
        index = 1;
    }
    else if (settings_.trainingSpec.organismType == OrganismType::NES_FLAPPY_BIRD) {
        index = 2;
    }

    LVGLBuilder::ActionDropdownBuilder::setSelected(trainingTargetDropdown_, index);

    const bool enabled = settings_.startMenuIdleAction == StartMenuIdleAction::TrainingSession;
    setControlEnabled(trainingTargetDropdown_, enabled);
}

void StartMenuSettingsPanel::onIdleActionChanged(lv_event_t* e)
{
    auto* self = static_cast<StartMenuSettingsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->idleActionDropdown_) {
        return;
    }

    if (self->updatingUi_) {
        return;
    }

    const uint16_t index =
        LVGLBuilder::ActionDropdownBuilder::getSelected(self->idleActionDropdown_);

    if (index > static_cast<uint16_t>(StartMenuIdleAction::TrainingSession)) {
        self->settings_.startMenuIdleAction = StartMenuIdleAction::ClockScenario;
    }
    else {
        self->settings_.startMenuIdleAction = static_cast<StartMenuIdleAction>(index);
    }

    self->updateIdleTimeoutControl();
    self->updateTrainingTargetDropdown();
    self->sendSettingsUpdate();
}

void StartMenuSettingsPanel::onIdleTimeoutChanged(lv_event_t* e)
{
    auto* self = static_cast<StartMenuSettingsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->idleTimeoutSlider_ || !self->idleTimeoutValueLabel_) {
        return;
    }

    if (self->updatingUi_) {
        return;
    }

    const int timeoutSeconds = std::clamp(
        static_cast<int>(lv_slider_get_value(self->idleTimeoutSlider_)),
        kIdleTimeoutMinSeconds,
        kIdleTimeoutMaxSeconds);
    self->settings_.startMenuIdleTimeoutMs = timeoutSeconds * 1000;
    lv_label_set_text(
        self->idleTimeoutValueLabel_, (std::to_string(timeoutSeconds) + " s").c_str());

    const auto code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        self->sendSettingsUpdate();
    }
}

void StartMenuSettingsPanel::onTrainingTargetChanged(lv_event_t* e)
{
    auto* self = static_cast<StartMenuSettingsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->trainingTargetDropdown_) {
        return;
    }

    if (self->updatingUi_) {
        return;
    }

    const uint16_t index =
        LVGLBuilder::ActionDropdownBuilder::getSelected(self->trainingTargetDropdown_);

    switch (index) {
        case 1:
            self->settings_.trainingSpec.organismType = OrganismType::DUCK;
            self->settings_.trainingSpec.scenarioId = Scenario::EnumType::Clock;
            break;
        case 2:
            self->settings_.trainingSpec.organismType = OrganismType::NES_FLAPPY_BIRD;
            self->settings_.trainingSpec.scenarioId = Scenario::EnumType::Nes;
            break;
        case 0:
        default:
            self->settings_.trainingSpec.organismType = OrganismType::TREE;
            self->settings_.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;
            break;
    }

    self->settings_.trainingSpec.population.clear();
    self->sendSettingsUpdate();
}

void StartMenuSettingsPanel::onBackToMainClicked(lv_event_t* e)
{
    auto* self = static_cast<StartMenuSettingsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) {
        return;
    }

    self->viewController_->showView("main");
}

void StartMenuSettingsPanel::onDefaultScenarioButtonClicked(lv_event_t* e)
{
    auto* self = static_cast<StartMenuSettingsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) {
        return;
    }

    self->viewController_->showView("scenario");
}

void StartMenuSettingsPanel::onDefaultScenarioSelected(lv_event_t* e)
{
    auto* self = static_cast<StartMenuSettingsPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    lv_obj_t* button = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto it = self->buttonToScenarioIndex_.find(button);
    if (it == self->buttonToScenarioIndex_.end()) {
        LOG_WARN(Controls, "StartMenuSettingsPanel: Unknown scenario button clicked");
        return;
    }

    self->settings_.defaultScenario =
        ScenarioMetadataCache::scenarioIdFromIndex(static_cast<uint16_t>(it->second));
    self->updateDefaultScenarioButtonText();

    if (self->viewController_) {
        self->viewController_->showView("main");
    }

    self->sendSettingsUpdate();
}

void StartMenuSettingsPanel::onResetConfirmToggled(lv_event_t* e)
{
    auto* self = static_cast<StartMenuSettingsPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->updateResetButtonEnabled();
}

void StartMenuSettingsPanel::onResetClicked(lv_event_t* e)
{
    auto* self = static_cast<StartMenuSettingsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->resetConfirmCheckbox_) {
        return;
    }

    if (!lv_obj_has_state(self->resetConfirmCheckbox_, LV_STATE_CHECKED)) {
        return;
    }

    self->sendSettingsReset();
}

void StartMenuSettingsPanel::onTimezoneButtonClicked(lv_event_t* e)
{
    auto* self = static_cast<StartMenuSettingsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) {
        return;
    }

    self->viewController_->showView("timezone");
}

void StartMenuSettingsPanel::onTimezoneSelected(lv_event_t* e)
{
    auto* self = static_cast<StartMenuSettingsPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    lv_obj_t* button = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto it = self->buttonToTimezoneIndex_.find(button);
    if (it == self->buttonToTimezoneIndex_.end()) {
        LOG_WARN(Controls, "StartMenuSettingsPanel: Unknown timezone button clicked");
        return;
    }

    self->settings_.timezoneIndex = it->second;
    self->updateTimezoneButtonText();

    if (self->viewController_) {
        self->viewController_->showView("main");
    }

    self->sendSettingsUpdate();
}

void StartMenuSettingsPanel::onVolumeChanged(lv_event_t* e)
{
    auto* self = static_cast<StartMenuSettingsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->volumeStepper_) {
        return;
    }

    if (self->updatingUi_) {
        return;
    }

    const int value = LVGLBuilder::ActionStepperBuilder::getValue(self->volumeStepper_);
    self->settings_.volumePercent = std::clamp(value, 0, 100);
    self->sendSettingsUpdate();
}

} // namespace Ui
} // namespace DirtSim
