#pragma once

#include "StateForward.h"
#include "ui/state-machine/Event.h"
#include <memory>

namespace DirtSim {
namespace Ui {
namespace State {

struct NetworkUiContext;

struct NetworkWifiConnecting {
    NetworkWifiConnecting() = default;
    explicit NetworkWifiConnecting(std::shared_ptr<NetworkUiContext> context);

    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const StopButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::NetworkConnectCancelPress::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::NetworkDiagnosticsGet::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "NetworkWifiConnecting"; }
    bool blocksAutoShrink() const { return true; }

private:
    std::shared_ptr<NetworkUiContext> context_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
