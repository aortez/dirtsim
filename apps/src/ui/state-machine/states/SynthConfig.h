#pragma once

#include "StateForward.h"
#include "SynthKeyboard.h"
#include "ui/state-machine/Event.h"
#include <lvgl/lvgl.h>

namespace DirtSim {
namespace Ui {
namespace State {

struct SynthConfig {
    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const RailAutoShrinkRequestEvent& evt, StateMachine& sm);
    Any onEvent(const RailModeChangedEvent& evt, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const StopButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SynthKeyPress::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "SynthConfig"; }
    int getLastKeyIndex() const { return keyboard_.getLastKeyIndex(); }
    bool getLastKeyIsBlack() const { return keyboard_.getLastKeyIsBlack(); }

private:
    static void onVolumeChanged(lv_event_t* e);

    void updateVolumeFromStepper();

    lv_obj_t* contentRoot_ = nullptr;
    lv_obj_t* bottomRow_ = nullptr;
    lv_obj_t* volumeStepper_ = nullptr;
    StateMachine* stateMachine_ = nullptr;
    int volumePercent_ = 50;
    SynthKeyboard keyboard_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
