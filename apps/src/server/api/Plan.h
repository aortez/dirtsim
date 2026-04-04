#pragma once

#include "ApiError.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/UUID.h"
#include "core/input/PlayerControlFrame.h"
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

struct PlanSummary {
    UUID id{};
    uint64_t bestFrontier = 0;
    uint64_t elapsedFrames = 0;

    using serialize = zpp::bits::members<3>;
};

struct SmbPlaybackRoot {
    uint64_t savestateFrameId = 0;
    std::vector<uint8_t> savestateBytes;
    uint64_t bestFrontier = 0;
    uint64_t gameplayFrames = 0;
    uint64_t gameplayFramesSinceProgress = 0;
    double distanceRewardTotal = 0.0;
    double evaluationScore = 0.0;
    double levelClearRewardTotal = 0.0;
    std::optional<uint8_t> gameState = std::nullopt;
    bool terminal = false;

    using serialize = zpp::bits::members<10>;
};

struct Plan {
    PlanSummary summary;
    std::vector<PlayerControlFrame> frames;
    std::optional<SmbPlaybackRoot> smbPlaybackRoot = std::nullopt;

    static constexpr const char* name() { return "Plan"; }
    using serialize = zpp::bits::members<3>;

    using OkayType = std::monostate;
    using Response = Result<OkayType, ApiError>;
    using Cwc = CommandWithCallback<Plan, Response>;
};

void to_json(nlohmann::json& j, const PlanSummary& value);
void from_json(const nlohmann::json& j, PlanSummary& value);
void to_json(nlohmann::json& j, const SmbPlaybackRoot& value);
void from_json(const nlohmann::json& j, SmbPlaybackRoot& value);
void to_json(nlohmann::json& j, const Plan& value);
void from_json(const nlohmann::json& j, Plan& value);

} // namespace Api
} // namespace DirtSim
