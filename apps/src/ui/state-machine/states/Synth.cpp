#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"
#include <string>

namespace DirtSim {
namespace Ui {
namespace State {

Synth::~Synth() = default;

void Synth::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Synth state");

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    uiManager->getMainMenuContainer();
    lv_obj_t* contentArea = uiManager->getMenuContentArea();
    DIRTSIM_ASSERT(contentArea, "Synth state requires a menu content area");

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
    keyboard_.setVolumePercent(sm.getSynthVolumePercent());
    bottomRow_ = nullptr;

    if (auto* panel = uiManager->getExpandablePanel()) {
        panel->hide();
        panel->clearContent();
        panel->resetWidth();
    }

    IconRail* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->setVisible(true);
    iconRail->setLayout(RailLayout::SingleColumn);
    iconRail->setMinimizedAffordanceStyle(IconRail::minimizedAffordanceLeftBottomSquare());
    iconRail->setVisibleIcons({ IconId::DUCK, IconId::MUSIC });
    iconRail->deselectAll();
}

void Synth::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting Synth state");

    keyboard_.destroy();

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

State::Any Synth::onEvent(const IconSelectedEvent& evt, StateMachine& /*sm*/)
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

    if (evt.selectedId == IconId::MUSIC) {
        LOG_INFO(State, "Music icon selected, opening SynthConfig");
        return SynthConfig{};
    }

    if (evt.selectedId == IconId::NONE) {
        return std::move(*this);
    }

    DIRTSIM_ASSERT(false, "Unexpected icon selection in Synth state");
    return std::move(*this);
}

State::Any Synth::onEvent(const RailModeChangedEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

State::Any Synth::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Stop button clicked, returning to StartMenu");
    return StartMenu{};
}

State::Any Synth::onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "SimStop command received, returning to StartMenu");
    cwc.sendResponse(UiApi::SimStop::Response::okay({ true }));
    return StartMenu{};
}

State::Any Synth::onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm)
{
    LOG_INFO(State, "StopButtonPress command received, returning to StartMenu");
    cwc.sendResponse(UiApi::StopButtonPress::Response::okay(std::monostate{}));
    return onEvent(StopButtonClickedEvent{}, sm);
}

State::Any Synth::onEvent(const UiApi::SynthKeyEvent::Cwc& cwc, StateMachine& /*sm*/)
{
    std::string error;
    if (!keyboard_.handleKeyEvent(
            cwc.command.key_index, cwc.command.is_black, cwc.command.is_pressed, "api", error)) {
        cwc.sendResponse(UiApi::SynthKeyEvent::Response::error(ApiError(error)));
        return std::move(*this);
    }

    UiApi::SynthKeyEvent::Okay response{
        .key_index = cwc.command.key_index,
        .is_black = cwc.command.is_black,
        .is_pressed = cwc.command.is_pressed,
    };
    cwc.sendResponse(UiApi::SynthKeyEvent::Response::okay(response));
    return std::move(*this);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
