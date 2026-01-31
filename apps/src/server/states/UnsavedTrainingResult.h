#pragma once

#include "StateForward.h"
#include "core/organisms/brains/Genome.h"
#include "server/api/EvolutionStart.h"
#include "server/api/Exit.h"
#include "server/api/TimerStatsGet.h"
#include "server/api/TrainingResult.h"
#include "server/api/TrainingResultDiscard.h"
#include "server/api/TrainingResultSave.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirtSim {
namespace Server {
namespace State {

struct UnsavedTrainingResult {
    struct Candidate {
        GenomeId id{};
        Genome genome;
        GenomeMetadata metadata;
        std::string brainKind;
        std::optional<std::string> brainVariant;
        double fitness = 0.0;
        int generation = 0;
    };

    Api::TrainingResult::Summary summary;
    std::vector<Candidate> candidates;
    std::unordered_map<std::string, Api::TimerStatsGet::TimerEntry> timerStats;
    EvolutionConfig evolutionConfig;
    MutationConfig mutationConfig;
    TrainingSpec trainingSpec;

    void onEnter(StateMachine& dsm);

    Any onEvent(const Api::EvolutionStart::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::TimerStatsGet::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::TrainingResultSave::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::TrainingResultDiscard::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::Exit::Cwc& cwc, StateMachine& dsm);

    static constexpr const char* name() { return "UnsavedTrainingResult"; }
};

} // namespace State
} // namespace Server
} // namespace DirtSim
