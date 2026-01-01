#pragma once

#include "StateForward.h"
#include "ui/state-machine/Event.h"
#include <chrono>
#include <cstdint>
#include <string>

namespace DirtSim {
namespace Ui {
namespace State {

/**
 * @brief Disconnected state - no DSSM server connection.
 *
 * Automatically retries connection every second for up to 10 attempts
 * when a connection fails.
 */
struct Disconnected {
    void onEnter(StateMachine& sm);

    Any onEvent(const ConnectToServerCommand& cmd, StateMachine& sm);
    Any onEvent(const ServerConnectedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);

    // Called each frame to check if retry is needed.
    void updateAnimations();

    static constexpr const char* name() { return "Disconnected"; }

private:
    static constexpr int MAX_RETRY_ATTEMPTS = 10;
    static constexpr double RETRY_INTERVAL_SECONDS = 1.0;

    StateMachine* sm_ = nullptr;
    std::string pending_host_;
    uint16_t pending_port_ = 0;
    int retry_count_ = 0;
    std::chrono::steady_clock::time_point last_attempt_time_;
    bool retry_pending_ = false;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
