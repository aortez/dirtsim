#pragma once

#include "core/ScenarioId.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/evolution/FitnessCalculator.h"
#include "server/api/FitnessPresentation.h"
#include "server/evolution/FitnessEvaluation.h"

#include <optional>
#include <span>
#include <string>

namespace DirtSim::Server::EvolutionSupport {

struct FitnessModelBundle {
    using EvaluateFn = FitnessEvaluation (*)(const FitnessContext& context);
    using FormatLogSummaryFn = std::string (*)(const FitnessEvaluation& evaluation);
    using GeneratePresentationFn =
        Api::FitnessPresentation (*)(const FitnessEvaluation& evaluation);
    using MergePassesFn = FitnessEvaluation (*)(std::span<const FitnessEvaluation> evaluations);

    EvaluateFn evaluate = nullptr;
    FormatLogSummaryFn formatLogSummary = nullptr;
    GeneratePresentationFn generatePresentation = nullptr;
    MergePassesFn mergePasses = nullptr;
};

FitnessModelBundle fitnessModelResolve(OrganismType organismType, Scenario::EnumType scenarioId);

} // namespace DirtSim::Server::EvolutionSupport
