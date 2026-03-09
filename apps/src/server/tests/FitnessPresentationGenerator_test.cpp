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
        .totalFitness = 6.395,
        .details =
            TreeFitnessBreakdown{
                .survivalRaw = 90.0,
                .survivalReference = 120.0,
                .survivalScore = 0.75,
                .maxEnergyRaw = 80.0,
                .maxEnergyNormalized = 0.8,
                .finalEnergyRaw = 80.0,
                .finalEnergyNormalized = 0.8,
                .energyReference = 100.0,
                .energyScore = 0.8,
                .producedEnergyRaw = 55.0,
                .producedEnergyNormalized = 0.8,
                .absorbedWaterRaw = 42.0,
                .absorbedWaterNormalized = 0.55,
                .waterReference = 80.0,
                .resourceScore = 0.7,
                .partialStructureBonus = 0.1,
                .stageBonus = 0.2,
                .structureBonus = 0.3,
                .milestoneBonus = 0.4,
                .commandScore = 0.5,
                .seedScore = 2.6,
                .totalFitness = 6.395,
                .coreFitness = 2.295,
                .bonusFitness = 4.1,
                .energyMaxWeightedComponent = 0.56,
                .energyFinalWeightedComponent = 0.24,
                .resourceEnergyWeightedComponent = 0.48,
                .resourceWaterWeightedComponent = 0.22,
                .rootBelowSeedBonus = 0.1,
                .woodAboveSeedBonus = 0.3,
                .commandsAccepted = 11,
                .commandsRejected = 3,
                .idleCancels = 2,
                .leafCount = 5,
                .rootCount = 2,
                .woodCount = 4,
                .partialStructurePartCount = 3,
                .seedCountBonus = 2.0,
                .seedDistanceBonus = 0.6,
                .seedDistanceReference = 10.0,
                .seedsProduced = 7,
                .landedSeedCount = 2,
                .averageLandedSeedDistance = 6.5,
                .maxLandedSeedDistance = 9.0,
            },
    };

    const Api::FitnessPresentation presentation =
        fitnessEvaluationTreePresentationGenerate(evaluation);
    EXPECT_EQ(presentation.organismType, OrganismType::TREE);
    EXPECT_EQ(presentation.modelId, "tree");
    EXPECT_EQ(
        presentation.summary,
        "Core 2.2950 = Survival 0.7500 x (1 + Energy 0.8000) x (1 + Resources 0.7000)\n"
        "Bonus 4.1000 = Structure 1.0000 + Commands/Seed 3.1000");
    ASSERT_EQ(presentation.sections.size(), 5u);
    EXPECT_EQ(presentation.sections[0].key, "survival");
    EXPECT_EQ(presentation.sections[1].key, "energy");
    EXPECT_EQ(presentation.sections[2].key, "resources");
    EXPECT_EQ(presentation.sections[3].key, "structure");
    EXPECT_EQ(presentation.sections[4].key, "commands_seed");
    EXPECT_EQ(presentation.sections[0].label, "Survival Factor");
    EXPECT_EQ(presentation.sections[3].label, "Structure Bonuses");

    const Api::FitnessPresentationMetric* energyMax =
        fitnessMetricFind(presentation.sections[1], "energy_max");
    ASSERT_NE(energyMax, nullptr);
    EXPECT_DOUBLE_EQ(energyMax->value, 80.0);
    ASSERT_TRUE(energyMax->reference.has_value());
    EXPECT_DOUBLE_EQ(energyMax->reference.value(), 100.0);
    ASSERT_TRUE(energyMax->normalized.has_value());
    EXPECT_DOUBLE_EQ(energyMax->normalized.value(), 0.8);

    const Api::FitnessPresentationMetric* energyMaxWeighted =
        fitnessMetricFind(presentation.sections[1], "energy_max_weighted");
    ASSERT_NE(energyMaxWeighted, nullptr);
    EXPECT_DOUBLE_EQ(energyMaxWeighted->value, 0.56);

    const Api::FitnessPresentationMetric* waterAbsorbed =
        fitnessMetricFind(presentation.sections[2], "water_absorbed");
    ASSERT_NE(waterAbsorbed, nullptr);
    EXPECT_DOUBLE_EQ(waterAbsorbed->value, 42.0);
    ASSERT_TRUE(waterAbsorbed->reference.has_value());
    EXPECT_DOUBLE_EQ(waterAbsorbed->reference.value(), 80.0);
    ASSERT_TRUE(waterAbsorbed->normalized.has_value());
    EXPECT_DOUBLE_EQ(waterAbsorbed->normalized.value(), 0.55);

    const Api::FitnessPresentationMetric* partialStructureParts =
        fitnessMetricFind(presentation.sections[3], "partial_structure_parts");
    ASSERT_NE(partialStructureParts, nullptr);
    EXPECT_DOUBLE_EQ(partialStructureParts->value, 3.0);

    const Api::FitnessPresentationMetric* commandsAccepted =
        fitnessMetricFind(presentation.sections[4], "commands_accepted");
    ASSERT_NE(commandsAccepted, nullptr);
    EXPECT_DOUBLE_EQ(commandsAccepted->value, 11.0);

    const Api::FitnessPresentationMetric* seedDistance =
        fitnessMetricFind(presentation.sections[4], "max_seed_distance");
    ASSERT_NE(seedDistance, nullptr);
    EXPECT_DOUBLE_EQ(seedDistance->value, 9.0);
    ASSERT_TRUE(seedDistance->reference.has_value());
    EXPECT_DOUBLE_EQ(seedDistance->reference.value(), 10.0);
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
