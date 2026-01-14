#pragma once

#include "StateForward.h"
#include "ui/controls/LogPanel.h"
#include "ui/state-machine/Event.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace DirtSim {
namespace Ui {

namespace State {

/**
 * Disconnected state - no DSSM server connection.
 * Shows diagnostics screen with log viewer while retrying connection.
 */
struct Disconnected {
    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const ConnectToServerCommand& cmd, StateMachine& sm);
    Any onEvent(const ServerConnectedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);

    void updateAnimations();

    static constexpr const char* name() { return "Disconnected"; }

private:
    static constexpr int MAX_RETRY_ATTEMPTS = 10;
    static constexpr double RETRY_INTERVAL_SECONDS = 1.0;

    void createDiagnosticsScreen(StateMachine& sm);
    void updateStatusLabel();

    StateMachine* sm_ = nullptr;
    std::string pending_host_;
    uint16_t pending_port_ = 0;
    int retry_count_ = 0;
    std::chrono::steady_clock::time_point last_attempt_time_;
    bool retry_pending_ = false;

    // UI components.
    lv_obj_t* mainContainer_ = nullptr;
    lv_obj_t* iconRail_ = nullptr;
    lv_obj_t* logButton_ = nullptr;
    lv_obj_t* contentArea_ = nullptr;
    lv_obj_t* statusLabel_ = nullptr;
    std::unique_ptr<LogPanel> logPanel_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
