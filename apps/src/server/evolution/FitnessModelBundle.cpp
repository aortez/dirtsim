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

FitnessEvaluation fitnessEvaluationNesSuperMarioBrosEvaluate(const FitnessContext& context)
{
    if (context.nesFitnessDetails == nullptr) {
        return fitnessEvaluationNesDuckEvaluate(context);
    }

    const auto* snapshot = std::get_if<NesSuperMarioBrosFitnessSnapshot>(context.nesFitnessDetails);
    if (snapshot == nullptr) {
        return fitnessEvaluationNesDuckEvaluate(context);
    }

    const NesSuperMarioBrosFitnessBreakdown breakdown =
        NesEvaluator::evaluateSuperMarioBrosWithBreakdown(context);

    return FitnessEvaluation{
        .totalFitness = breakdown.totalFitness,
        .details = breakdown,
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

std::string fitnessEvaluationNesSuperMarioBrosLogSummary(const FitnessEvaluation& evaluation)
{
    const auto* breakdown = fitnessEvaluationNesSuperMarioBrosBreakdownGet(evaluation);
    if (breakdown == nullptr) {
        return "";
    }

    std::ostringstream summary;
    summary << std::fixed << std::setprecision(3);
    summary << "reward=" << breakdown->totalFitness;
    summary << " dist=" << breakdown->distanceRewardTotal;
    summary << " clear=" << breakdown->levelClearRewardTotal;
    summary << " stage=" << breakdown->bestStageIndex;
    summary << " x=" << breakdown->bestAbsoluteX;
    summary << " frames=" << breakdown->gameplayFrames;
    summary << " stalled=" << breakdown->framesSinceProgress;
    summary << " end=" << static_cast<int>(breakdown->endReason);
    return summary.str();
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

struct ObservedDoubleAverage {
    bool observed = false;
    double value = 0.0;
};

ObservedDoubleAverage averageObservedDouble(
    bool firstObserved, double firstValue, bool secondObserved, double secondValue)
{
    if (firstObserved && secondObserved) {
        return ObservedDoubleAverage{
            .observed = true,
            .value = averageDouble(firstValue, secondValue),
        };
    }
    if (firstObserved) {
        return ObservedDoubleAverage{
            .observed = true,
            .value = firstValue,
        };
    }
    if (secondObserved) {
        return ObservedDoubleAverage{
            .observed = true,
            .value = secondValue,
        };
    }
    return {};
}

DuckFitnessBreakdown duckFitnessBreakdownAverage(
    const DuckFitnessBreakdown& first, const DuckFitnessBreakdown& second, double totalFitness)
{
    const ObservedDoubleAverage exitDoorDistance = averageObservedDouble(
        first.exitDoorDistanceObserved,
        first.bestExitDoorDistanceCells,
        second.exitDoorDistanceObserved,
        second.bestExitDoorDistanceCells);
    const ObservedDoubleAverage exitDoorTime = averageObservedDouble(
        first.exitedThroughDoor, first.exitDoorTime, second.exitedThroughDoor, second.exitDoorTime);

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
    merged.collisionDamageTotal =
        averageDouble(first.collisionDamageTotal, second.collisionDamageTotal);
    merged.damageTotal = averageDouble(first.damageTotal, second.damageTotal);
    merged.fullTraversals = averageDouble(first.fullTraversals, second.fullTraversals);
    merged.traversalProgress = averageDouble(first.traversalProgress, second.traversalProgress);
    merged.traversalRatePer100Seconds =
        averageDouble(first.traversalRatePer100Seconds, second.traversalRatePer100Seconds);
    merged.traversalPoints = averageDouble(first.traversalPoints, second.traversalPoints);
    merged.hurdleClears = averageDouble(first.hurdleClears, second.hurdleClears);
    merged.hurdleOpportunities =
        averageDouble(first.hurdleOpportunities, second.hurdleOpportunities);
    merged.leftWallTouches = averageDouble(first.leftWallTouches, second.leftWallTouches);
    merged.pitClears = averageDouble(first.pitClears, second.pitClears);
    merged.pitOpportunities = averageDouble(first.pitOpportunities, second.pitOpportunities);
    merged.obstacleClears = averageDouble(first.obstacleClears, second.obstacleClears);
    merged.obstacleOpportunities =
        averageDouble(first.obstacleOpportunities, second.obstacleOpportunities);
    merged.obstacleClearRatePer100Seconds =
        averageDouble(first.obstacleClearRatePer100Seconds, second.obstacleClearRatePer100Seconds);
    merged.obstacleClearRatePoints =
        averageDouble(first.obstacleClearRatePoints, second.obstacleClearRatePoints);
    merged.obstacleCompetenceScore =
        averageDouble(first.obstacleCompetenceScore, second.obstacleCompetenceScore);
    merged.obstacleCompetencePoints =
        averageDouble(first.obstacleCompetencePoints, second.obstacleCompetencePoints);
    merged.rightWallTouches = averageDouble(first.rightWallTouches, second.rightWallTouches);
    merged.coursePoints = averageDouble(first.coursePoints, second.coursePoints);
    merged.exitDoorDistanceObserved = exitDoorDistance.observed;
    merged.bestExitDoorDistanceCells = exitDoorDistance.value;
    merged.exitDoorProximityScore =
        averageDouble(first.exitDoorProximityScore, second.exitDoorProximityScore);
    merged.exitDoorProximityPoints =
        averageDouble(first.exitDoorProximityPoints, second.exitDoorProximityPoints);
    merged.exitedThroughDoor = first.exitedThroughDoor || second.exitedThroughDoor;
    merged.exitDoorTime = exitDoorTime.value;
    merged.healthAverage = averageDouble(first.healthAverage, second.healthAverage);
    merged.exitDoorCompletionPoints =
        averageDouble(first.exitDoorCompletionPoints, second.exitDoorCompletionPoints);
    merged.survivalPoints = averageDouble(first.survivalPoints, second.survivalPoints);
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
            DIRTSIM_ASSERT(
                scenarioId == Scenario::EnumType::Clock,
                "FitnessModelBundle: duck fitness only supports the Clock scenario");
            bundle.evaluate = fitnessEvaluationDuckEvaluate;
            bundle.formatLogSummary = fitnessEvaluationNoopLogSummary;
            bundle.generatePresentation = fitnessEvaluationDuckClockPresentationGenerate;
            bundle.mergePasses = fitnessEvaluationDuckClockMerge;
            return bundle;
        case OrganismType::NES_DUCK:
            if (scenarioId == Scenario::EnumType::NesSuperMarioBros) {
                bundle.evaluate = fitnessEvaluationNesSuperMarioBrosEvaluate;
                bundle.formatLogSummary = fitnessEvaluationNesSuperMarioBrosLogSummary;
                bundle.generatePresentation =
                    fitnessEvaluationNesSuperMarioBrosPresentationGenerate;
            }
            else {
                bundle.evaluate = fitnessEvaluationNesDuckEvaluate;
                bundle.formatLogSummary = fitnessEvaluationNoopLogSummary;
                bundle.generatePresentation = fitnessEvaluationNesGenericPresentationGenerate;
            }
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
