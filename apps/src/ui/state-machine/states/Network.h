#pragma once

#include "StateForward.h"
#include "ui/controls/NetworkDiagnosticsPanel.h"
#include "ui/controls/StopPanel.h"
#include "ui/state-machine/Event.h"
#include <lvgl/lvgl.h>
#include <memory>

namespace DirtSim {
namespace Ui {
namespace State {

/**
 * @brief Network diagnostics screen state.
 */
struct Network {
    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const RailAutoShrinkRequestEvent& evt, StateMachine& sm);
    Any onEvent(const RailModeChangedEvent& evt, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const StopButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "Network"; }

private:
    lv_obj_t* contentRoot_ = nullptr;
    std::unique_ptr<NetworkDiagnosticsPanel> networkPanel_;
    std::unique_ptr<StopPanel> homePanel_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
