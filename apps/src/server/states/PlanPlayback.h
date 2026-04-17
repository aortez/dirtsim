#pragma once

#include "StateForward.h"
#include "core/UUID.h"
#include "server/Event.h"
#include "server/search/SmbPlanExecution.h"
#include <chrono>
#include <cstdint>
#include <optional>

namespace DirtSim {
namespace Server {
namespace State {

struct PlanPlayback {
    void onEnter(StateMachine& dsm);
    void onExit(StateMachine& dsm);

    std::optional<Any> tick(StateMachine& dsm);

    Any onEvent(const Api::Exit::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::PlanPlaybackPauseSet::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::PlanPlaybackStop::Cwc& cwc, StateMachine& dsm);

    static constexpr const char* name() { return "PlanPlayback"; }

    UUID planId{};
    SearchSupport::SmbPlanExecution execution;
    uint32_t planPlaybackFrameDelayMs = 0;
    std::chrono::steady_clock::time_point lastPlaybackTickTime_{};
    bool renderBroadcasted_ = false;
};

} // namespace State
} // namespace Server
} // namespace DirtSim
