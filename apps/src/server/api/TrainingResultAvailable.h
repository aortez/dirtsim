#pragma once

#include "core/ScenarioId.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/evolution/GenomeMetadata.h"

#include <optional>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

/**
 * Training result summary broadcast from server after evolution completes.
 * Not a request/response — pushed to subscribed clients.
 */
struct TrainingResultAvailable {
    struct Summary {
        Scenario::EnumType scenarioId = Scenario::EnumType::TreeGermination;
        OrganismType organismType = OrganismType::TREE;
        int populationSize = 0;
        int maxGenerations = 0;
        int completedGenerations = 0;
        double bestFitness = 0.0;
        double averageFitness = 0.0;
        double totalTrainingSeconds = 0.0;
        std::string primaryBrainKind;
        std::optional<std::string> primaryBrainVariant;
        int primaryPopulationCount = 0;
        GenomeId trainingSessionId{};

        using serialize = zpp::bits::members<12>;
    };

    struct Candidate {
        GenomeId id{};
        double fitness = 0.0;
        std::string brainKind;
        std::optional<std::string> brainVariant;
        int generation = 0;

        using serialize = zpp::bits::members<5>;
    };

    Summary summary;
    std::vector<Candidate> candidates;

    static constexpr const char* name() { return "TrainingResultAvailable"; }
    using serialize = zpp::bits::members<2>;
};

} // namespace Api
} // namespace DirtSim
