#pragma once

#include "core/WorldData.h"
#include "server/api/TrainingBestSnapshot.h"
#include "server/evolution/FitnessEvaluation.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace DirtSim::Server::EvolutionSupport {

/**
 * Legacy adapter that preserves the current training best snapshot DTO for the existing UI.
 */
Api::TrainingBestSnapshot trainingBestSnapshotLegacyBuild(
    WorldData worldData,
    std::vector<OrganismId> organismIds,
    const FitnessEvaluation& evaluation,
    int generation,
    int commandsAccepted,
    int commandsRejected,
    const std::vector<std::pair<std::string, int>>& topCommandSignatures,
    const std::vector<std::pair<std::string, int>>& topCommandOutcomeSignatures,
    std::optional<ScenarioVideoFrame> scenarioVideoFrame);

} // namespace DirtSim::Server::EvolutionSupport
