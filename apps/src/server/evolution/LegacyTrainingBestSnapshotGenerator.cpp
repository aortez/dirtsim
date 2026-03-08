#include "LegacyTrainingBestSnapshotGenerator.h"

#include "LegacyFitnessReportGenerator.h"

namespace DirtSim::Server::EvolutionSupport {

Api::TrainingBestSnapshot trainingBestSnapshotLegacyBuild(
    WorldData worldData,
    std::vector<OrganismId> organismIds,
    const FitnessEvaluation& evaluation,
    int generation,
    int commandsAccepted,
    int commandsRejected,
    const std::vector<std::pair<std::string, int>>& topCommandSignatures,
    const std::vector<std::pair<std::string, int>>& topCommandOutcomeSignatures,
    std::optional<ScenarioVideoFrame> scenarioVideoFrame)
{
    Api::TrainingBestSnapshot bestSnapshot;
    bestSnapshot.worldData = std::move(worldData);
    bestSnapshot.organismIds = std::move(organismIds);
    bestSnapshot.fitness = evaluation.totalFitness;
    bestSnapshot.generation = generation;
    bestSnapshot.commandsAccepted = commandsAccepted;
    bestSnapshot.commandsRejected = commandsRejected;
    bestSnapshot.topCommandSignatures.reserve(topCommandSignatures.size());
    for (const auto& [signature, count] : topCommandSignatures) {
        bestSnapshot.topCommandSignatures.push_back(
            Api::TrainingBestSnapshot::CommandSignatureCount{
                .signature = signature,
                .count = count,
            });
    }
    bestSnapshot.topCommandOutcomeSignatures.reserve(topCommandOutcomeSignatures.size());
    for (const auto& [signature, count] : topCommandOutcomeSignatures) {
        bestSnapshot.topCommandOutcomeSignatures.push_back(
            Api::TrainingBestSnapshot::CommandSignatureCount{
                .signature = signature,
                .count = count,
            });
    }
    bestSnapshot.scenarioVideoFrame = std::move(scenarioVideoFrame);
    bestSnapshot.fitnessBreakdown = fitnessEvaluationLegacyReportGenerate(evaluation);
    return bestSnapshot;
}

} // namespace DirtSim::Server::EvolutionSupport
