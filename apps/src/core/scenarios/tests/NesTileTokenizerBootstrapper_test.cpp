#include "core/scenarios/nes/NesTileTokenizerBootstrapper.h"

#include "core/scenarios/nes/NesTileTokenizer.h"
#include "core/scenarios/tests/NesTestRomPath.h"

#include <gtest/gtest.h>

using namespace DirtSim;

TEST(NesTileTokenizerBootstrapperTest, RejectsNonPositiveBootstrapFrameCount)
{
    const auto result = NesTileTokenizerBootstrapper::build(
        Scenario::EnumType::NesFlappyParatroopa,
        std::nullopt,
        NesTileTokenizerBootstrapper::Config{ .bootstrapFrames = 0 });

    ASSERT_TRUE(result.isError());
    EXPECT_NE(result.errorValue().find("bootstrapFrames must be positive"), std::string::npos);
}

TEST(NesTileTokenizerBootstrapperTest, RejectsScenarioConfigMismatch)
{
    Config::NesSuperMarioBros smbConfig = std::get<Config::NesSuperMarioBros>(
        makeDefaultConfig(Scenario::EnumType::NesSuperMarioBros));

    const auto result = NesTileTokenizerBootstrapper::build(
        Scenario::EnumType::NesFlappyParatroopa, ScenarioConfig{ smbConfig });

    ASSERT_TRUE(result.isError());
    EXPECT_NE(result.errorValue().find("config rejected"), std::string::npos);
}

TEST(NesTileTokenizerBootstrapperTest, BuildsFrozenTokenizerFromRuntime)
{
    const auto romPath = DirtSim::Test::resolveFlappyRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing for NES tile tokenizer bootstrapper test.";
    }

    Config::NesFlappyParatroopa nesConfig = std::get<Config::NesFlappyParatroopa>(
        makeDefaultConfig(Scenario::EnumType::NesFlappyParatroopa));
    nesConfig.romPath = romPath.value().string();
    nesConfig.requireSmolnesMapper = true;

    auto result = NesTileTokenizerBootstrapper::build(
        Scenario::EnumType::NesFlappyParatroopa, ScenarioConfig{ nesConfig });

    ASSERT_TRUE(result.isValue()) << result.errorValue();
    ASSERT_NE(result.value(), nullptr);
    EXPECT_EQ(result.value()->getMode(), NesTileTokenizer::Mode::Frozen);
    EXPECT_GT(result.value()->getMappedHashCount(), 0u);
}
