#pragma once

#include "core/WorldData.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

/**
 * Best-playback frame broadcast from server during evolution when enabled.
 * Carries a renderable world snapshot for the best genome replay panel.
 */
struct TrainingBestPlaybackFrame {
    WorldData worldData;
    std::vector<OrganismId> organismIds;
    double fitness = 0.0;
    int generation = 0;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame;

    static constexpr const char* name() { return "TrainingBestPlaybackFrame"; }

    using serialize = zpp::bits::members<5>;
};

void to_json(nlohmann::json& j, const TrainingBestPlaybackFrame& value);
void from_json(const nlohmann::json& j, TrainingBestPlaybackFrame& value);

} // namespace Api
} // namespace DirtSim
