#pragma once

#include "StateForward.h"
#include "ui/controls/StopPanel.h"
#include "ui/state-machine/Event.h"
#include <array>
#include <lvgl/lvgl.h>
#include <memory>

namespace DirtSim {
namespace Ui {
namespace State {

/**
 * @brief Synth screen state with a single-octave keyboard.
 */
struct Synth {
    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const RailAutoShrinkRequestEvent& evt, StateMachine& sm);
    Any onEvent(const RailModeChangedEvent& evt, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const StopButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "Synth"; }

private:
    static void onKeyboardResized(lv_event_t* e);
    void layoutKeyboard();

    lv_obj_t* contentRoot_ = nullptr;
    lv_obj_t* keyboardContainer_ = nullptr;
    lv_obj_t* whiteKeysContainer_ = nullptr;
    lv_obj_t* bottomRow_ = nullptr;
    std::array<lv_obj_t*, 7> whiteKeys_{};
    std::array<lv_obj_t*, 5> blackKeys_{};
    std::unique_ptr<StopPanel> homePanel_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
