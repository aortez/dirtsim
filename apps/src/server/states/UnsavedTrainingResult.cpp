#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "server/StateMachine.h"
#include <unordered_map>
#include <unordered_set>

namespace DirtSim {
namespace Server {
namespace State {

void UnsavedTrainingResult::onEnter(StateMachine& /*dsm*/)
{
    LOG_INFO(
        State,
        "UnsavedTrainingResult: Ready (candidates={}, scenario={})",
        candidates.size(),
        toString(summary.scenarioId));
}

Any UnsavedTrainingResult::onEvent(const Api::EvolutionStart::Cwc& cwc, StateMachine& dsm)
{
    LOG_INFO(State, "UnsavedTrainingResult: Discarding result to start new evolution");
    Idle idle;
    return idle.onEvent(cwc, dsm);
}

Any UnsavedTrainingResult::onEvent(const Api::TimerStatsGet::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::TimerStatsGet::Response;

    Api::TimerStatsGet::Okay okay;
    okay.timers = timerStats;

    LOG_INFO(State, "UnsavedTrainingResult: TimerStatsGet returning {} timers", okay.timers.size());
    cwc.sendResponse(Response::okay(std::move(okay)));
    return std::move(*this);
}

Any UnsavedTrainingResult::onEvent(const Api::TrainingResultSave::Cwc& cwc, StateMachine& dsm)
{
    const bool restartRequested = cwc.command.restart;
    std::unordered_map<GenomeId, const Candidate*> candidateLookup;
    candidateLookup.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        candidateLookup.emplace(candidate.id, &candidate);
    }

    std::vector<GenomeId> requestedIds = cwc.command.ids;
    if (requestedIds.empty()) {
        requestedIds.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            requestedIds.push_back(candidate.id);
        }
    }

    std::unordered_set<GenomeId> uniqueIds;
    uniqueIds.reserve(requestedIds.size());
    for (const auto& id : requestedIds) {
        if (uniqueIds.insert(id).second && candidateLookup.find(id) == candidateLookup.end()) {
            cwc.sendResponse(
                Api::TrainingResultSave::Response::error(
                    ApiError("TrainingResultSave id not found: " + id.toShortString())));
            return std::move(*this);
        }
    }

    auto& repo = dsm.getGenomeRepository();
    Api::TrainingResultSave::Okay response;
    response.savedIds.reserve(uniqueIds.size());

    for (const auto& id : uniqueIds) {
        const Candidate* candidate = candidateLookup.at(id);
        const auto storeResult =
            repo.storeOrUpdateByHash(candidate->genome, candidate->metadata, candidate->id);
        response.savedIds.push_back(storeResult.id);
    }

    if (evolutionConfig.genomeArchiveMaxSize > 0) {
        const size_t pruned =
            repo.pruneManagedByFitness(static_cast<size_t>(evolutionConfig.genomeArchiveMaxSize));
        if (pruned > 0) {
            LOG_INFO(
                State,
                "UnsavedTrainingResult: Pruned {} managed genomes (max_archive={})",
                pruned,
                evolutionConfig.genomeArchiveMaxSize);
        }
    }

    response.savedCount = static_cast<int>(response.savedIds.size());
    response.discardedCount = static_cast<int>(candidates.size()) - response.savedCount;

    Api::TrainingResult trainingResult;
    trainingResult.summary = summary;
    trainingResult.candidates.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        trainingResult.candidates.push_back(
            Api::TrainingResult::Candidate{
                .id = candidate.id,
                .fitness = candidate.fitness,
                .brainKind = candidate.brainKind,
                .brainVariant = candidate.brainVariant,
                .generation = candidate.generation,
            });
    }
    dsm.storeTrainingResult(trainingResult);

    cwc.sendResponse(Api::TrainingResultSave::Response::okay(std::move(response)));
    if (!restartRequested) {
        return Idle{};
    }

    DIRTSIM_ASSERT(
        !trainingSpec.population.empty(),
        "UnsavedTrainingResult: Restart requested with empty training population");
    DIRTSIM_ASSERT(
        evolutionConfig.populationSize > 0,
        "UnsavedTrainingResult: Restart requested with non-positive population size");

    LOG_INFO(State, "UnsavedTrainingResult: Restarting evolution after save");
    Evolution nextState;
    nextState.evolutionConfig = evolutionConfig;
    nextState.mutationConfig = mutationConfig;
    nextState.trainingSpec = trainingSpec;
    return nextState;
}

Any UnsavedTrainingResult::onEvent(
    const Api::TrainingResultDiscard::Cwc& cwc, StateMachine& /*dsm*/)
{
    Api::TrainingResultDiscard::Okay response;
    response.discarded = true;
    cwc.sendResponse(Api::TrainingResultDiscard::Response::okay(std::move(response)));
    return Idle{};
}

Any UnsavedTrainingResult::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    LOG_INFO(State, "UnsavedTrainingResult: Exit received, shutting down");
    cwc.sendResponse(Api::Exit::Response::okay(std::monostate{}));
    return Shutdown{};
}

} // namespace State
} // namespace Server
} // namespace DirtSim
