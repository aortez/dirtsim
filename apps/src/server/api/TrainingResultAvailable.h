#pragma once

#include "ApiError.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/ScenarioId.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/evolution/GenomeMetadata.h"

#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

/**
 * Training result summary sent from server after evolution completes.
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

    using OkayType = std::monostate;
    using Response = Result<OkayType, ApiError>;
    using Cwc = CommandWithCallback<TrainingResultAvailable, Response>;
};

void to_json(nlohmann::json& j, const TrainingResultAvailable::Summary& summary);
void from_json(const nlohmann::json& j, TrainingResultAvailable::Summary& summary);
void to_json(nlohmann::json& j, const TrainingResultAvailable::Candidate& candidate);
void from_json(const nlohmann::json& j, TrainingResultAvailable::Candidate& candidate);

} // namespace Api
} // namespace DirtSim
