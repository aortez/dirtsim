#pragma once

#include "core/WorldData.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

/**
 * Best snapshot broadcast when a new all-time fitness record is reached, and
 * occasionally for tied-best variants to help visualize plateau diversity.
 * Includes a renderable WorldData snapshot and organism grid.
 */
struct TrainingBestSnapshot {
    struct CommandSignatureCount {
        std::string signature;
        int count = 0;

        using serialize = zpp::bits::members<2>;
    };

    WorldData worldData;
    std::vector<OrganismId> organismIds;
    double fitness = 0.0;
    int generation = 0;
    int commandsAccepted = 0;
    int commandsRejected = 0;
    std::vector<CommandSignatureCount> topCommandSignatures;
    std::vector<CommandSignatureCount> topCommandOutcomeSignatures;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame;

    static constexpr const char* name() { return "TrainingBestSnapshot"; }

    using serialize = zpp::bits::members<9>;
};

void to_json(nlohmann::json& j, const TrainingBestSnapshot::CommandSignatureCount& value);
void from_json(const nlohmann::json& j, TrainingBestSnapshot::CommandSignatureCount& value);

void to_json(nlohmann::json& j, const TrainingBestSnapshot& value);
void from_json(const nlohmann::json& j, TrainingBestSnapshot& value);

} // namespace Api
} // namespace DirtSim
