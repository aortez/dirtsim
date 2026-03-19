#pragma once

#include "StateForward.h"
#include "ui/state-machine/Event.h"
#include <memory>

namespace DirtSim {
namespace Ui {
namespace State {

struct NetworkUiContext;

struct NetworkScanner {
    NetworkScanner() = default;
    explicit NetworkScanner(std::shared_ptr<NetworkUiContext> context);

    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const RailModeChangedEvent& evt, StateMachine& sm);
    Any onEvent(const StopButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::NetworkDiagnosticsGet::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::NetworkScannerEnterPress::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::NetworkScannerExitPress::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "NetworkScanner"; }
    bool blocksAutoShrink() const { return true; }

private:
    std::shared_ptr<NetworkUiContext> context_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
