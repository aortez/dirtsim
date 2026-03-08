#include "FitnessPresentationGenerator.h"

#include <iomanip>
#include <sstream>
#include <utility>

namespace DirtSim::Server::EvolutionSupport {

namespace {

Api::FitnessPresentationMetric makeMetric(
    std::string key,
    std::string label,
    double value,
    std::optional<double> reference,
    std::optional<double> normalized,
    std::string unit)
{
    return Api::FitnessPresentationMetric{
        .key = std::move(key),
        .label = std::move(label),
        .value = value,
        .reference = reference,
        .normalized = normalized,
        .unit = std::move(unit),
    };
}

Api::FitnessPresentationSection makeSection(
    std::string key,
    std::string label,
    std::optional<double> score,
    std::vector<Api::FitnessPresentationMetric> metrics)
{
    return Api::FitnessPresentationSection{
        .key = std::move(key),
        .label = std::move(label),
        .score = score,
        .metrics = std::move(metrics),
    };
}

std::optional<double> optionalPositive(double value)
{
    if (value <= 0.0) {
        return std::nullopt;
    }
    return value;
}

std::string scoreSummaryBuild(
    double survivalScore,
    double energyScore,
    double resourceScore,
    double structureScore,
    double behaviorScore)
{
    std::ostringstream summary;
    summary << std::fixed << std::setprecision(2);
    summary << "Survival " << survivalScore;
    summary << " | Energy " << energyScore;
    summary << " | Resources " << resourceScore;
    summary << " | Structure " << structureScore;
    summary << " | Behavior " << behaviorScore;
    return summary.str();
}

Api::FitnessPresentation genericPresentationBuild(
    OrganismType organismType,
    std::string modelId,
    const FitnessEvaluation& evaluation,
    std::string summary)
{
    Api::FitnessPresentation presentation{
        .organismType = organismType,
        .modelId = std::move(modelId),
        .totalFitness = evaluation.totalFitness,
        .summary = std::move(summary),
        .sections = {},
    };
    presentation.sections.push_back(makeSection(
        "overview",
        "Overview",
        std::nullopt,
        { makeMetric(
            "total_fitness",
            "Total Fitness",
            evaluation.totalFitness,
            std::nullopt,
            std::nullopt,
            "score") }));
    return presentation;
}

} // namespace

Api::FitnessPresentation fitnessEvaluationDuckPresentationGenerate(
    const FitnessEvaluation& evaluation)
{
    if (const auto* duck = fitnessEvaluationDuckBreakdownGet(evaluation)) {
        std::ostringstream summary;
        summary << std::fixed << std::setprecision(2);
        summary << "Survival " << duck->survivalScore;
        summary << " | Movement " << duck->movementScore;
        summary << " | Coverage " << duck->coverageScore;
        return genericPresentationBuild(OrganismType::DUCK, "duck", evaluation, summary.str());
    }
    return genericPresentationBuild(
        OrganismType::DUCK, "duck", evaluation, "Detailed duck presentation is not available yet.");
}

Api::FitnessPresentation fitnessEvaluationGoosePresentationGenerate(
    const FitnessEvaluation& evaluation)
{
    return genericPresentationBuild(
        OrganismType::GOOSE,
        "goose",
        evaluation,
        "Detailed goose presentation is not available yet.");
}

Api::FitnessPresentation fitnessEvaluationNesDuckPresentationGenerate(
    const FitnessEvaluation& evaluation)
{
    return genericPresentationBuild(
        OrganismType::NES_DUCK,
        "nes_duck",
        evaluation,
        "Detailed NES presentation is not available yet.");
}

Api::FitnessPresentation fitnessEvaluationTreePresentationGenerate(
    const FitnessEvaluation& evaluation)
{
    const TreeFitnessBreakdown* breakdown = fitnessEvaluationTreeBreakdownGet(evaluation);
    if (!breakdown) {
        return genericPresentationBuild(
            OrganismType::TREE, "tree", evaluation, "Detailed tree presentation is not available.");
    }

    const double structureScore = breakdown->partialStructureBonus + breakdown->stageBonus
        + breakdown->structureBonus + breakdown->milestoneBonus;
    const double behaviorScore = breakdown->commandScore + breakdown->seedScore;

    Api::FitnessPresentation presentation{
        .organismType = OrganismType::TREE,
        .modelId = "tree",
        .totalFitness = evaluation.totalFitness,
        .summary = scoreSummaryBuild(
            breakdown->survivalScore,
            breakdown->energyScore,
            breakdown->resourceScore,
            structureScore,
            behaviorScore),
        .sections = {},
    };

    presentation.sections.reserve(5);
    presentation.sections.push_back(makeSection(
        "survival",
        "Survival",
        breakdown->survivalScore,
        { makeMetric(
            "survival_time",
            "Survival Time",
            breakdown->survivalRaw,
            optionalPositive(breakdown->survivalReference),
            breakdown->survivalScore,
            "seconds") }));
    presentation.sections.push_back(makeSection(
        "energy",
        "Energy",
        breakdown->energyScore,
        {
            makeMetric(
                "energy_max",
                "Max Energy",
                breakdown->maxEnergyRaw,
                optionalPositive(breakdown->energyReference),
                breakdown->maxEnergyNormalized,
                "energy"),
            makeMetric(
                "energy_final",
                "Final Energy",
                breakdown->finalEnergyRaw,
                optionalPositive(breakdown->energyReference),
                breakdown->finalEnergyNormalized,
                "energy"),
        }));
    presentation.sections.push_back(makeSection(
        "resources",
        "Resources",
        breakdown->resourceScore,
        {
            makeMetric(
                "energy_produced",
                "Energy Produced",
                breakdown->producedEnergyRaw,
                optionalPositive(breakdown->energyReference),
                breakdown->producedEnergyNormalized,
                "energy"),
            makeMetric(
                "water_absorbed",
                "Water Absorbed",
                breakdown->absorbedWaterRaw,
                optionalPositive(breakdown->waterReference),
                breakdown->absorbedWaterNormalized,
                "water"),
        }));
    presentation.sections.push_back(makeSection(
        "structure",
        "Structure",
        structureScore,
        {
            makeMetric(
                "partial_structure_bonus",
                "Partial Structure",
                breakdown->partialStructureBonus,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "stage_bonus",
                "Stage Bonus",
                breakdown->stageBonus,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "structure_bonus",
                "Structure Bonus",
                breakdown->structureBonus,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "milestone_bonus",
                "Milestone Bonus",
                breakdown->milestoneBonus,
                std::nullopt,
                std::nullopt,
                "score"),
        }));
    presentation.sections.push_back(makeSection(
        "behavior",
        "Behavior",
        behaviorScore,
        {
            makeMetric(
                "command_score",
                "Command Score",
                breakdown->commandScore,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "seed_score",
                "Seed Score",
                breakdown->seedScore,
                std::nullopt,
                std::nullopt,
                "score"),
        }));
    return presentation;
}

} // namespace DirtSim::Server::EvolutionSupport
