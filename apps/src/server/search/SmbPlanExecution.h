#pragma once

#include "core/Result.h"
#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/WorldData.h"
#include "core/scenarios/nes/NesFitnessDetails.h"
#include "core/scenarios/nes/NesGameAdapter.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include "server/api/Plan.h"
#include "server/api/SearchProgress.h"
#include <memory>
#include <optional>
#include <string>

namespace DirtSim::Server::SearchSupport {

struct SmbPlanExecutionTickResult {
    bool completed = false;
    bool frameAdvanced = false;
    std::optional<std::string> error = std::nullopt;
};

class SmbPlanExecution {
public:
    Result<std::monostate, std::string> startHoldRight();
    Result<std::monostate, std::string> startPlayback(const Api::Plan& plan);

    SmbPlanExecutionTickResult tick();

    void pauseSet(bool paused);
    void stop();

    bool isCompleted() const;
    bool isPaused() const;
    bool hasRenderableFrame() const;

    const Api::Plan& getPlan() const;
    const Api::SearchProgress& getProgress() const;
    const std::optional<ScenarioVideoFrame>& getScenarioVideoFrame() const;
    const WorldData& getWorldData() const;

private:
    enum class Mode : uint8_t {
        HoldRightSearch = 0,
        PlanPlayback = 1,
    };

    static uint64_t encodeFrontier(const NesSuperMarioBrosFitnessSnapshot& snapshot);
    static PlayerControlFrame holdRightFrame();
    static uint8_t playerControlFrameToNesMask(const PlayerControlFrame& frame);

    Result<std::monostate, std::string> startCommon();
    void updateProgress(const NesFitnessDetails& fitnessDetails);

    std::unique_ptr<NesSmolnesScenarioDriver> driver_;
    std::unique_ptr<NesGameAdapter> gameAdapter_;
    Timers timers_;
    WorldData worldData_;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame_ = std::nullopt;
    std::optional<uint8_t> lastGameState_ = std::nullopt;
    Api::Plan plan_;
    Api::SearchProgress progress_;
    size_t playbackFrameIndex_ = 0;
    bool completed_ = false;
    bool paused_ = false;
    Mode mode_ = Mode::HoldRightSearch;
};

} // namespace DirtSim::Server::SearchSupport
