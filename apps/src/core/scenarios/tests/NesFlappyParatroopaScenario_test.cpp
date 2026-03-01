#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "core/scenarios/nes/NesRomValidation.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
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

    NesSmolnesScenarioDriver driver(Scenario::EnumType::NesFlappyParatroopa);
    Config::NesFlappyParatroopa config = std::get<Config::NesFlappyParatroopa>(
        makeDefaultConfig(Scenario::EnumType::NesFlappyParatroopa));
    config.romPath = romPath.string();
    config.requireSmolnesMapper = true;

    const auto setResult = driver.setConfig(ScenarioConfig{ config });
    if (setResult.isError()) {
        result.lastError = setResult.errorValue();
        return result;
    }

    const auto setupResult = driver.setup();
    if (setupResult.isError()) {
        result.lastError = setupResult.errorValue();
        return result;
    }

    Timers timers;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame;
    for (int frame = 0; frame < frameCount; ++frame) {
        driver.tick(timers, scenarioVideoFrame);
    }

    result.healthy = driver.isRuntimeHealthy();
    result.renderedFrames = driver.getRuntimeRenderedFrameCount();
    if (!result.healthy) {
        result.lastError = driver.getRuntimeLastError();
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

    const NesRomCheckResult result = inspectNesRom(romPath);

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

    const NesRomCheckResult result = inspectNesRom(romPath);

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

    const NesRomCheckResult result = inspectNesRom(romPath);

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
        validateNesRomSelection(config.romId, config.romDirectory, config.romPath);
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
        validateNesRomSelection(config.romId, config.romDirectory, config.romPath);
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
        validateNesRomSelection(config.romId, config.romDirectory, config.romPath);
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
    EXPECT_EQ(metadata->kind, ScenarioKind::NesWorld);
    EXPECT_EQ(metadata->name, "NES Flappy Paratroopa");

    std::unique_ptr<ScenarioRunner> scenario =
        registry.createScenario(Scenario::EnumType::NesFlappyParatroopa);
    EXPECT_EQ(scenario, nullptr);
}

TEST(NesFlappyParatroopaScenarioTest, FlappyParatroopaRomLoadsAndTicks100Frames)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    NesSmolnesScenarioDriver driver(Scenario::EnumType::NesFlappyParatroopa);
    Config::NesFlappyParatroopa config = std::get<Config::NesFlappyParatroopa>(
        makeDefaultConfig(Scenario::EnumType::NesFlappyParatroopa));
    config.romPath = romPath.value().string();
    config.requireSmolnesMapper = true;

    const auto setResult = driver.setConfig(ScenarioConfig{ config });
    ASSERT_TRUE(setResult.isValue()) << setResult.errorValue();
    const auto setupResult = driver.setup();
    ASSERT_TRUE(setupResult.isValue()) << setupResult.errorValue();

    const NesRomCheckResult& romCheck = driver.getLastRomCheck();
    ASSERT_TRUE(romCheck.isCompatible()) << "ROM compatibility check failed: " << romCheck.message
                                         << " (mapper=" << romCheck.mapper << ")";
    ASSERT_TRUE(driver.isRuntimeRunning()) << driver.getRuntimeLastError();
    ASSERT_TRUE(driver.isRuntimeHealthy()) << driver.getRuntimeLastError();

    Timers timers;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame;
    constexpr int frameCount = 100;
    for (int frame = 0; frame < frameCount; ++frame) {
        driver.tick(timers, scenarioVideoFrame);
    }

    EXPECT_TRUE(driver.isRuntimeHealthy()) << driver.getRuntimeLastError();
    EXPECT_EQ(driver.getRuntimeRenderedFrameCount(), static_cast<uint64_t>(frameCount));

    ASSERT_TRUE(scenarioVideoFrame.has_value());
    const ScenarioVideoFrame& videoFrame = scenarioVideoFrame.value();
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

    NesSmolnesScenarioDriver driver(Scenario::EnumType::NesFlappyParatroopa);
    Config::NesFlappyParatroopa config = std::get<Config::NesFlappyParatroopa>(
        makeDefaultConfig(Scenario::EnumType::NesFlappyParatroopa));
    config.romPath = romPath.value().string();
    config.requireSmolnesMapper = true;

    const auto setResult = driver.setConfig(ScenarioConfig{ config });
    ASSERT_TRUE(setResult.isValue()) << setResult.errorValue();
    const auto setupResult = driver.setup();
    ASSERT_TRUE(setupResult.isValue()) << setupResult.errorValue();

    ASSERT_TRUE(driver.isRuntimeRunning()) << driver.getRuntimeLastError();
    ASSERT_TRUE(driver.isRuntimeHealthy()) << driver.getRuntimeLastError();

    Timers timers;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame;
    constexpr double deltaTime = 1.0 / 60.0;
    (void)deltaTime;
    for (int frame = 0; frame < 10; ++frame) {
        driver.tick(timers, scenarioVideoFrame);
    }
    ASSERT_EQ(driver.getRuntimeRenderedFrameCount(), 10u);
    ASSERT_TRUE(scenarioVideoFrame.has_value());
    EXPECT_EQ(scenarioVideoFrame->frame_id, 10u);

    const auto resetResult = driver.reset();
    ASSERT_TRUE(resetResult.isValue()) << resetResult.errorValue();
    scenarioVideoFrame.reset();

    ASSERT_TRUE(driver.isRuntimeRunning()) << driver.getRuntimeLastError();
    ASSERT_TRUE(driver.isRuntimeHealthy()) << driver.getRuntimeLastError();
    EXPECT_EQ(driver.getRuntimeRenderedFrameCount(), 0u);

    driver.tick(timers, scenarioVideoFrame);
    EXPECT_EQ(driver.getRuntimeRenderedFrameCount(), 1u);
    ASSERT_TRUE(scenarioVideoFrame.has_value());
    EXPECT_EQ(scenarioVideoFrame->frame_id, 1u);
}

TEST(NesFlappyParatroopaScenarioTest, RuntimeMemorySnapshotExposesCpuAndPrgRam)
{
    const std::optional<std::filesystem::path> romPath = resolveNesFixtureRomPath();
    if (!romPath.has_value()) {
        GTEST_SKIP() << "ROM fixture missing. Run 'cd apps && make fetch-nes-test-rom' or set "
                        "DIRTSIM_NES_TEST_ROM_PATH.";
    }

    NesSmolnesScenarioDriver driver(Scenario::EnumType::NesFlappyParatroopa);
    Config::NesFlappyParatroopa config = std::get<Config::NesFlappyParatroopa>(
        makeDefaultConfig(Scenario::EnumType::NesFlappyParatroopa));
    config.romPath = romPath.value().string();
    config.requireSmolnesMapper = true;

    const auto setResult = driver.setConfig(ScenarioConfig{ config });
    ASSERT_TRUE(setResult.isValue()) << setResult.errorValue();
    const auto setupResult = driver.setup();
    ASSERT_TRUE(setupResult.isValue()) << setupResult.errorValue();

    ASSERT_TRUE(driver.isRuntimeRunning()) << driver.getRuntimeLastError();
    ASSERT_TRUE(driver.isRuntimeHealthy()) << driver.getRuntimeLastError();

    Timers timers;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame;
    constexpr double deltaTime = 1.0 / 60.0;
    (void)deltaTime;
    for (int frame = 0; frame < 8; ++frame) {
        driver.setController1State(SMOLNES_RUNTIME_BUTTON_START);
        driver.tick(timers, scenarioVideoFrame);
    }

    const auto firstSnapshot = driver.copyRuntimeMemorySnapshot();
    ASSERT_TRUE(firstSnapshot.has_value());
    EXPECT_EQ(firstSnapshot->cpuRam.size(), static_cast<size_t>(SMOLNES_RUNTIME_CPU_RAM_BYTES));
    EXPECT_EQ(firstSnapshot->prgRam.size(), static_cast<size_t>(SMOLNES_RUNTIME_PRG_RAM_BYTES));

    driver.tick(timers, scenarioVideoFrame);
    const auto secondSnapshot = driver.copyRuntimeMemorySnapshot();
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
