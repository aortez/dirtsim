#pragma once

#include "core/WorldData.h"
#include "server/api/TrainingBestSnapshot.h"
#include "server/evolution/FitnessEvaluation.h"
#include "server/evolution/FitnessModelBundle.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace DirtSim::Server::EvolutionSupport {

Api::TrainingBestSnapshot trainingBestSnapshotBuild(
    WorldData worldData,
    std::vector<OrganismId> organismIds,
    const FitnessEvaluation& evaluation,
    const FitnessModelBundle& fitnessModel,
    int generation,
    int commandsAccepted,
    int commandsRejected,
    const std::vector<std::pair<std::string, int>>& topCommandSignatures,
    const std::vector<std::pair<std::string, int>>& topCommandOutcomeSignatures,
    std::optional<ScenarioVideoFrame> scenarioVideoFrame);

} // namespace DirtSim::Server::EvolutionSupport
