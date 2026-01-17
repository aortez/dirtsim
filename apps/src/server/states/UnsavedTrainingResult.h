#pragma once

#include "StateForward.h"
#include "core/organisms/brains/Genome.h"
#include "server/api/EvolutionStart.h"
#include "server/api/Exit.h"
#include "server/api/TrainingResultAvailable.h"
#include "server/api/TrainingResultDiscard.h"
#include "server/api/TrainingResultSave.h"

#include <optional>
#include <string>
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

    Api::TrainingResultAvailable::Summary summary;
    std::vector<Candidate> candidates;

    void onEnter(StateMachine& dsm);

    Any onEvent(const Api::EvolutionStart::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::TrainingResultSave::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::TrainingResultDiscard::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::Exit::Cwc& cwc, StateMachine& dsm);

    static constexpr const char* name() { return "UnsavedTrainingResult"; }
};

} // namespace State
} // namespace Server
} // namespace DirtSim
