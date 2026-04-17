#pragma once

#include "core/RenderMessage.h"
#include "core/Result.h"
#include "core/scenarios/nes/SmolnesRuntime.h"
#include "server/search/SmbSearchCore.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace DirtSim::Server::SearchSupport {

enum class SmbSearchRootFixtureId : uint8_t {
    FirstGap = 0,
    FirstGoomba = 1,
    FlatGroundSanity = 2,
};

struct SmbSearchReplayResult {
    SmbSearchEvaluatorSummary evaluatorSummary;
    std::optional<SmolnesRuntime::MemorySnapshot> memorySnapshot = std::nullopt;
    std::optional<SmolnesRuntime::Savestate> savestate = std::nullopt;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame = std::nullopt;
};

struct SmbSearchRootFixture {
    SmbSearchRootFixtureId id = SmbSearchRootFixtureId::FlatGroundSanity;
    SmbSearchEvaluatorSummary evaluatorSummary;
    std::optional<SmolnesRuntime::MemorySnapshot> memorySnapshot = std::nullopt;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame = std::nullopt;
    SmolnesRuntime::Savestate savestate;
    std::string name;
};

class SmbSearchHarness {
public:
    Result<SmbSearchRootFixture, std::string> captureGameplayRoot(
        uint64_t targetGameplayFrames, const std::string& name) const;
    Result<SmbSearchRootFixture, std::string> captureFixture(
        SmbSearchRootFixtureId fixtureId) const;
    Result<SmbSearchReplayResult, std::string> replayFromRoot(
        const SmolnesRuntime::Savestate& rootSavestate,
        const SmbSearchEvaluatorSummary& rootSummary,
        const std::vector<PlayerControlFrame>& frames) const;
    Result<SmbSearchReplayResult, std::string> replayFromRoot(
        const SmolnesRuntime::Savestate& rootSavestate,
        const SmbSearchEvaluatorSummary& rootSummary,
        const std::vector<SmbSearchLegalAction>& actions) const;
};

std::string toString(SmbSearchRootFixtureId fixtureId);

} // namespace DirtSim::Server::SearchSupport
