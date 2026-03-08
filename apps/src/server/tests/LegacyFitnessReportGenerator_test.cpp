#include "server/evolution/LegacyFitnessReportGenerator.h"
#include <gtest/gtest.h>
#include <string_view>

using namespace DirtSim;
using namespace DirtSim::Server::EvolutionSupport;

namespace {

const Api::FitnessMetric* fitnessMetricFind(
    const Api::FitnessBreakdownReport& report, std::string_view key)
{
    for (const auto& metric : report.metrics) {
        if (metric.key == key) {
            return &metric;
        }
    }
    return nullptr;
}

} // namespace

TEST(LegacyFitnessReportGeneratorTest, BuildsDuckReportFromNativeBreakdown)
{
    const FitnessEvaluation evaluation{
        .totalFitness = 2.75,
        .details =
            DuckFitnessBreakdown{
                .survivalRaw = 20.0,
                .survivalReference = 40.0,
                .survivalScore = 0.5,
                .wingUpSeconds = 3.0,
                .exitedThroughDoor = true,
                .exitDoorRaw = 1.0,
                .exitDoorBonus = 0.5,
                .totalFitness = 2.75,
            },
    };

    const auto report = fitnessEvaluationLegacyReportGenerate(evaluation);
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->organismType, OrganismType::DUCK);
    EXPECT_EQ(report->modelId, "duck_v2");
    EXPECT_EQ(report->modelVersion, 2);
    EXPECT_DOUBLE_EQ(report->totalFitness, 2.75);

    const Api::FitnessMetric* wingUpSeconds = fitnessMetricFind(report.value(), "wing_up_seconds");
    ASSERT_NE(wingUpSeconds, nullptr);
    EXPECT_DOUBLE_EQ(wingUpSeconds->raw, 3.0);
    EXPECT_EQ(wingUpSeconds->unit, "seconds");

    const Api::FitnessMetric* exitDoor = fitnessMetricFind(report.value(), "exit_door");
    ASSERT_NE(exitDoor, nullptr);
    EXPECT_DOUBLE_EQ(exitDoor->raw, 1.0);
    EXPECT_DOUBLE_EQ(exitDoor->normalized, 0.5);
    EXPECT_EQ(exitDoor->unit, "bool");
}

TEST(LegacyFitnessReportGeneratorTest, BuildsTreeReportFromNativeBreakdown)
{
    const FitnessEvaluation evaluation{
        .totalFitness = 4.25,
        .details =
            TreeFitnessBreakdown{
                .survivalRaw = 90.0,
                .survivalReference = 120.0,
                .survivalScore = 0.75,
                .maxEnergyRaw = 30.0,
                .maxEnergyNormalized = 0.3,
                .finalEnergyRaw = 20.0,
                .finalEnergyNormalized = 0.2,
                .energyReference = 100.0,
                .energyScore = 0.8,
                .producedEnergyRaw = 55.0,
                .producedEnergyNormalized = 0.6,
                .absorbedWaterRaw = 42.0,
                .absorbedWaterNormalized = 0.4,
                .waterReference = 80.0,
                .resourceScore = 0.7,
                .partialStructureBonus = 0.1,
                .stageBonus = 0.2,
                .structureBonus = 0.3,
                .milestoneBonus = 0.4,
                .commandScore = 0.5,
                .seedScore = 0.6,
                .totalFitness = 4.25,
            },
    };

    const auto report = fitnessEvaluationLegacyReportGenerate(evaluation);
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->organismType, OrganismType::TREE);
    EXPECT_EQ(report->modelId, "tree_v1");
    EXPECT_EQ(report->modelVersion, 1);

    const Api::FitnessMetric* energyMax = fitnessMetricFind(report.value(), "energy_max");
    ASSERT_NE(energyMax, nullptr);
    EXPECT_DOUBLE_EQ(energyMax->raw, 30.0);
    ASSERT_TRUE(energyMax->reference.has_value());
    EXPECT_DOUBLE_EQ(energyMax->reference.value(), 100.0);
    EXPECT_DOUBLE_EQ(energyMax->normalized, 0.3);

    const Api::FitnessMetric* waterAbsorbed =
        fitnessMetricFind(report.value(), "resource_water_absorbed");
    ASSERT_NE(waterAbsorbed, nullptr);
    EXPECT_DOUBLE_EQ(waterAbsorbed->raw, 42.0);
    ASSERT_TRUE(waterAbsorbed->reference.has_value());
    EXPECT_DOUBLE_EQ(waterAbsorbed->reference.value(), 80.0);
    EXPECT_DOUBLE_EQ(waterAbsorbed->normalized, 0.4);
}

TEST(LegacyFitnessReportGeneratorTest, ReturnsNoReportForMonostateEvaluation)
{
    const FitnessEvaluation evaluation{
        .totalFitness = 1.25,
        .details = std::monostate{},
    };

    EXPECT_FALSE(fitnessEvaluationLegacyReportGenerate(evaluation).has_value());
}
