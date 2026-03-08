#pragma once

#include "server/api/FitnessBreakdownReport.h"
#include "server/evolution/FitnessEvaluation.h"

#include <optional>

namespace DirtSim::Server::EvolutionSupport {

/**
 * Legacy adapter that preserves the current fitness report DTO for the existing UI.
 */
std::optional<Api::FitnessBreakdownReport> fitnessEvaluationLegacyReportGenerate(
    const FitnessEvaluation& evaluation);

} // namespace DirtSim::Server::EvolutionSupport
