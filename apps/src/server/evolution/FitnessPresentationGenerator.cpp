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

const char* smbEndReasonText(SmbEpisodeEndReason endReason)
{
    switch (endReason) {
        case SmbEpisodeEndReason::None:
            return "running";
        case SmbEpisodeEndReason::LifeLost:
            return "life_lost";
        case SmbEpisodeEndReason::NoProgressTimeout:
            return "no_progress_timeout";
    }

    return "unknown";
}

std::string nesSuperMarioBrosSummaryBuild(const NesSuperMarioBrosFitnessBreakdown& breakdown)
{
    std::ostringstream summary;
    summary << std::fixed << std::setprecision(4);
    summary << "Reward " << breakdown.totalFitness;
    summary << " | Best Stage " << breakdown.bestStageIndex;
    summary << " | Best X " << breakdown.bestAbsoluteX;
    summary << " | Gameplay " << breakdown.gameplayFrames << " frames";
    summary << " | Since Progress " << breakdown.framesSinceProgress << " frames";
    summary << " | End " << smbEndReasonText(breakdown.endReason);
    return summary.str();
}

const char* duckExitText(bool exitedThroughDoor)
{
    return exitedThroughDoor ? "yes" : "no";
}

std::string duckSummaryBuild(const DuckFitnessBreakdown& breakdown, bool includeClockExit)
{
    std::ostringstream summary;
    summary << std::fixed << std::setprecision(4);
    summary << "Survival " << breakdown.survivalScore;
    summary << " | Movement " << breakdown.movementScore;
    summary << " | Coverage " << breakdown.coverageScore;
    if (includeClockExit) {
        summary << " | Exit " << duckExitText(breakdown.exitedThroughDoor);
    }
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

Api::FitnessPresentation duckPresentationBuild(
    const FitnessEvaluation& evaluation, const DuckFitnessBreakdown& duck, bool includeClockExit)
{
    Api::FitnessPresentation presentation{
        .organismType = OrganismType::DUCK,
        .modelId = "duck",
        .totalFitness = evaluation.totalFitness,
        .summary = duckSummaryBuild(duck, includeClockExit),
        .sections = {},
    };

    presentation.sections.reserve(includeClockExit ? 7u : 5u);
    presentation.sections.push_back(makeSection(
        "survival",
        "Survival",
        duck.survivalScore,
        { makeMetric(
            "lifespan",
            "Lifespan",
            duck.survivalRaw,
            optionalPositive(duck.survivalReference),
            duck.survivalScore,
            "seconds") }));
    presentation.sections.push_back(makeSection(
        "movement",
        "Movement",
        duck.movementScore,
        {
            makeMetric(
                "movement_raw",
                "Movement Raw",
                duck.movementRaw,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "coverage_score",
                "Coverage Score",
                duck.coverageScore,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "effort_penalty_score",
                "Effort Penalty",
                duck.effortPenaltyScore,
                std::nullopt,
                std::nullopt,
                "score"),
        }));
    presentation.sections.push_back(makeSection(
        "coverage",
        "Coverage",
        duck.coverageScore,
        {
            makeMetric(
                "coverage_columns",
                "Columns",
                duck.coverageColumnRaw,
                optionalPositive(duck.coverageColumnReference),
                duck.coverageColumnScore,
                "columns"),
            makeMetric(
                "coverage_rows",
                "Rows",
                duck.coverageRowRaw,
                optionalPositive(duck.coverageRowReference),
                duck.coverageRowScore,
                "rows"),
            makeMetric(
                "coverage_cells",
                "Cells",
                duck.coverageCellRaw,
                optionalPositive(duck.coverageCellReference),
                duck.coverageCellScore,
                "cells"),
        }));
    presentation.sections.push_back(makeSection(
        "effort",
        "Effort",
        std::nullopt,
        {
            makeMetric(
                "effort",
                "Effort",
                duck.effortRaw,
                optionalPositive(duck.effortReference),
                duck.effortScore,
                "effort"),
            makeMetric(
                "effort_penalty_raw",
                "Effort Penalty Raw",
                duck.effortPenaltyRaw,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "wing_up_seconds",
                "Wing Up",
                duck.wingUpSeconds,
                std::nullopt,
                std::nullopt,
                "seconds"),
            makeMetric(
                "wing_down_seconds",
                "Wing Down",
                duck.wingDownSeconds,
                std::nullopt,
                std::nullopt,
                "seconds"),
        }));
    presentation.sections.push_back(makeSection(
        "condition",
        "Condition",
        std::nullopt,
        {
            makeMetric(
                "energy_average",
                "Energy Average",
                duck.energyAverage,
                std::nullopt,
                std::nullopt,
                "energy"),
            makeMetric(
                "energy_consumed_total",
                "Energy Consumed",
                duck.energyConsumedTotal,
                std::nullopt,
                std::nullopt,
                "energy"),
            makeMetric(
                "energy_limited_seconds",
                "Energy-Limited Time",
                duck.energyLimitedSeconds,
                std::nullopt,
                std::nullopt,
                "seconds"),
            makeMetric(
                "health_average",
                "Health Average",
                duck.healthAverage,
                std::nullopt,
                std::nullopt,
                "health"),
            makeMetric(
                "collision_damage_total",
                "Collision Damage",
                duck.collisionDamageTotal,
                std::nullopt,
                std::nullopt,
                "damage"),
            makeMetric(
                "damage_total",
                "Total Damage",
                duck.damageTotal,
                std::nullopt,
                std::nullopt,
                "damage"),
        }));
    if (includeClockExit) {
        presentation.sections.push_back(makeSection(
            "clock_course",
            "Clock Course",
            duck.traversalBonus + duck.obstacleBonus,
            {
                makeMetric(
                    "left_wall_touches",
                    "Left Wall Touches",
                    duck.leftWallTouches,
                    std::nullopt,
                    std::nullopt,
                    "touches"),
                makeMetric(
                    "right_wall_touches",
                    "Right Wall Touches",
                    duck.rightWallTouches,
                    std::nullopt,
                    std::nullopt,
                    "touches"),
                makeMetric(
                    "full_traversals",
                    "Full Traversals",
                    duck.fullTraversals,
                    std::nullopt,
                    std::nullopt,
                    "traversals"),
                makeMetric(
                    "traversal_bonus",
                    "Traversal Bonus",
                    duck.traversalBonus,
                    std::nullopt,
                    std::nullopt,
                    "score"),
                makeMetric(
                    "traversal_score",
                    "Traversal Score",
                    duck.traversalScore,
                    std::nullopt,
                    std::nullopt,
                    "score"),
                makeMetric(
                    "pit_clears",
                    "Pit Clears",
                    duck.pitClears,
                    std::nullopt,
                    std::nullopt,
                    "clears"),
                makeMetric(
                    "pit_clear_bonus",
                    "Pit Clear Bonus",
                    duck.pitClearBonus,
                    std::nullopt,
                    std::nullopt,
                    "score"),
                makeMetric(
                    "pit_opportunities",
                    "Pit Opportunities",
                    duck.pitOpportunities,
                    std::nullopt,
                    std::nullopt,
                    "opportunities"),
                makeMetric(
                    "pit_clear_score",
                    "Pit Clear Score",
                    duck.pitClearScore,
                    std::nullopt,
                    std::nullopt,
                    "score"),
                makeMetric(
                    "hurdle_clears",
                    "Hurdle Clears",
                    duck.hurdleClears,
                    std::nullopt,
                    std::nullopt,
                    "clears"),
                makeMetric(
                    "hurdle_clear_bonus",
                    "Hurdle Clear Bonus",
                    duck.hurdleClearBonus,
                    std::nullopt,
                    std::nullopt,
                    "score"),
                makeMetric(
                    "hurdle_opportunities",
                    "Hurdle Opportunities",
                    duck.hurdleOpportunities,
                    std::nullopt,
                    std::nullopt,
                    "opportunities"),
                makeMetric(
                    "hurdle_clear_score",
                    "Hurdle Clear Score",
                    duck.hurdleClearScore,
                    std::nullopt,
                    std::nullopt,
                    "score"),
                makeMetric(
                    "obstacle_score",
                    "Obstacle Score",
                    duck.obstacleScore,
                    std::nullopt,
                    std::nullopt,
                    "score"),
                makeMetric(
                    "obstacle_bonus",
                    "Obstacle Bonus",
                    duck.obstacleBonus,
                    std::nullopt,
                    std::nullopt,
                    "score"),
            }));

        std::vector<Api::FitnessPresentationMetric> clockExitMetrics{
            makeMetric(
                "exit_door_raw",
                "Exit Door Raw",
                duck.exitDoorRaw,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "exit_door_proximity_score",
                "Exit Door Proximity",
                duck.exitDoorProximityScore,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "exit_door_proximity_bonus",
                "Exit Door Proximity Bonus",
                duck.exitDoorProximityBonus,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "exit_door_bonus",
                "Exit Door Completion Bonus",
                duck.exitDoorBonus,
                std::nullopt,
                std::nullopt,
                "score"),
            makeMetric(
                "exit_door_time",
                "Exit Door Time",
                duck.exitDoorTime,
                std::nullopt,
                std::nullopt,
                "seconds"),
        };
        if (duck.exitDoorDistanceObserved) {
            clockExitMetrics.push_back(makeMetric(
                "best_exit_door_distance_cells",
                "Best Exit Distance",
                duck.bestExitDoorDistanceCells,
                10.0,
                std::nullopt,
                "cells"));
        }

        presentation.sections.push_back(makeSection(
            "clock_exit",
            "Clock Exit",
            duck.exitDoorBonus + duck.exitDoorProximityBonus,
            std::move(clockExitMetrics)));
    }

    return presentation;
}

} // namespace

Api::FitnessPresentation fitnessEvaluationDuckPresentationGenerate(
    const FitnessEvaluation& evaluation)
{
    if (const auto* duck = fitnessEvaluationDuckBreakdownGet(evaluation)) {
        return duckPresentationBuild(evaluation, *duck, false);
    }
    return genericPresentationBuild(
        OrganismType::DUCK, "duck", evaluation, "Detailed duck presentation is not available yet.");
}

Api::FitnessPresentation fitnessEvaluationDuckClockPresentationGenerate(
    const FitnessEvaluation& evaluation)
{
    if (const auto* duck = fitnessEvaluationDuckBreakdownGet(evaluation)) {
        return duckPresentationBuild(evaluation, *duck, true);
    }
    return genericPresentationBuild(
        OrganismType::DUCK,
        "duck",
        evaluation,
        "Detailed duck clock presentation is not available yet.");
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

Api::FitnessPresentation fitnessEvaluationNesGenericPresentationGenerate(
    const FitnessEvaluation& evaluation)
{
    return genericPresentationBuild(
        OrganismType::NES_DUCK,
        "nes_duck",
        evaluation,
        "Detailed NES presentation is not available yet.");
}

Api::FitnessPresentation fitnessEvaluationNesSuperMarioBrosPresentationGenerate(
    const FitnessEvaluation& evaluation)
{
    const auto* breakdown = fitnessEvaluationNesSuperMarioBrosBreakdownGet(evaluation);
    if (breakdown == nullptr) {
        return genericPresentationBuild(
            OrganismType::NES_DUCK,
            "nes_smb",
            evaluation,
            "Detailed NES Super Mario Bros presentation is not available.");
    }

    Api::FitnessPresentation presentation{
        .organismType = OrganismType::NES_DUCK,
        .modelId = "nes_smb",
        .totalFitness = evaluation.totalFitness,
        .summary = nesSuperMarioBrosSummaryBuild(*breakdown),
        .sections = {},
    };

    presentation.sections.reserve(3);
    presentation.sections.push_back(makeSection(
        "reward",
        "Reward Totals",
        breakdown->totalFitness,
        {
            makeMetric(
                "total_reward",
                "Total Reward",
                breakdown->totalFitness,
                std::nullopt,
                std::nullopt,
                "reward"),
            makeMetric(
                "distance_reward_total",
                "Distance Reward",
                breakdown->distanceRewardTotal,
                std::nullopt,
                std::nullopt,
                "reward"),
            makeMetric(
                "level_clear_reward_total",
                "Level Clear Reward",
                breakdown->levelClearRewardTotal,
                std::nullopt,
                std::nullopt,
                "reward"),
        }));
    presentation.sections.push_back(makeSection(
        "frontier",
        "Best Frontier",
        std::nullopt,
        {
            makeMetric(
                "best_stage_index",
                "Best Stage Index",
                static_cast<double>(breakdown->bestStageIndex),
                std::nullopt,
                std::nullopt,
                "stage"),
            makeMetric(
                "best_world",
                "Best World",
                static_cast<double>(breakdown->bestWorld),
                std::nullopt,
                std::nullopt,
                "world"),
            makeMetric(
                "best_level",
                "Best Level",
                static_cast<double>(breakdown->bestLevel),
                std::nullopt,
                std::nullopt,
                "level"),
            makeMetric(
                "best_absolute_x",
                "Best Absolute X",
                static_cast<double>(breakdown->bestAbsoluteX),
                std::nullopt,
                std::nullopt,
                "px"),
        }));
    presentation.sections.push_back(makeSection(
        "episode",
        "Episode State",
        std::nullopt,
        {
            makeMetric(
                "gameplay_frames",
                "Gameplay Frames",
                static_cast<double>(breakdown->gameplayFrames),
                std::nullopt,
                std::nullopt,
                "frames"),
            makeMetric(
                "frames_since_progress",
                "Frames Since Progress",
                static_cast<double>(breakdown->framesSinceProgress),
                breakdown->noProgressTimeoutFrames > 0
                    ? std::optional<double>(static_cast<double>(breakdown->noProgressTimeoutFrames))
                    : std::nullopt,
                breakdown->noProgressTimeoutFrames > 0
                    ? std::optional<double>(
                          static_cast<double>(breakdown->framesSinceProgress)
                          / static_cast<double>(breakdown->noProgressTimeoutFrames))
                    : std::nullopt,
                "frames"),
            makeMetric(
                "current_lives",
                "Current Lives",
                static_cast<double>(breakdown->currentLives),
                std::nullopt,
                std::nullopt,
                "lives"),
            makeMetric(
                "current_world",
                "Current World",
                static_cast<double>(breakdown->currentWorld),
                std::nullopt,
                std::nullopt,
                "world"),
            makeMetric(
                "current_level",
                "Current Level",
                static_cast<double>(breakdown->currentLevel),
                std::nullopt,
                std::nullopt,
                "level"),
            makeMetric(
                "current_absolute_x",
                "Current Absolute X",
                static_cast<double>(breakdown->currentAbsoluteX),
                std::nullopt,
                std::nullopt,
                "px"),
        }));

    return presentation;
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
