#pragma once

#include "StateForward.h"
#include "ui/state-machine/Event.h"

namespace DirtSim {
namespace Ui {
namespace State {

/**
 * @brief Training state - displays evolution progress and controls.
 *
 * Shows progress bars for generation and evaluation, fitness statistics,
 * and provides controls to pause, resume, stop, or view the best genome.
 */
struct Training {
    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);

    static constexpr const char* name() { return "Training"; }
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
