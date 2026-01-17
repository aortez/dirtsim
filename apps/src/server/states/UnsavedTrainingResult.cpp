#include "State.h"
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

Any UnsavedTrainingResult::onEvent(const Api::TrainingResultSave::Cwc& cwc, StateMachine& dsm)
{
    if (cwc.command.ids.empty()) {
        cwc.sendResponse(Api::TrainingResultSave::Response::error(
            ApiError("TrainingResultSave requires at least one id")));
        return std::move(*this);
    }

    std::unordered_map<GenomeId, const Candidate*> candidateLookup;
    candidateLookup.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        candidateLookup.emplace(candidate.id, &candidate);
    }

    std::unordered_set<GenomeId> uniqueIds;
    uniqueIds.reserve(cwc.command.ids.size());
    for (const auto& id : cwc.command.ids) {
        if (uniqueIds.insert(id).second && candidateLookup.find(id) == candidateLookup.end()) {
            cwc.sendResponse(Api::TrainingResultSave::Response::error(
                ApiError("TrainingResultSave id not found: " + id.toShortString())));
            return std::move(*this);
        }
    }

    auto& repo = dsm.getGenomeRepository();
    for (const auto& id : uniqueIds) {
        if (repo.exists(id)) {
            cwc.sendResponse(Api::TrainingResultSave::Response::error(
                ApiError("TrainingResultSave id already exists: " + id.toShortString())));
            return std::move(*this);
        }
    }

    Api::TrainingResultSave::Okay response;
    response.savedIds.reserve(uniqueIds.size());

    for (const auto& id : uniqueIds) {
        const Candidate* candidate = candidateLookup.at(id);
        repo.store(candidate->id, candidate->genome, candidate->metadata);
        response.savedIds.push_back(candidate->id);
    }

    response.savedCount = static_cast<int>(response.savedIds.size());
    response.discardedCount = static_cast<int>(candidates.size()) - response.savedCount;

    cwc.sendResponse(Api::TrainingResultSave::Response::okay(std::move(response)));
    return Idle{};
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
