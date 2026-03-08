#pragma once

#include "core/organisms/evolution/DuckEvaluator.h"
#include "core/organisms/evolution/TreeEvaluator.h"

#include <optional>
#include <variant>

namespace DirtSim::Server::EvolutionSupport {

using FitnessDetails = std::variant<std::monostate, DuckFitnessBreakdown, TreeFitnessBreakdown>;

struct FitnessEvaluation {
    double totalFitness = 0.0;
    FitnessDetails details{};
};

inline const DuckFitnessBreakdown* fitnessEvaluationDuckBreakdownGet(
    const FitnessEvaluation& evaluation)
{
    return std::get_if<DuckFitnessBreakdown>(&evaluation.details);
}

inline const TreeFitnessBreakdown* fitnessEvaluationTreeBreakdownGet(
    const FitnessEvaluation& evaluation)
{
    return std::get_if<TreeFitnessBreakdown>(&evaluation.details);
}

inline std::optional<double> fitnessEvaluationWingUpSecondsGet(const FitnessEvaluation& evaluation)
{
    const DuckFitnessBreakdown* breakdown = fitnessEvaluationDuckBreakdownGet(evaluation);
    if (!breakdown) {
        return std::nullopt;
    }
    return breakdown->wingUpSeconds;
}

} // namespace DirtSim::Server::EvolutionSupport
