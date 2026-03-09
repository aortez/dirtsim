#pragma once

#include "server/api/FitnessPresentation.h"
#include "server/evolution/FitnessEvaluation.h"

namespace DirtSim::Server::EvolutionSupport {

Api::FitnessPresentation fitnessEvaluationDuckPresentationGenerate(
    const FitnessEvaluation& evaluation);
Api::FitnessPresentation fitnessEvaluationGoosePresentationGenerate(
    const FitnessEvaluation& evaluation);
Api::FitnessPresentation fitnessEvaluationNesGenericPresentationGenerate(
    const FitnessEvaluation& evaluation);
Api::FitnessPresentation fitnessEvaluationNesSuperMarioBrosPresentationGenerate(
    const FitnessEvaluation& evaluation);
Api::FitnessPresentation fitnessEvaluationTreePresentationGenerate(
    const FitnessEvaluation& evaluation);

} // namespace DirtSim::Server::EvolutionSupport
