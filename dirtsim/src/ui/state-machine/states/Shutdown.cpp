#include "State.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "ui/state-machine/StateMachine.h"

namespace DirtSim {
namespace Ui {
namespace State {

void Shutdown::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Performing cleanup");

    // Disconnect from DSSM server if connected.
    // Note: We just disconnect without sending exit command. The server is headless
    // and should keep running independently. If server shutdown is needed, it should
    // be done via separate mechanism (e.g., CLI tool).
    if (sm.wsService_) {
        if (sm.wsService_->isConnected()) {
            LOG_INFO(State, "Disconnecting from DSSM server");
            sm.wsService_->disconnect();
        }
        if (sm.wsService_->isListening()) {
            LOG_INFO(State, "Stopping WebSocket server");
            sm.wsService_->stopListening();
        }
    }

    // Clean up LVGL resources.
    // Note: LVGL resources are managed by unique_ptrs in StateMachine.
    // They will be automatically cleaned up when StateMachine destructor runs.
    if (sm.uiManager_) {
        LOG_INFO(State, "UI components will be cleaned up by StateMachine destructor");
    }

    // Set exit flag to signal main loop to exit.
    LOG_INFO(State, "Setting shouldExit flag to true");
    sm.setShouldExit(true);

    LOG_INFO(State, "Cleanup complete, shouldExit={}", sm.shouldExit());
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
