#pragma once

#include "core/RenderMessage.h"
#include "core/ScenarioConfig.h"
#include "core/WorldData.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace DirtSim {

struct UiUpdateEvent {
    uint64_t sequenceNum = 0;
    WorldData worldData; // Just the data, no physics calculators needed for rendering.
    uint32_t fps = 0;
    uint64_t stepCount = 0;
    bool isPaused = false;
    std::chrono::steady_clock::time_point timestamp;

    // Scenario metadata (sent alongside world data).
    Scenario::EnumType scenario_id = Scenario::EnumType::Empty;
    ScenarioConfig scenario_config = Config::Empty{};

    // Standalone video frame for NES scenarios (not part of WorldData).
    std::optional<ScenarioVideoFrame> scenarioVideoFrame;

    static constexpr const char* name() { return "UiUpdateEvent"; }
};

} // namespace DirtSim
