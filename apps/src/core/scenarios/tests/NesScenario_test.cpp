#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/scenarios/NesScenario.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "core/scenarios/nes/SmolnesRuntimeBackend.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <string>

using namespace DirtSim;

namespace {

void writeRomHeader(const std::filesystem::path& path, const std::array<uint8_t, 16>& header)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.is_open()) << "Failed to create ROM fixture: " << path.string();
    file.write(
        reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    ASSERT_TRUE(file.good()) << "Failed to write ROM fixture: " << path.string();
}

std::optional<std::filesystem::path> resolveNesFixtureRomPath()
{
    if (const char* romPathEnv = std::getenv("DIRTSIM_NES_TEST_ROM_PATH"); romPathEnv != nullptr) {
        const std::filesystem::path romPath{ romPathEnv };
        if (std::filesystem::exists(romPath)) {
            return romPath;
        }
    }

    const std::filesystem::path repoRelativeRomPath =
        std::filesystem::path("testdata") / "roms" / "Flappy.Paratroopa.World.Unl.nes";
    if (std::filesystem::exists(repoRelativeRomPath)) {
        return repoRelativeRomPath;
    }

    return std::nullopt;
}

} // namespace

TEST(NesScenarioTest, InspectRomAcceptsMapperZero)
{
    const std::filesystem::path romPath =
        std::filesystem::path(::testing::TempDir()) / "nes_mapper0_fixture.nes";
    writeRomHeader(
        romPath, { 'N', 'E', 'S', 0x1A, 0x02, 0x01, 0x01, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 });

    const NesRomCheckResult result = NesScenario::inspectRom(romPath);

    EXPECT_EQ(result.status, NesRomCheckStatus::Compatible);
    EXPECT_TRUE(result.isCompatible());
    EXPECT_EQ(result.mapper, 0u);
    EXPECT_EQ(result.prgBanks16k, 2u);
    EXPECT_EQ(result.chrBanks8k, 1u);
}

TEST(NesScenarioTest, InspectRomRejectsUnsupportedMapper)
{
    const std::filesystem::path romPath =
        std::filesystem::path(::testing::TempDir()) / "nes_mapper30_fixture.nes";
    writeRomHeader(
        romPath, { 'N', 'E', 'S', 0x1A, 0x20, 0x00, 0xE3, 0x10, 0, 0, 0, 0, 0, 0, 0, 0 });

    const NesRomCheckResult result = NesScenario::inspectRom(romPath);

    EXPECT_EQ(result.status, NesRomCheckStatus::UnsupportedMapper);
    EXPECT_FALSE(result.isCompatible());
    EXPECT_EQ(result.mapper, 30u);
}

TEST(NesScenarioTest, InspectRomRejectsInvalidHeader)
{
    const std::filesystem::path romPath =
        std::filesystem::path(::testing::TempDir()) / "nes_invalid_header_fixture.nes";
    writeRomHeader(
        romPath, { 'B', 'A', 'D', 0x1A, 0x02, 0x01, 0x01, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 });

    const NesRomCheckResult result = NesScenario::inspectRom(romPath);

    EXPECT_EQ(result.status, NesRomCheckStatus::InvalidHeader);
    EXPECT_FALSE(result.isCompatible());
}

TEST(NesScenarioTest, ScenarioConfigMapsToNesEnum)
{
    const ScenarioConfig config = makeDefaultConfig(Scenario::EnumType::Nes);
    ASSERT_TRUE(std::holds_alternative<Config::Nes>(config));
    EXPECT_EQ(getScenarioId(config), Scenario::EnumType::Nes);
}

TEST(NesScenarioTest, ScenarioRegistryRegistersNesScenario)
{
    GenomeRepository genomeRepository;
    const ScenarioRegistry registry = ScenarioRegistry::createDefault(genomeRepository);

    const auto ids = registry.getScenarioIds();
    EXPECT_NE(std::find(ids.begin(), ids.end(), Scenario::EnumType::Nes), ids.end());

    const ScenarioMetadata* metadata = registry.getMetadata(Scenario::EnumType::Nes);
    ASSERT_NE(metadata, nullptr);
    EXPECT_EQ(metadata->name, "NES");

    std::unique_ptr<ScenarioRunner> scenario = registry.createScenario(Scenario::EnumType::Nes);
    ASSERT_NE(scenario, nullptr);
    EXPECT_TRUE(std::holds_alternative<Config::Nes>(scenario->getConfig()));
}

TEST(NesScenarioTest, FlappyParatroopaRomLoadsAndTicks100Frames)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    auto scenario = std::make_unique<NesScenario>();
    const ScenarioMetadata& metadata = scenario->getMetadata();
    World world(metadata.requiredWidth, metadata.requiredHeight);

    Config::Nes config = std::get<Config::Nes>(scenario->getConfig());
    config.romPath = romPath.value().string();
    config.frameSkip = 1;
    config.requireSmolnesMapper = true;
    scenario->setConfig(config, world);
    scenario->setup(world);

    const NesRomCheckResult& romCheck = scenario->getLastRomCheck();
    ASSERT_TRUE(romCheck.isCompatible()) << "ROM compatibility check failed: " << romCheck.message
                                         << " (mapper=" << romCheck.mapper << ")";
    ASSERT_TRUE(scenario->isRuntimeRunning()) << scenario->getRuntimeLastError();
    ASSERT_TRUE(scenario->isRuntimeHealthy()) << scenario->getRuntimeLastError();

    constexpr double deltaTime = 1.0 / 60.0;
    constexpr int frameCount = 100;
    for (int frame = 0; frame < frameCount; ++frame) {
        scenario->tick(world, deltaTime);
    }

    EXPECT_TRUE(scenario->isRuntimeHealthy()) << scenario->getRuntimeLastError();
    EXPECT_EQ(scenario->getRuntimeRenderedFrameCount(), static_cast<uint64_t>(frameCount));

    ASSERT_TRUE(world.getData().scenario_video_frame.has_value());
    const ScenarioVideoFrame& videoFrame = world.getData().scenario_video_frame.value();
    EXPECT_EQ(videoFrame.width, SMOLNES_RUNTIME_FRAME_WIDTH);
    EXPECT_EQ(videoFrame.height, SMOLNES_RUNTIME_FRAME_HEIGHT);
    EXPECT_EQ(videoFrame.frame_id, static_cast<uint64_t>(frameCount));
    EXPECT_EQ(videoFrame.pixels.size(), static_cast<size_t>(SMOLNES_RUNTIME_FRAME_BYTES));
}

TEST(NesScenarioTest, ResetRestartsRuntimeFrameCounter)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    auto scenario = std::make_unique<NesScenario>();
    const ScenarioMetadata& metadata = scenario->getMetadata();
    World world(metadata.requiredWidth, metadata.requiredHeight);

    Config::Nes config = std::get<Config::Nes>(scenario->getConfig());
    config.romPath = romPath.value().string();
    config.frameSkip = 1;
    config.requireSmolnesMapper = true;
    scenario->setConfig(config, world);
    scenario->setup(world);

    ASSERT_TRUE(scenario->isRuntimeRunning()) << scenario->getRuntimeLastError();
    ASSERT_TRUE(scenario->isRuntimeHealthy()) << scenario->getRuntimeLastError();

    constexpr double deltaTime = 1.0 / 60.0;
    for (int frame = 0; frame < 10; ++frame) {
        scenario->tick(world, deltaTime);
    }
    ASSERT_EQ(scenario->getRuntimeRenderedFrameCount(), 10u);
    ASSERT_TRUE(world.getData().scenario_video_frame.has_value());
    EXPECT_EQ(world.getData().scenario_video_frame->frame_id, 10u);

    scenario->reset(world);

    ASSERT_TRUE(scenario->isRuntimeRunning()) << scenario->getRuntimeLastError();
    ASSERT_TRUE(scenario->isRuntimeHealthy()) << scenario->getRuntimeLastError();
    EXPECT_EQ(scenario->getRuntimeRenderedFrameCount(), 0u);
    EXPECT_FALSE(world.getData().scenario_video_frame.has_value());

    scenario->tick(world, deltaTime);
    EXPECT_EQ(scenario->getRuntimeRenderedFrameCount(), 1u);
    ASSERT_TRUE(world.getData().scenario_video_frame.has_value());
    EXPECT_EQ(world.getData().scenario_video_frame->frame_id, 1u);
}
