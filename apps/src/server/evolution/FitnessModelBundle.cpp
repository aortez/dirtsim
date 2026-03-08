#include "FitnessModelBundle.h"

#include "FitnessPresentationGenerator.h"
#include "core/Assert.h"
#include "core/organisms/evolution/NesEvaluator.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>

namespace DirtSim::Server::EvolutionSupport {

namespace {

FitnessEvaluation fitnessEvaluationDuckEvaluate(const FitnessContext& context)
{
    const DuckFitnessBreakdown breakdown = DuckEvaluator::evaluateWithBreakdown(context);
    return FitnessEvaluation{
        .totalFitness = breakdown.totalFitness,
        .details = breakdown,
    };
}

FitnessEvaluation fitnessEvaluationGenericEvaluate(const FitnessContext& context)
{
    const double totalFitness = computeFitnessForOrganism(context);
    return FitnessEvaluation{
        .totalFitness = totalFitness,
        .details = std::monostate{},
    };
}

FitnessEvaluation fitnessEvaluationIdentityMerge(std::span<const FitnessEvaluation> evaluations)
{
    if (evaluations.empty()) {
        return {};
    }
    return evaluations.front();
}

std::string fitnessEvaluationNoopLogSummary(const FitnessEvaluation& /*evaluation*/)
{
    return "";
}

FitnessEvaluation fitnessEvaluationNesDuckEvaluate(const FitnessContext& context)
{
    const double totalFitness =
        NesEvaluator::evaluateFromRewardTotal(context.result.nesRewardTotal);
    return FitnessEvaluation{
        .totalFitness = totalFitness,
        .details = std::monostate{},
    };
}

FitnessEvaluation fitnessEvaluationTreeEvaluate(const FitnessContext& context)
{
    const TreeFitnessBreakdown breakdown = TreeEvaluator::evaluateWithBreakdown(context);
    return FitnessEvaluation{
        .totalFitness = breakdown.totalFitness,
        .details = breakdown,
    };
}

std::string fitnessEvaluationTreeLogSummary(const FitnessEvaluation& evaluation)
{
    const TreeFitnessBreakdown* breakdown = fitnessEvaluationTreeBreakdownGet(evaluation);
    if (!breakdown) {
        return "";
    }

    std::ostringstream summary;
    summary << std::fixed << std::setprecision(3);
    summary << "surv=" << breakdown->survivalScore;
    summary << " energy=" << breakdown->energyScore;
    summary << " res=" << breakdown->resourceScore;
    summary << " partial=" << breakdown->partialStructureBonus;
    summary << " stage=" << breakdown->stageBonus;
    summary << " struct=" << breakdown->structureBonus;
    summary << " milestone=" << breakdown->milestoneBonus;
    summary << " cmd=" << breakdown->commandScore;
    return summary.str();
}

double averageDouble(double left, double right)
{
    return 0.5 * (left + right);
}

DuckFitnessBreakdown duckFitnessBreakdownAverage(
    const DuckFitnessBreakdown& first, const DuckFitnessBreakdown& second, double totalFitness)
{
    DuckFitnessBreakdown merged;
    merged.survivalRaw = averageDouble(first.survivalRaw, second.survivalRaw);
    merged.survivalReference = averageDouble(first.survivalReference, second.survivalReference);
    merged.survivalScore = averageDouble(first.survivalScore, second.survivalScore);
    merged.energyAverage = averageDouble(first.energyAverage, second.energyAverage);
    merged.energyConsumedTotal =
        averageDouble(first.energyConsumedTotal, second.energyConsumedTotal);
    merged.energyLimitedSeconds =
        averageDouble(first.energyLimitedSeconds, second.energyLimitedSeconds);
    merged.wingUpSeconds = averageDouble(first.wingUpSeconds, second.wingUpSeconds);
    merged.wingDownSeconds = averageDouble(first.wingDownSeconds, second.wingDownSeconds);
    merged.movementRaw = averageDouble(first.movementRaw, second.movementRaw);
    merged.movementScore = averageDouble(first.movementScore, second.movementScore);
    merged.displacementScore = averageDouble(first.displacementScore, second.displacementScore);
    merged.efficiencyScore = averageDouble(first.efficiencyScore, second.efficiencyScore);
    merged.effortRaw = averageDouble(first.effortRaw, second.effortRaw);
    merged.effortReference = averageDouble(first.effortReference, second.effortReference);
    merged.effortScore = averageDouble(first.effortScore, second.effortScore);
    merged.effortPenaltyRaw = averageDouble(first.effortPenaltyRaw, second.effortPenaltyRaw);
    merged.effortPenaltyScore = averageDouble(first.effortPenaltyScore, second.effortPenaltyScore);
    merged.coverageColumnRaw = averageDouble(first.coverageColumnRaw, second.coverageColumnRaw);
    merged.coverageColumnReference =
        averageDouble(first.coverageColumnReference, second.coverageColumnReference);
    merged.coverageScore = averageDouble(first.coverageScore, second.coverageScore);
    merged.coverageColumnScore =
        averageDouble(first.coverageColumnScore, second.coverageColumnScore);
    merged.coverageRowRaw = averageDouble(first.coverageRowRaw, second.coverageRowRaw);
    merged.coverageRowReference =
        averageDouble(first.coverageRowReference, second.coverageRowReference);
    merged.coverageRowScore = averageDouble(first.coverageRowScore, second.coverageRowScore);
    merged.coverageCellRaw = averageDouble(first.coverageCellRaw, second.coverageCellRaw);
    merged.coverageCellReference =
        averageDouble(first.coverageCellReference, second.coverageCellReference);
    merged.coverageCellScore = averageDouble(first.coverageCellScore, second.coverageCellScore);
    merged.collisionDamageTotal =
        averageDouble(first.collisionDamageTotal, second.collisionDamageTotal);
    merged.damageTotal = averageDouble(first.damageTotal, second.damageTotal);
    merged.exitDoorRaw = averageDouble(first.exitDoorRaw, second.exitDoorRaw);
    merged.exitedThroughDoor = merged.exitDoorRaw >= 0.5;
    merged.healthAverage = averageDouble(first.healthAverage, second.healthAverage);
    merged.exitDoorBonus = averageDouble(first.exitDoorBonus, second.exitDoorBonus);
    merged.totalFitness = totalFitness;
    return merged;
}

FitnessEvaluation fitnessEvaluationDuckClockMerge(std::span<const FitnessEvaluation> evaluations)
{
    DIRTSIM_ASSERT(
        evaluations.size() == 4, "FitnessModelBundle: duck clock merge requires exactly 4 passes");

    const auto* primaryPassOne = fitnessEvaluationDuckBreakdownGet(evaluations[0]);
    const auto* oppositePassOne = fitnessEvaluationDuckBreakdownGet(evaluations[1]);
    const auto* primaryPassTwo = fitnessEvaluationDuckBreakdownGet(evaluations[2]);
    const auto* oppositePassTwo = fitnessEvaluationDuckBreakdownGet(evaluations[3]);
    DIRTSIM_ASSERT(
        primaryPassOne != nullptr && oppositePassOne != nullptr && primaryPassTwo != nullptr
            && oppositePassTwo != nullptr,
        "FitnessModelBundle: duck clock merge requires duck breakdowns");

    const double primarySideAverage =
        averageDouble(evaluations[0].totalFitness, evaluations[2].totalFitness);
    const double oppositeSideAverage =
        averageDouble(evaluations[1].totalFitness, evaluations[3].totalFitness);
    const bool usePrimarySide = primarySideAverage <= oppositeSideAverage;
    const double finalFitness = std::min(primarySideAverage, oppositeSideAverage);

    const DuckFitnessBreakdown& chosenFirst = usePrimarySide ? *primaryPassOne : *oppositePassOne;
    const DuckFitnessBreakdown& chosenSecond = usePrimarySide ? *primaryPassTwo : *oppositePassTwo;
    const DuckFitnessBreakdown merged =
        duckFitnessBreakdownAverage(chosenFirst, chosenSecond, finalFitness);

    return FitnessEvaluation{
        .totalFitness = finalFitness,
        .details = merged,
    };
}

} // namespace

FitnessModelBundle fitnessModelResolve(OrganismType organismType, Scenario::EnumType scenarioId)
{
    FitnessModelBundle bundle;

    switch (organismType) {
        case OrganismType::TREE:
            bundle.evaluate = fitnessEvaluationTreeEvaluate;
            bundle.formatLogSummary = fitnessEvaluationTreeLogSummary;
            bundle.generatePresentation = fitnessEvaluationTreePresentationGenerate;
            bundle.mergePasses = fitnessEvaluationIdentityMerge;
            return bundle;
        case OrganismType::DUCK:
            bundle.evaluate = fitnessEvaluationDuckEvaluate;
            bundle.formatLogSummary = fitnessEvaluationNoopLogSummary;
            bundle.generatePresentation = fitnessEvaluationDuckPresentationGenerate;
            bundle.mergePasses = scenarioId == Scenario::EnumType::Clock
                ? fitnessEvaluationDuckClockMerge
                : fitnessEvaluationIdentityMerge;
            return bundle;
        case OrganismType::NES_DUCK:
            bundle.evaluate = fitnessEvaluationNesDuckEvaluate;
            bundle.formatLogSummary = fitnessEvaluationNoopLogSummary;
            bundle.generatePresentation = fitnessEvaluationNesDuckPresentationGenerate;
            bundle.mergePasses = fitnessEvaluationIdentityMerge;
            return bundle;
        case OrganismType::GOOSE:
            bundle.evaluate = fitnessEvaluationGenericEvaluate;
            bundle.formatLogSummary = fitnessEvaluationNoopLogSummary;
            bundle.generatePresentation = fitnessEvaluationGoosePresentationGenerate;
            bundle.mergePasses = fitnessEvaluationIdentityMerge;
            return bundle;
    }

    bundle.evaluate = fitnessEvaluationGenericEvaluate;
    bundle.formatLogSummary = fitnessEvaluationNoopLogSummary;
    bundle.generatePresentation = fitnessEvaluationGoosePresentationGenerate;
    bundle.mergePasses = fitnessEvaluationIdentityMerge;
    return bundle;
}

} // namespace DirtSim::Server::EvolutionSupport
