#pragma once

#include "StateForward.h"
#include "server/Event.h"
#include "server/search/SmbSearchExecution.h"
#include <chrono>
#include <optional>

namespace DirtSim {
namespace Server {
namespace State {

struct SearchActive {
    void onEnter(StateMachine& dsm);
    void onExit(StateMachine& dsm);

    std::optional<Any> tick(StateMachine& dsm);

    Any onEvent(const Api::Exit::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::SearchPauseSet::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::SearchProgressGet::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::SearchStop::Cwc& cwc, StateMachine& dsm);

    static constexpr const char* name() { return "SearchActive"; }

    SearchSupport::SmbSearchExecution execution;
    std::chrono::steady_clock::time_point lastProgressBroadcastTime_{};
    bool renderBroadcasted_ = false;
};

} // namespace State
} // namespace Server
} // namespace DirtSim
