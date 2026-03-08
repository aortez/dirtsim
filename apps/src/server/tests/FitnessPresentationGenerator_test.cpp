#include "server/evolution/FitnessPresentationGenerator.h"
#include <gtest/gtest.h>
#include <string_view>

using namespace DirtSim;
using namespace DirtSim::Server::EvolutionSupport;

namespace {

const Api::FitnessPresentationMetric* fitnessMetricFind(
    const Api::FitnessPresentationSection& section, std::string_view key)
{
    for (const auto& metric : section.metrics) {
        if (metric.key == key) {
            return &metric;
        }
    }
    return nullptr;
}

} // namespace

TEST(FitnessPresentationGeneratorTest, BuildsDuckPresentationFromNativeBreakdown)
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

    const Api::FitnessPresentation presentation =
        fitnessEvaluationDuckPresentationGenerate(evaluation);
    EXPECT_EQ(presentation.organismType, OrganismType::DUCK);
    EXPECT_EQ(presentation.modelId, "duck");
    EXPECT_DOUBLE_EQ(presentation.totalFitness, 2.75);
    ASSERT_EQ(presentation.sections.size(), 1u);
    EXPECT_EQ(presentation.sections[0].key, "overview");
}

TEST(FitnessPresentationGeneratorTest, BuildsTreePresentationFromNativeBreakdown)
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

    const Api::FitnessPresentation presentation =
        fitnessEvaluationTreePresentationGenerate(evaluation);
    EXPECT_EQ(presentation.organismType, OrganismType::TREE);
    EXPECT_EQ(presentation.modelId, "tree");
    ASSERT_EQ(presentation.sections.size(), 5u);
    EXPECT_EQ(presentation.sections[0].key, "survival");
    EXPECT_EQ(presentation.sections[1].key, "energy");
    EXPECT_EQ(presentation.sections[2].key, "resources");
    EXPECT_EQ(presentation.sections[3].key, "structure");
    EXPECT_EQ(presentation.sections[4].key, "behavior");

    const Api::FitnessPresentationMetric* energyMax =
        fitnessMetricFind(presentation.sections[1], "energy_max");
    ASSERT_NE(energyMax, nullptr);
    EXPECT_DOUBLE_EQ(energyMax->value, 30.0);
    ASSERT_TRUE(energyMax->reference.has_value());
    EXPECT_DOUBLE_EQ(energyMax->reference.value(), 100.0);
    ASSERT_TRUE(energyMax->normalized.has_value());
    EXPECT_DOUBLE_EQ(energyMax->normalized.value(), 0.3);

    const Api::FitnessPresentationMetric* waterAbsorbed =
        fitnessMetricFind(presentation.sections[2], "water_absorbed");
    ASSERT_NE(waterAbsorbed, nullptr);
    EXPECT_DOUBLE_EQ(waterAbsorbed->value, 42.0);
    ASSERT_TRUE(waterAbsorbed->reference.has_value());
    EXPECT_DOUBLE_EQ(waterAbsorbed->reference.value(), 80.0);
    ASSERT_TRUE(waterAbsorbed->normalized.has_value());
    EXPECT_DOUBLE_EQ(waterAbsorbed->normalized.value(), 0.4);
}

TEST(FitnessPresentationGeneratorTest, FallsBackWhenTreeDetailsAreMissing)
{
    const FitnessEvaluation evaluation{
        .totalFitness = 1.25,
        .details = std::monostate{},
    };

    const Api::FitnessPresentation presentation =
        fitnessEvaluationTreePresentationGenerate(evaluation);
    EXPECT_EQ(presentation.modelId, "tree");
    EXPECT_EQ(presentation.totalFitness, 1.25);
    ASSERT_EQ(presentation.sections.size(), 1u);
    EXPECT_EQ(presentation.sections[0].key, "overview");
}
