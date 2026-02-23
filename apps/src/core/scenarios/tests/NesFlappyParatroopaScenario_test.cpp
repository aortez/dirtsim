#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/scenarios/NesFlappyParatroopaScenario.h"
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
#include <thread>

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

struct ParallelRuntimeResult {
    std::string lastError;
    uint64_t renderedFrames = 0;
    bool healthy = false;
};

ParallelRuntimeResult runScenarioFrames(const std::filesystem::path& romPath, int frameCount)
{
    ParallelRuntimeResult result;

    auto scenario = std::make_unique<NesFlappyParatroopaScenario>();
    const ScenarioMetadata& metadata = scenario->getMetadata();
    World world(metadata.requiredWidth, metadata.requiredHeight);

    Config::NesFlappyParatroopa config =
        std::get<Config::NesFlappyParatroopa>(scenario->getConfig());
    config.romPath = romPath.string();
    config.requireSmolnesMapper = true;
    scenario->setConfig(config, world);
    scenario->setup(world);

    if (!scenario->isRuntimeRunning()) {
        result.lastError = scenario->getRuntimeLastError();
        return result;
    }
    if (!scenario->isRuntimeHealthy()) {
        result.lastError = scenario->getRuntimeLastError();
        return result;
    }

    constexpr double deltaTime = 1.0 / 60.0;
    for (int frame = 0; frame < frameCount; ++frame) {
        scenario->tick(world, deltaTime);
    }

    result.healthy = scenario->isRuntimeHealthy();
    result.renderedFrames = scenario->getRuntimeRenderedFrameCount();
    if (!result.healthy) {
        result.lastError = scenario->getRuntimeLastError();
    }
    return result;
}

} // namespace

TEST(NesFlappyParatroopaScenarioTest, InspectRomAcceptsMapperZero)
{
    const std::filesystem::path romPath =
        std::filesystem::path(::testing::TempDir()) / "nes_mapper0_fixture.nes";
    writeRomHeader(
        romPath, { 'N', 'E', 'S', 0x1A, 0x02, 0x01, 0x01, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 });

    const NesRomCheckResult result = NesFlappyParatroopaScenario::inspectRom(romPath);

    EXPECT_EQ(result.status, NesRomCheckStatus::Compatible);
    EXPECT_TRUE(result.isCompatible());
    EXPECT_EQ(result.mapper, 0u);
    EXPECT_EQ(result.prgBanks16k, 2u);
    EXPECT_EQ(result.chrBanks8k, 1u);
}

TEST(NesFlappyParatroopaScenarioTest, InspectRomRejectsUnsupportedMapper)
{
    const std::filesystem::path romPath =
        std::filesystem::path(::testing::TempDir()) / "nes_mapper30_fixture.nes";
    writeRomHeader(
        romPath, { 'N', 'E', 'S', 0x1A, 0x20, 0x00, 0xE3, 0x10, 0, 0, 0, 0, 0, 0, 0, 0 });

    const NesRomCheckResult result = NesFlappyParatroopaScenario::inspectRom(romPath);

    EXPECT_EQ(result.status, NesRomCheckStatus::UnsupportedMapper);
    EXPECT_FALSE(result.isCompatible());
    EXPECT_EQ(result.mapper, 30u);
}

TEST(NesFlappyParatroopaScenarioTest, InspectRomRejectsInvalidHeader)
{
    const std::filesystem::path romPath =
        std::filesystem::path(::testing::TempDir()) / "nes_invalid_header_fixture.nes";
    writeRomHeader(
        romPath, { 'B', 'A', 'D', 0x1A, 0x02, 0x01, 0x01, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 });

    const NesRomCheckResult result = NesFlappyParatroopaScenario::inspectRom(romPath);

    EXPECT_EQ(result.status, NesRomCheckStatus::InvalidHeader);
    EXPECT_FALSE(result.isCompatible());
}

TEST(NesFlappyParatroopaScenarioTest, ValidateConfigResolvesRomIdFromCatalog)
{
    const std::filesystem::path romDir =
        std::filesystem::path(::testing::TempDir()) / "nes_catalog_valid";
    std::filesystem::create_directories(romDir);
    const std::filesystem::path romPath = romDir / "Flappy.Paratroopa.World.Unl.nes";
    writeRomHeader(
        romPath, { 'N', 'E', 'S', 0x1A, 0x02, 0x01, 0x01, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 });

    Config::NesFlappyParatroopa config{};
    config.romPath = "";
    config.romId = "flappy-paratroopa-world-unl";
    config.romDirectory = romDir.string();

    const NesConfigValidationResult validation =
        NesFlappyParatroopaScenario::validateConfig(config);
    EXPECT_TRUE(validation.valid);
    EXPECT_EQ(validation.resolvedRomPath, romPath);
    EXPECT_EQ(validation.resolvedRomId, "flappy-paratroopa-world-unl");
    EXPECT_TRUE(validation.romCheck.isCompatible());
}

TEST(NesFlappyParatroopaScenarioTest, ValidateConfigRejectsUnknownRomId)
{
    const std::filesystem::path romDir =
        std::filesystem::path(::testing::TempDir()) / "nes_catalog_missing";
    std::filesystem::create_directories(romDir);

    Config::NesFlappyParatroopa config{};
    config.romPath = "";
    config.romId = "missing-rom";
    config.romDirectory = romDir.string();

    const NesConfigValidationResult validation =
        NesFlappyParatroopaScenario::validateConfig(config);
    EXPECT_FALSE(validation.valid);
    EXPECT_EQ(validation.romCheck.status, NesRomCheckStatus::FileNotFound);
    EXPECT_NE(validation.message.find("No ROM found"), std::string::npos);
}

TEST(NesFlappyParatroopaScenarioTest, ValidateConfigFallsBackToRomPathWhenCatalogLookupMisses)
{
    const std::filesystem::path romPath =
        std::filesystem::path(::testing::TempDir()) / "Flappy.Paratroopa.World.Unl.nes";
    writeRomHeader(
        romPath, { 'N', 'E', 'S', 0x1A, 0x02, 0x01, 0x01, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 });

    Config::NesFlappyParatroopa config{};
    config.romId = "flappy-paratroopa-world-unl";
    config.romDirectory =
        (std::filesystem::path(::testing::TempDir()) / "missing_rom_dir").string();
    config.romPath = romPath.string();

    const NesConfigValidationResult validation =
        NesFlappyParatroopaScenario::validateConfig(config);
    EXPECT_TRUE(validation.valid);
    EXPECT_EQ(validation.resolvedRomPath, romPath);
    EXPECT_EQ(validation.resolvedRomId, "flappy-paratroopa-world-unl");
    EXPECT_TRUE(validation.romCheck.isCompatible());
}

TEST(NesFlappyParatroopaScenarioTest, ScenarioConfigMapsToNesEnum)
{
    const ScenarioConfig config = makeDefaultConfig(Scenario::EnumType::NesFlappyParatroopa);
    ASSERT_TRUE(std::holds_alternative<Config::NesFlappyParatroopa>(config));
    EXPECT_EQ(getScenarioId(config), Scenario::EnumType::NesFlappyParatroopa);
}

TEST(NesFlappyParatroopaScenarioTest, ScenarioRegistryRegistersNesFlappyParatroopaScenario)
{
    GenomeRepository genomeRepository;
    const ScenarioRegistry registry = ScenarioRegistry::createDefault(genomeRepository);

    const auto ids = registry.getScenarioIds();
    EXPECT_NE(
        std::find(ids.begin(), ids.end(), Scenario::EnumType::NesFlappyParatroopa), ids.end());

    const ScenarioMetadata* metadata =
        registry.getMetadata(Scenario::EnumType::NesFlappyParatroopa);
    ASSERT_NE(metadata, nullptr);
    EXPECT_EQ(metadata->name, "NES Flappy Paratroopa");

    std::unique_ptr<ScenarioRunner> scenario =
        registry.createScenario(Scenario::EnumType::NesFlappyParatroopa);
    ASSERT_NE(scenario, nullptr);
    EXPECT_TRUE(std::holds_alternative<Config::NesFlappyParatroopa>(scenario->getConfig()));
}

TEST(NesFlappyParatroopaScenarioTest, FlappyParatroopaRomLoadsAndTicks100Frames)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    auto scenario = std::make_unique<NesFlappyParatroopaScenario>();
    const ScenarioMetadata& metadata = scenario->getMetadata();
    World world(metadata.requiredWidth, metadata.requiredHeight);

    Config::NesFlappyParatroopa config =
        std::get<Config::NesFlappyParatroopa>(scenario->getConfig());
    config.romPath = romPath.value().string();
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

TEST(NesFlappyParatroopaScenarioTest, ResetRestartsRuntimeFrameCounter)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    auto scenario = std::make_unique<NesFlappyParatroopaScenario>();
    const ScenarioMetadata& metadata = scenario->getMetadata();
    World world(metadata.requiredWidth, metadata.requiredHeight);

    Config::NesFlappyParatroopa config =
        std::get<Config::NesFlappyParatroopa>(scenario->getConfig());
    config.romPath = romPath.value().string();
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

TEST(NesFlappyParatroopaScenarioTest, RuntimeMemorySnapshotExposesCpuAndPrgRam)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    auto scenario = std::make_unique<NesFlappyParatroopaScenario>();
    const ScenarioMetadata& metadata = scenario->getMetadata();
    World world(metadata.requiredWidth, metadata.requiredHeight);

    Config::NesFlappyParatroopa config =
        std::get<Config::NesFlappyParatroopa>(scenario->getConfig());
    config.romPath = romPath.value().string();
    config.requireSmolnesMapper = true;
    scenario->setConfig(config, world);
    scenario->setup(world);

    ASSERT_TRUE(scenario->isRuntimeRunning()) << scenario->getRuntimeLastError();
    ASSERT_TRUE(scenario->isRuntimeHealthy()) << scenario->getRuntimeLastError();

    constexpr double deltaTime = 1.0 / 60.0;
    for (int frame = 0; frame < 8; ++frame) {
        scenario->setController1State(SMOLNES_RUNTIME_BUTTON_START);
        scenario->tick(world, deltaTime);
    }

    const auto firstSnapshot = scenario->copyRuntimeMemorySnapshot();
    ASSERT_TRUE(firstSnapshot.has_value());
    EXPECT_EQ(firstSnapshot->cpuRam.size(), static_cast<size_t>(SMOLNES_RUNTIME_CPU_RAM_BYTES));
    EXPECT_EQ(firstSnapshot->prgRam.size(), static_cast<size_t>(SMOLNES_RUNTIME_PRG_RAM_BYTES));

    scenario->tick(world, deltaTime);
    const auto secondSnapshot = scenario->copyRuntimeMemorySnapshot();
    ASSERT_TRUE(secondSnapshot.has_value());

    bool cpuChanged = false;
    for (size_t i = 0; i < firstSnapshot->cpuRam.size(); ++i) {
        if (firstSnapshot->cpuRam[i] != secondSnapshot->cpuRam[i]) {
            cpuChanged = true;
            break;
        }
    }
    EXPECT_TRUE(cpuChanged) << "CPU RAM should change after advancing a frame.";
}

TEST(NesFlappyParatroopaScenarioTest, ParallelRuntimeInstancesCanAdvanceIndependently)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    constexpr int frameCount = 90;
    ParallelRuntimeResult firstResult;
    ParallelRuntimeResult secondResult;
    const std::filesystem::path resolvedRomPath = romPath.value();

    std::thread firstThread([&firstResult, &resolvedRomPath, frameCount]() {
        firstResult = runScenarioFrames(resolvedRomPath, frameCount);
    });
    std::thread secondThread([&secondResult, &resolvedRomPath, frameCount]() {
        secondResult = runScenarioFrames(resolvedRomPath, frameCount);
    });
    firstThread.join();
    secondThread.join();

    EXPECT_TRUE(firstResult.lastError.empty()) << firstResult.lastError;
    EXPECT_TRUE(secondResult.lastError.empty()) << secondResult.lastError;
    EXPECT_TRUE(firstResult.healthy) << firstResult.lastError;
    EXPECT_TRUE(secondResult.healthy) << secondResult.lastError;
    EXPECT_EQ(firstResult.renderedFrames, static_cast<uint64_t>(frameCount));
    EXPECT_EQ(secondResult.renderedFrames, static_cast<uint64_t>(frameCount));
}
