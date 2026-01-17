#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/ScenarioId.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/evolution/GenomeMetadata.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace TrainingResultGet {

DEFINE_API_NAME(TrainingResultGet);

struct Okay;

struct Command {
    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<0>;
};

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

void to_json(nlohmann::json& j, const Summary& summary);
void from_json(const nlohmann::json& j, Summary& summary);
void to_json(nlohmann::json& j, const Candidate& candidate);
void from_json(const nlohmann::json& j, Candidate& candidate);

struct Okay {
    Summary summary;
    std::vector<Candidate> candidates;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace TrainingResultGet
} // namespace Api
} // namespace DirtSim
