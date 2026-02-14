#pragma once

#include "StateForward.h"
#include "SynthKeyboard.h"
#include "ui/state-machine/Event.h"
#include <lvgl/lvgl.h>

namespace DirtSim {
namespace Ui {
namespace State {

struct Synth {
    Synth() = default;
    Synth(const Synth&) = delete;
    Synth& operator=(const Synth&) = delete;
    Synth(Synth&&) = default;
    Synth& operator=(Synth&&) = default;
    ~Synth();
    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const RailModeChangedEvent& evt, StateMachine& sm);
    Any onEvent(const StopButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SynthKeyEvent::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "Synth"; }
    int getLastKeyIndex() const { return keyboard_.getLastKeyIndex(); }
    bool getLastKeyIsBlack() const { return keyboard_.getLastKeyIsBlack(); }

private:
    lv_obj_t* contentRoot_ = nullptr;
    lv_obj_t* bottomRow_ = nullptr;
    SynthKeyboard keyboard_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
