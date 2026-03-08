#include "LegacyFitnessReportGenerator.h"

#include <cmath>
#include <utility>

namespace DirtSim::Server::EvolutionSupport {

namespace {

Api::FitnessMetric makeFitnessMetric(
    std::string key,
    std::string label,
    std::string group,
    double raw,
    double normalized,
    std::optional<double> reference,
    std::string unit)
{
    return Api::FitnessMetric{
        .key = std::move(key),
        .label = std::move(label),
        .group = std::move(group),
        .raw = raw,
        .normalized = normalized,
        .reference = reference,
        .weight = std::nullopt,
        .contribution = std::nullopt,
        .unit = std::move(unit),
    };
}

std::optional<double> optionalPositive(double value)
{
    if (value <= 0.0) {
        return std::nullopt;
    }
    return value;
}

Api::FitnessBreakdownReport duckFitnessReportBuild(const DuckFitnessBreakdown& breakdown)
{
    Api::FitnessBreakdownReport report{
        .organismType = OrganismType::DUCK,
        .modelId = "duck_v2",
        .modelVersion = 2,
        .totalFitness = breakdown.totalFitness,
        .totalFormula = "survival * (1 + movement) + exit_door_bonus",
        .metrics = {},
    };

    report.metrics.reserve(18);
    report.metrics.push_back(makeFitnessMetric(
        "survival",
        "Survival",
        "survival",
        breakdown.survivalRaw,
        breakdown.survivalScore,
        optionalPositive(breakdown.survivalReference),
        "seconds"));
    report.metrics.push_back(makeFitnessMetric(
        "energy_avg",
        "Energy Avg",
        "energy",
        breakdown.energyAverage,
        breakdown.energyAverage,
        std::nullopt,
        "ratio"));
    report.metrics.push_back(makeFitnessMetric(
        "energy_limited_seconds",
        "Energy Limited",
        "energy",
        breakdown.energyLimitedSeconds,
        breakdown.energyLimitedSeconds,
        std::nullopt,
        "seconds"));
    report.metrics.push_back(makeFitnessMetric(
        "energy_consumed_total",
        "Energy Consumed",
        "energy",
        breakdown.energyConsumedTotal,
        breakdown.energyConsumedTotal,
        std::nullopt,
        "energy"));
    report.metrics.push_back(makeFitnessMetric(
        "wing_up_seconds",
        "Wing Up",
        "wings",
        breakdown.wingUpSeconds,
        breakdown.wingUpSeconds,
        std::nullopt,
        "seconds"));
    report.metrics.push_back(makeFitnessMetric(
        "wing_down_seconds",
        "Wing Down",
        "wings",
        breakdown.wingDownSeconds,
        breakdown.wingDownSeconds,
        std::nullopt,
        "seconds"));
    report.metrics.push_back(makeFitnessMetric(
        "coverage_columns",
        "Coverage Columns",
        "coverage",
        breakdown.coverageColumnRaw,
        breakdown.coverageColumnScore,
        optionalPositive(breakdown.coverageColumnReference),
        "cells"));
    report.metrics.push_back(makeFitnessMetric(
        "coverage_rows",
        "Coverage Rows",
        "coverage",
        breakdown.coverageRowRaw,
        breakdown.coverageRowScore,
        optionalPositive(breakdown.coverageRowReference),
        "cells"));
    report.metrics.push_back(makeFitnessMetric(
        "coverage_cells",
        "Coverage Cells",
        "coverage",
        breakdown.coverageCellRaw,
        breakdown.coverageCellScore,
        optionalPositive(breakdown.coverageCellReference),
        "cells"));
    report.metrics.push_back(makeFitnessMetric(
        "coverage_total",
        "Coverage Total",
        "coverage",
        breakdown.coverageScore,
        breakdown.coverageScore,
        std::nullopt,
        "score"));
    report.metrics.push_back(makeFitnessMetric(
        "effort",
        "Effort",
        "effort",
        breakdown.effortRaw,
        breakdown.effortScore,
        optionalPositive(breakdown.effortReference),
        "ratio"));
    report.metrics.push_back(makeFitnessMetric(
        "effort_penalty",
        "Effort Penalty",
        "effort",
        breakdown.effortPenaltyRaw,
        breakdown.effortPenaltyScore,
        std::nullopt,
        "score"));
    report.metrics.push_back(makeFitnessMetric(
        "movement",
        "Movement",
        "movement",
        breakdown.movementRaw,
        breakdown.movementScore,
        std::nullopt,
        "score"));
    report.metrics.push_back(makeFitnessMetric(
        "health_avg",
        "Health Avg",
        "health",
        breakdown.healthAverage,
        breakdown.healthAverage,
        std::nullopt,
        "ratio"));
    report.metrics.push_back(makeFitnessMetric(
        "collision_damage",
        "Collision Damage",
        "health",
        breakdown.collisionDamageTotal,
        breakdown.collisionDamageTotal,
        std::nullopt,
        "damage"));
    report.metrics.push_back(makeFitnessMetric(
        "damage_total",
        "Damage Total",
        "health",
        breakdown.damageTotal,
        breakdown.damageTotal,
        std::nullopt,
        "damage"));
    report.metrics.push_back(makeFitnessMetric(
        "exit_door",
        "Exit Door",
        "door",
        breakdown.exitDoorRaw,
        breakdown.exitDoorBonus,
        std::nullopt,
        "bool"));
    report.metrics.push_back(makeFitnessMetric(
        "exit_door_bonus",
        "Exit Door Bonus",
        "door",
        breakdown.exitDoorBonus,
        breakdown.exitDoorBonus,
        std::nullopt,
        "score"));

    return report;
}

Api::FitnessBreakdownReport treeFitnessReportBuild(const TreeFitnessBreakdown& breakdown)
{
    Api::FitnessBreakdownReport report{
        .organismType = OrganismType::TREE,
        .modelId = "tree_v1",
        .modelVersion = 1,
        .totalFitness = breakdown.totalFitness,
        .totalFormula =
            "survival*(1+energy)*(1+resource)+partial+stage+structure+milestone+command+seed",
        .metrics = {},
    };

    report.metrics.reserve(14);
    report.metrics.push_back(makeFitnessMetric(
        "survival",
        "Survival",
        "survival",
        breakdown.survivalRaw,
        breakdown.survivalScore,
        optionalPositive(breakdown.survivalReference),
        "seconds"));
    report.metrics.push_back(makeFitnessMetric(
        "energy_max",
        "Max Energy",
        "energy",
        breakdown.maxEnergyRaw,
        breakdown.maxEnergyNormalized,
        optionalPositive(breakdown.energyReference),
        "energy"));
    report.metrics.push_back(makeFitnessMetric(
        "energy_final",
        "Final Energy",
        "energy",
        breakdown.finalEnergyRaw,
        breakdown.finalEnergyNormalized,
        optionalPositive(breakdown.energyReference),
        "energy"));
    report.metrics.push_back(makeFitnessMetric(
        "energy_score",
        "Energy Score",
        "energy",
        breakdown.energyScore,
        breakdown.energyScore,
        std::nullopt,
        "score"));
    report.metrics.push_back(makeFitnessMetric(
        "resource_energy_produced",
        "Energy Produced",
        "resource",
        breakdown.producedEnergyRaw,
        breakdown.producedEnergyNormalized,
        optionalPositive(breakdown.energyReference),
        "energy"));
    report.metrics.push_back(makeFitnessMetric(
        "resource_water_absorbed",
        "Water Absorbed",
        "resource",
        breakdown.absorbedWaterRaw,
        breakdown.absorbedWaterNormalized,
        optionalPositive(breakdown.waterReference),
        "water"));
    report.metrics.push_back(makeFitnessMetric(
        "resource_score",
        "Resource Score",
        "resource",
        breakdown.resourceScore,
        breakdown.resourceScore,
        std::nullopt,
        "score"));
    report.metrics.push_back(makeFitnessMetric(
        "partial_structure_bonus",
        "Partial Structure Bonus",
        "bonus",
        breakdown.partialStructureBonus,
        breakdown.partialStructureBonus,
        std::nullopt,
        "score"));
    report.metrics.push_back(makeFitnessMetric(
        "stage_bonus",
        "Stage Bonus",
        "bonus",
        breakdown.stageBonus,
        breakdown.stageBonus,
        std::nullopt,
        "score"));
    report.metrics.push_back(makeFitnessMetric(
        "structure_bonus",
        "Structure Bonus",
        "bonus",
        breakdown.structureBonus,
        breakdown.structureBonus,
        std::nullopt,
        "score"));
    report.metrics.push_back(makeFitnessMetric(
        "milestone_bonus",
        "Milestone Bonus",
        "bonus",
        breakdown.milestoneBonus,
        breakdown.milestoneBonus,
        std::nullopt,
        "score"));
    report.metrics.push_back(makeFitnessMetric(
        "command_score",
        "Command Score",
        "command",
        breakdown.commandScore,
        breakdown.commandScore,
        std::nullopt,
        "score"));
    report.metrics.push_back(makeFitnessMetric(
        "seed_score",
        "Seed Score",
        "seed",
        breakdown.seedScore,
        breakdown.seedScore,
        std::nullopt,
        "score"));
    report.metrics.push_back(makeFitnessMetric(
        "total_fitness",
        "Total Fitness",
        "total",
        breakdown.totalFitness,
        breakdown.totalFitness,
        std::nullopt,
        "score"));

    return report;
}

} // namespace

std::optional<Api::FitnessBreakdownReport> fitnessEvaluationLegacyReportGenerate(
    const FitnessEvaluation& evaluation)
{
    if (const auto* duck = fitnessEvaluationDuckBreakdownGet(evaluation)) {
        return duckFitnessReportBuild(*duck);
    }
    if (const auto* tree = fitnessEvaluationTreeBreakdownGet(evaluation)) {
        return treeFitnessReportBuild(*tree);
    }
    return std::nullopt;
}

} // namespace DirtSim::Server::EvolutionSupport
