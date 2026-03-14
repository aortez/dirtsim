#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

class FakeSmolnesRuntime : public SmolnesRuntime {
public:
    bool start(const std::string& romPath) override
    {
        running_ = true;
        startedRomPath_ = romPath;
        return true;
    }

    bool runFrames(uint32_t frameCount, uint32_t /*timeoutMs*/) override
    {
        runFramesCalls_++;
        if (failNextRun_) {
            failNextRun_ = false;
            return false;
        }
        renderedFrameCount_ += frameCount;
        return true;
    }

    void stop() override { running_ = false; }

    void setController1State(uint8_t buttonMask) override { lastControllerMask_ = buttonMask; }

    bool isHealthy() const override { return true; }
    bool isRunning() const override { return running_; }
    uint64_t getRenderedFrameCount() const override { return renderedFrameCount_; }

    bool copyLatestFrameInto(ScenarioVideoFrame& frame) const override
    {
        frame.width = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_WIDTH);
        frame.height = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_HEIGHT);
        frame.frame_id = 7u;
        frame.pixels.resize(SMOLNES_RUNTIME_FRAME_BYTES);
        std::fill(frame.pixels.begin(), frame.pixels.end(), std::byte{ 0x55 });
        return true;
    }

    std::optional<NesPaletteFrame> copyLatestPaletteFrame() const override
    {
        NesPaletteFrame frame;
        frame.width = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_WIDTH);
        frame.height = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_HEIGHT);
        frame.frameId = 9u;
        frame.indices.resize(SMOLNES_RUNTIME_PALETTE_FRAME_BYTES, 3u);
        return frame;
    }

    std::optional<MemorySnapshot> copyMemorySnapshot() const override
    {
        MemorySnapshot snapshot;
        snapshot.cpuRam.fill(0u);
        snapshot.prgRam.fill(0u);
        snapshot.cpuRam[0x10] = 0xABu;
        snapshot.prgRam[0x20] = 0xCDu;
        return snapshot;
    }

    std::optional<ProfilingSnapshot> copyProfilingSnapshot() const override
    {
        return ProfilingSnapshot{};
    }

    void setApuSampleCallback(SmolnesApuSampleCallback /*callback*/, void* /*userdata*/) override {}
    void setPacingMode(SmolnesRuntimePacingMode /*mode*/) override {}
    std::string getLastError() const override { return lastError_; }

    const std::string& getStartedRomPath() const { return startedRomPath_; }
    uint8_t getLastControllerMask() const { return lastControllerMask_; }
    int getRunFramesCalls() const { return runFramesCalls_; }
    void failNextRun(std::string lastError)
    {
        failNextRun_ = true;
        lastError_ = lastError;
    }

private:
    bool failNextRun_ = false;
    bool running_ = false;
    int runFramesCalls_ = 0;
    uint8_t lastControllerMask_ = 0;
    uint64_t renderedFrameCount_ = 0;
    std::string lastError_;
    std::string startedRomPath_;
};

std::filesystem::path writeFakeRom()
{
    const std::filesystem::path romPath =
        std::filesystem::path(::testing::TempDir()) / "fake_smolnes_driver_test_rom.nes";
    std::ofstream stream(romPath, std::ios::binary | std::ios::trunc);
    EXPECT_TRUE(stream.is_open());

    const std::array<uint8_t, 16> header = {
        'N', 'E', 'S', 0x1A, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    };
    stream.write(
        reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    EXPECT_TRUE(stream.good());
    return romPath;
}

} // namespace

TEST(NesSmolnesScenarioDriverTest, StepUsesInjectedRuntimeFactory)
{
    FakeSmolnesRuntime* runtime = nullptr;
    const std::filesystem::path romPath = writeFakeRom();

    NesSmolnesScenarioDriver driver(
        Scenario::EnumType::NesSuperMarioBros,
        NesSmolnesScenarioDriver::RuntimeConfig{
            .runtimeFactory =
                [&runtime]() {
                    auto fakeRuntime = std::make_unique<FakeSmolnesRuntime>();
                    runtime = fakeRuntime.get();
                    return fakeRuntime;
                },
        });

    Config::NesSuperMarioBros config = std::get<Config::NesSuperMarioBros>(
        makeDefaultConfig(Scenario::EnumType::NesSuperMarioBros));
    config.romId = "";
    config.romPath = romPath.string();
    ASSERT_FALSE(driver.setConfig(ScenarioConfig{ config }).isError());
    ASSERT_FALSE(driver.setup().isError());
    ASSERT_NE(runtime, nullptr);

    Timers timers;
    const NesSmolnesScenarioDriver::StepResult stepResult = driver.step(timers, 0x93u);

    EXPECT_EQ(runtime->getStartedRomPath(), romPath.string());
    EXPECT_EQ(runtime->getLastControllerMask(), 0x93u);
    EXPECT_EQ(runtime->getRunFramesCalls(), 1);
    EXPECT_TRUE(stepResult.runtimeHealthy);
    EXPECT_TRUE(stepResult.runtimeRunning);
    EXPECT_EQ(stepResult.controllerMask, 0x93u);
    EXPECT_EQ(stepResult.renderedFramesBefore, 0u);
    EXPECT_EQ(stepResult.renderedFramesAfter, 1u);
    EXPECT_EQ(stepResult.advancedFrames, 1u);
    ASSERT_TRUE(stepResult.memorySnapshot.has_value());
    EXPECT_EQ(stepResult.memorySnapshot->cpuRam[0x10], 0xABu);
    EXPECT_EQ(stepResult.memorySnapshot->prgRam[0x20], 0xCDu);
    ASSERT_TRUE(stepResult.paletteFrame.has_value());
    EXPECT_EQ(stepResult.paletteFrame->frameId, 9u);
    ASSERT_TRUE(stepResult.scenarioVideoFrame.has_value());
    EXPECT_EQ(stepResult.scenarioVideoFrame->frame_id, 7u);
}

TEST(NesSmolnesScenarioDriverTest, TickClearsScenarioFrameAfterRuntimeFailure)
{
    FakeSmolnesRuntime* runtime = nullptr;
    const std::filesystem::path romPath = writeFakeRom();

    NesSmolnesScenarioDriver driver(
        Scenario::EnumType::NesSuperMarioBros,
        NesSmolnesScenarioDriver::RuntimeConfig{
            .runtimeFactory =
                [&runtime]() {
                    auto fakeRuntime = std::make_unique<FakeSmolnesRuntime>();
                    runtime = fakeRuntime.get();
                    return fakeRuntime;
                },
        });

    Config::NesSuperMarioBros config = std::get<Config::NesSuperMarioBros>(
        makeDefaultConfig(Scenario::EnumType::NesSuperMarioBros));
    config.romId = "";
    config.romPath = romPath.string();
    ASSERT_FALSE(driver.setConfig(ScenarioConfig{ config }).isError());
    ASSERT_FALSE(driver.setup().isError());
    ASSERT_NE(runtime, nullptr);

    Timers timers;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame;
    driver.tick(timers, scenarioVideoFrame);
    ASSERT_TRUE(scenarioVideoFrame.has_value());

    runtime->failNextRun("Injected run failure");
    driver.tick(timers, scenarioVideoFrame);

    EXPECT_FALSE(scenarioVideoFrame.has_value());
    EXPECT_FALSE(driver.isRuntimeRunning());
}
