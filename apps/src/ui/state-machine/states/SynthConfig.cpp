#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <algorithm>
#include <string>

namespace DirtSim {
namespace Ui {
namespace State {

void SynthConfig::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering SynthConfig state");

    stateMachine_ = &sm;
    volumePercent_ = sm.getSynthVolumePercent();

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    uiManager->getMainMenuContainer();
    lv_obj_t* contentArea = uiManager->getMenuContentArea();
    DIRTSIM_ASSERT(contentArea, "SynthConfig state requires a menu content area");

    lv_obj_clean(contentArea);

    contentRoot_ = lv_obj_create(contentArea);
    lv_obj_set_size(contentRoot_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(contentRoot_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(contentRoot_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(contentRoot_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(contentRoot_, 0, 0);
    lv_obj_set_style_pad_row(contentRoot_, 0, 0);
    lv_obj_set_style_border_width(contentRoot_, 0, 0);
    lv_obj_clear_flag(contentRoot_, LV_OBJ_FLAG_SCROLLABLE);

    keyboard_.create(contentRoot_);
    keyboard_.setVolumePercent(volumePercent_);
    bottomRow_ = nullptr;

    auto* panel = uiManager->getExpandablePanel();
    DIRTSIM_ASSERT(panel, "SynthConfig state requires an ExpandablePanel");
    panel->clearContent();
    panel->resetWidth();
    panel->show();

    lv_obj_t* panelContent = panel->getContentArea();
    DIRTSIM_ASSERT(panelContent, "SynthConfig state requires ExpandablePanel content area");

    lv_obj_t* column = lv_obj_create(panelContent);
    lv_obj_set_size(column, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(column, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(column, 0, 0);
    lv_obj_set_style_pad_all(column, 0, 0);
    lv_obj_set_style_pad_row(column, 12, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    volumeStepper_ = LVGLBuilder::actionStepper(column)
                         .label("Volume")
                         .range(0, 100)
                         .step(1)
                         .value(volumePercent_)
                         .valueFormat("%.0f")
                         .valueScale(1.0)
                         .width(LV_PCT(95))
                         .callback(onVolumeChanged, this)
                         .buildOrLog();
    DIRTSIM_ASSERT(volumeStepper_, "SynthConfig volume stepper build failed");

    IconRail* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->setLayout(RailLayout::SingleColumn);
    iconRail->setVisibleIcons({ IconId::DUCK, IconId::MUSIC });
    iconRail->selectIcon(IconId::MUSIC);
}

void SynthConfig::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting SynthConfig state");

    keyboard_.destroy();
    stateMachine_ = nullptr;
    volumeStepper_ = nullptr;

    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->clearContent();
            panel->hide();
            panel->resetWidth();
        }
    }

    if (contentRoot_) {
        lv_obj_del(contentRoot_);
        contentRoot_ = nullptr;
        bottomRow_ = nullptr;
    }
}

State::Any SynthConfig::onEvent(const IconSelectedEvent& evt, StateMachine& /*sm*/)
{
    LOG_INFO(
        State,
        "Icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    if (evt.selectedId == IconId::DUCK) {
        LOG_INFO(State, "Duck icon selected, returning to StartMenu");
        return StartMenu{};
    }

    if (evt.selectedId == IconId::NONE) {
        LOG_INFO(State, "Music icon deselected, closing SynthConfig");
        return Synth{};
    }

    if (evt.selectedId == IconId::MUSIC) {
        return std::move(*this);
    }

    DIRTSIM_ASSERT(false, "Unexpected icon selection in SynthConfig state");
    return std::move(*this);
}

State::Any SynthConfig::onEvent(const RailModeChangedEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

State::Any SynthConfig::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Stop button clicked, returning to StartMenu");
    return StartMenu{};
}

State::Any SynthConfig::onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "SimStop command received, returning to StartMenu");
    cwc.sendResponse(UiApi::SimStop::Response::okay({ true }));
    return StartMenu{};
}

State::Any SynthConfig::onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm)
{
    LOG_INFO(State, "StopButtonPress command received, returning to StartMenu");
    cwc.sendResponse(UiApi::StopButtonPress::Response::okay(std::monostate{}));
    return onEvent(StopButtonClickedEvent{}, sm);
}

State::Any SynthConfig::onEvent(const UiApi::SynthKeyPress::Cwc& cwc, StateMachine& /*sm*/)
{
    std::string error;
    if (!keyboard_.handleKeyPress(cwc.command.key_index, cwc.command.is_black, "api", error)) {
        cwc.sendResponse(UiApi::SynthKeyPress::Response::error(ApiError(error)));
        return std::move(*this);
    }

    UiApi::SynthKeyPress::Okay response{
        .key_index = cwc.command.key_index,
        .is_black = cwc.command.is_black,
    };
    cwc.sendResponse(UiApi::SynthKeyPress::Response::okay(response));
    return std::move(*this);
}

void SynthConfig::onVolumeChanged(lv_event_t* e)
{
    auto* self = static_cast<SynthConfig*>(lv_event_get_user_data(e));
    DIRTSIM_ASSERT(self, "SynthConfig volume change handler requires SynthConfig user_data");

    self->updateVolumeFromStepper();
}

void SynthConfig::updateVolumeFromStepper()
{
    DIRTSIM_ASSERT(volumeStepper_, "SynthConfig requires volumeStepper_");

    const int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(volumeStepper_);
    volumePercent_ = std::clamp(static_cast<int>(value), 0, 100);
    keyboard_.setVolumePercent(volumePercent_);

    DIRTSIM_ASSERT(stateMachine_, "SynthConfig requires a valid StateMachine");
    stateMachine_->setSynthVolumePercent(volumePercent_);

    LOG_INFO(State, "Synth volume set to {}", volumePercent_);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
