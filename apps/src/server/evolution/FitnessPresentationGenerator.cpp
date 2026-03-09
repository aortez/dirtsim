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

std::string treeSummaryBuild(
    double coreFitness,
    double bonusFitness,
    double survivalScore,
    double energyScore,
    double resourceScore,
    double structureScore,
    double behaviorScore)
{
    std::ostringstream summary;
    summary << std::fixed << std::setprecision(4);
    summary << "Core " << coreFitness << " = Survival " << survivalScore << " x (1 + Energy "
            << energyScore << ") x (1 + Resources " << resourceScore << ")";
    summary << "\nBonus " << bonusFitness << " = Structure " << structureScore
            << " + Commands/Seed " << behaviorScore;
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
        .summary = treeSummaryBuild(
            breakdown->coreFitness,
            breakdown->bonusFitness,
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
        "Survival Factor",
        breakdown->survivalScore,
        { makeMetric(
            "survival_time",
            "Lifespan",
            breakdown->survivalRaw,
            optionalPositive(breakdown->survivalReference),
            breakdown->survivalScore,
            "seconds") }));
    presentation.sections.push_back(makeSection(
        "energy",
        "Energy Factor",
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
                "energy_max_weighted",
                "Max Energy Weighted",
                breakdown->energyMaxWeightedComponent,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "energy_final",
                "Final Energy",
                breakdown->finalEnergyRaw,
                optionalPositive(breakdown->energyReference),
                breakdown->finalEnergyNormalized,
                "energy"),
            makeMetric(
                "energy_final_weighted",
                "Final Energy Weighted",
                breakdown->energyFinalWeightedComponent,
                std::nullopt,
                std::nullopt,
                "score"),
        }));
    presentation.sections.push_back(makeSection(
        "resources",
        "Resource Factor",
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
                "energy_produced_weighted",
                "Energy Produced Weighted",
                breakdown->resourceEnergyWeightedComponent,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "water_absorbed",
                "Water Absorbed",
                breakdown->absorbedWaterRaw,
                optionalPositive(breakdown->waterReference),
                breakdown->absorbedWaterNormalized,
                "water"),
            makeMetric(
                "water_absorbed_weighted",
                "Water Absorbed Weighted",
                breakdown->resourceWaterWeightedComponent,
                std::nullopt,
                std::nullopt,
                "score"),
        }));
    presentation.sections.push_back(makeSection(
        "structure",
        "Structure Bonuses",
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
                "minimal_structure_bonus",
                "Minimal Structure Bonus",
                breakdown->structureBonus,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "root_below_seed_bonus",
                "Root Below Seed Bonus",
                breakdown->rootBelowSeedBonus,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "wood_above_seed_bonus",
                "Wood Above Seed Bonus",
                breakdown->woodAboveSeedBonus,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "leaf_count",
                "Leaf Count",
                static_cast<double>(breakdown->leafCount),
                std::nullopt,
                std::nullopt,
                "cells"),
            makeMetric(
                "root_count",
                "Root Count",
                static_cast<double>(breakdown->rootCount),
                std::nullopt,
                std::nullopt,
                "cells"),
            makeMetric(
                "wood_count",
                "Wood Count",
                static_cast<double>(breakdown->woodCount),
                std::nullopt,
                std::nullopt,
                "cells"),
            makeMetric(
                "partial_structure_parts",
                "Partial Structure Parts",
                static_cast<double>(breakdown->partialStructurePartCount),
                std::nullopt,
                std::nullopt,
                "parts"),
        }));
    presentation.sections.push_back(makeSection(
        "commands_seed",
        "Commands & Seed",
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
                "commands_accepted",
                "Commands Accepted",
                static_cast<double>(breakdown->commandsAccepted),
                std::nullopt,
                std::nullopt,
                "commands"),
            makeMetric(
                "commands_rejected",
                "Commands Rejected",
                static_cast<double>(breakdown->commandsRejected),
                std::nullopt,
                std::nullopt,
                "commands"),
            makeMetric(
                "idle_cancels",
                "Idle Cancels",
                static_cast<double>(breakdown->idleCancels),
                std::nullopt,
                std::nullopt,
                "commands"),
            makeMetric(
                "seed_score",
                "Seed Score",
                breakdown->seedScore,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "seed_count_bonus",
                "Seed Count Bonus",
                breakdown->seedCountBonus,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "seed_distance_bonus",
                "Seed Distance Bonus",
                breakdown->seedDistanceBonus,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "seeds_produced",
                "Seeds Produced",
                static_cast<double>(breakdown->seedsProduced),
                std::nullopt,
                std::nullopt,
                "seeds"),
            makeMetric(
                "landed_seed_count",
                "Landed Seeds",
                static_cast<double>(breakdown->landedSeedCount),
                std::nullopt,
                std::nullopt,
                "seeds"),
            makeMetric(
                "average_seed_distance",
                "Average Seed Distance",
                breakdown->averageLandedSeedDistance,
                std::nullopt,
                std::nullopt,
                "cells"),
            makeMetric(
                "max_seed_distance",
                "Max Seed Distance",
                breakdown->maxLandedSeedDistance,
                optionalPositive(breakdown->seedDistanceReference),
                std::nullopt,
                "cells"),
        }));
    return presentation;
}

} // namespace DirtSim::Server::EvolutionSupport
