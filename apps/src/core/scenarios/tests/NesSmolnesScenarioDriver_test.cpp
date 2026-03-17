#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>

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
        controllerSnapshot_.latestFrameId = renderedFrameCount_;
        if (controllerSnapshot_.controller1SequenceId != latchedControllerSequenceId_) {
            latchedControllerSequenceId_ = controllerSnapshot_.controller1SequenceId;
            latchedControllerAppliedFrameId_ = renderedFrameCount_;
            latchedControllerLatchTimestampNs_ = 2000u + renderedFrameCount_;
        }
        controllerSnapshot_.controller1AppliedFrameId = latchedControllerAppliedFrameId_;
        controllerSnapshot_.controller1LatchTimestampNs = latchedControllerLatchTimestampNs_;
        return true;
    }

    void stop() override { running_ = false; }

    void setController1State(uint8_t buttonMask) override
    {
        if (buttonMask == lastControllerMask_) {
            return;
        }
        lastControllerMask_ = buttonMask;
        controllerSnapshot_.controller1State = buttonMask;
        controllerSnapshot_.controller1RequestTimestampNs = 1000u + nextControllerSequenceId_;
        controllerSnapshot_.controller1SequenceId = nextControllerSequenceId_++;
    }

    bool isHealthy() const override { return true; }
    bool isRunning() const override { return running_; }
    uint64_t getRenderedFrameCount() const override { return renderedFrameCount_; }

    bool copyLatestFrameInto(ScenarioVideoFrame& frame) const override
    {
        frame.width = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_WIDTH);
        frame.height = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_HEIGHT);
        frame.frame_id = renderedFrameCount_;
        frame.pixels.resize(SMOLNES_RUNTIME_FRAME_BYTES);
        std::fill(frame.pixels.begin(), frame.pixels.end(), std::byte{ 0x55 });
        return true;
    }

    std::optional<NesPaletteFrame> copyLatestPaletteFrame() const override
    {
        NesPaletteFrame frame;
        frame.width = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_WIDTH);
        frame.height = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_HEIGHT);
        frame.frameId = renderedFrameCount_;
        frame.indices.resize(SMOLNES_RUNTIME_PALETTE_FRAME_BYTES, 3u);
        return frame;
    }

    std::optional<MemorySnapshot> copyMemorySnapshot() const override
    {
        const auto it = frameMemorySnapshots_.find(renderedFrameCount_);
        if (it != frameMemorySnapshots_.end()) {
            MemorySnapshot snapshot = it->second;
            snapshot.frameId = renderedFrameCount_;
            return snapshot;
        }
        MemorySnapshot snapshot = defaultMemorySnapshot_;
        snapshot.frameId = renderedFrameCount_;
        return snapshot;
    }

    std::optional<ProfilingSnapshot> copyProfilingSnapshot() const override
    {
        return ProfilingSnapshot{};
    }

    std::optional<ControllerSnapshot> copyControllerSnapshot() const override
    {
        return controllerSnapshot_;
    }

    std::optional<LiveSnapshot> copyLiveSnapshot() const override
    {
        auto scenarioVideoFrame = copyLatestFrame();
        auto paletteFrame = copyLatestPaletteFrame();
        auto memorySnapshot = copyMemorySnapshot();
        auto controllerSnapshot = copyControllerSnapshot();
        if (!scenarioVideoFrame.has_value() || !paletteFrame.has_value()
            || !memorySnapshot.has_value()) {
            return std::nullopt;
        }

        return LiveSnapshot{
            .controllerSnapshot = controllerSnapshot,
            .memorySnapshot = memorySnapshot.value(),
            .paletteFrame = paletteFrame.value(),
            .videoFrame = scenarioVideoFrame.value(),
        };
    }

    void setApuSampleCallback(SmolnesApuSampleCallback /*callback*/, void* /*userdata*/) override {}
    void setPacingMode(SmolnesRuntimePacingMode /*mode*/) override {}
    std::string getLastError() const override { return lastError_; }

    const std::string& getStartedRomPath() const { return startedRomPath_; }
    uint8_t getLastControllerMask() const { return lastControllerMask_; }
    int getRunFramesCalls() const { return runFramesCalls_; }
    void setDefaultMemorySnapshot(const MemorySnapshot& snapshot)
    {
        defaultMemorySnapshot_ = snapshot;
    }
    void setFrameMemorySnapshot(uint64_t frameId, const MemorySnapshot& snapshot)
    {
        frameMemorySnapshots_[frameId] = snapshot;
    }
    void failNextRun(std::string lastError)
    {
        failNextRun_ = true;
        lastError_ = lastError;
    }

private:
    static MemorySnapshot makeDefaultMemorySnapshot()
    {
        MemorySnapshot snapshot;
        snapshot.cpuRam.fill(0u);
        snapshot.prgRam.fill(0u);
        snapshot.cpuRam[0x10] = 0xABu;
        snapshot.prgRam[0x20] = 0xCDu;
        return snapshot;
    }

    bool failNextRun_ = false;
    MemorySnapshot defaultMemorySnapshot_ = makeDefaultMemorySnapshot();
    std::map<uint64_t, MemorySnapshot> frameMemorySnapshots_;
    bool running_ = false;
    int runFramesCalls_ = 0;
    uint8_t lastControllerMask_ = 0;
    uint64_t latchedControllerAppliedFrameId_ = 0;
    uint64_t latchedControllerLatchTimestampNs_ = 0;
    uint64_t latchedControllerSequenceId_ = 0;
    uint64_t nextControllerSequenceId_ = 1;
    uint64_t renderedFrameCount_ = 0;
    ControllerSnapshot controllerSnapshot_{};
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

SmolnesRuntime::MemorySnapshot makeSmbProbeSnapshot(
    uint8_t playerState,
    uint8_t facingDirection,
    uint8_t movementDirection,
    uint8_t playerXPage,
    uint8_t playerXScreen,
    uint8_t horizontalSpeed,
    uint8_t verticalSpeed,
    uint8_t playerYScreen)
{
    SmolnesRuntime::MemorySnapshot snapshot;
    snapshot.cpuRam.fill(0u);
    snapshot.prgRam.fill(0u);
    snapshot.cpuRam[0x000E] = playerState;
    snapshot.cpuRam[0x0033] = facingDirection;
    snapshot.cpuRam[0x0045] = movementDirection;
    snapshot.cpuRam[0x0057] = horizontalSpeed;
    snapshot.cpuRam[0x006D] = playerXPage;
    snapshot.cpuRam[0x0086] = playerXScreen;
    snapshot.cpuRam[0x009F] = verticalSpeed;
    snapshot.cpuRam[0x00CE] = playerYScreen;
    snapshot.cpuRam[0x0714] = 0u;
    snapshot.cpuRam[0x0756] = 0u;
    snapshot.cpuRam[0x075A] = 3u;
    snapshot.cpuRam[0x075F] = 1u;
    snapshot.cpuRam[0x0760] = 1u;
    snapshot.cpuRam[0x0770] = 1u;
    snapshot.cpuRam[0x10] = 0xABu;
    snapshot.prgRam[0x20] = 0xCDu;
    return snapshot;
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
    EXPECT_EQ(stepResult.paletteFrame->frameId, 1u);
    ASSERT_TRUE(stepResult.scenarioVideoFrame.has_value());
    EXPECT_EQ(stepResult.scenarioVideoFrame->frame_id, 1u);
    ASSERT_TRUE(stepResult.controllerTelemetry.has_value());
    EXPECT_EQ(
        stepResult.controllerTelemetry->controllerSource,
        NesGameAdapterControllerSource::LiveInput);
    EXPECT_EQ(stepResult.controllerTelemetry->resolvedControllerMask, 0x93u);
    EXPECT_EQ(stepResult.controllerTelemetry->controllerAppliedFrameId, 1u);
    EXPECT_EQ(stepResult.controllerTelemetry->controllerSequenceId, 1u);
    const auto lastControllerTelemetry = driver.getLastControllerTelemetry();
    ASSERT_TRUE(lastControllerTelemetry.has_value());
    EXPECT_EQ(lastControllerTelemetry->controllerSequenceId, 1u);
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

TEST(NesSmolnesScenarioDriverTest, StepPreservesLiveInputOriginAcrossHeldFrames)
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
    const auto firstStep = driver.step(timers, SMOLNES_RUNTIME_BUTTON_RIGHT);
    ASSERT_TRUE(firstStep.controllerTelemetry.has_value());
    EXPECT_EQ(firstStep.controllerTelemetry->controllerSequenceId, 1u);
    EXPECT_EQ(firstStep.controllerTelemetry->controllerAppliedFrameId, 1u);
    EXPECT_EQ(firstStep.controllerTelemetry->controllerLatchTimestampNs, 2001u);

    const auto secondStep = driver.step(timers, SMOLNES_RUNTIME_BUTTON_RIGHT);
    ASSERT_TRUE(secondStep.controllerTelemetry.has_value());
    EXPECT_EQ(secondStep.controllerTelemetry->controllerSequenceId, 1u);
    EXPECT_EQ(secondStep.controllerTelemetry->controllerAppliedFrameId, 1u);
    EXPECT_EQ(secondStep.controllerTelemetry->controllerLatchTimestampNs, 2001u);
}

TEST(NesSmolnesScenarioDriverTest, StepCapturesSmbResponseTelemetryWhenProbeEnabled)
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

    runtime->setDefaultMemorySnapshot(
        makeSmbProbeSnapshot(0x08u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u));
    runtime->setFrameMemorySnapshot(
        301u, makeSmbProbeSnapshot(0x08u, 1u, 1u, 0x00u, 0x21u, 18u, 0u, 120u));
    driver.setSmbResponseProbeEnabled(true);

    Timers timers;
    for (uint64_t frameId = 0; frameId < 300u; ++frameId) {
        const auto warmupStep = driver.step(timers, 0u);
        ASSERT_TRUE(warmupStep.runtimeHealthy);
        ASSERT_TRUE(warmupStep.runtimeRunning);
    }

    const NesSmolnesScenarioDriver::StepResult stepResult =
        driver.step(timers, SMOLNES_RUNTIME_BUTTON_RIGHT);

    ASSERT_TRUE(stepResult.smbResponseTelemetry.has_value());
    EXPECT_EQ(stepResult.smbResponseTelemetry->kind, NesSuperMarioBrosResponseKind::MoveRight);
    EXPECT_EQ(
        stepResult.smbResponseTelemetry->context, NesSuperMarioBrosResponseContext::GroundedStart);
    EXPECT_EQ(
        stepResult.smbResponseTelemetry->milestone, NesSuperMarioBrosResponseMilestone::Motion);
    EXPECT_EQ(stepResult.smbResponseTelemetry->controllerSequenceId, 1u);
    EXPECT_EQ(stepResult.smbResponseTelemetry->controllerAppliedFrameId, 301u);
    EXPECT_EQ(stepResult.smbResponseTelemetry->responseFrameId, 301u);
    const auto lastSmbResponseTelemetry = driver.getLastSmbResponseTelemetry();
    ASSERT_TRUE(lastSmbResponseTelemetry.has_value());
    EXPECT_EQ(lastSmbResponseTelemetry->responseFrameId, 301u);
}
