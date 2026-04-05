#pragma once

#include "core/ScenarioId.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/TrainingResumePolicy.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "core/scenarios/ClockConfig.h"
#include "core/scenarios/RainingConfig.h"
#include "core/scenarios/SandboxConfig.h"
#include "core/scenarios/TreeGerminationConfig.h"
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <zpp_bits.h>

namespace DirtSim {

enum class StartMenuIdleAction : uint8_t {
    ClockScenario = 0,
    None,
    TrainingSession,
};

struct UiTrainingConfig {
    int streamIntervalMs = 16;
    bool bestPlaybackEnabled = false;
    int bestPlaybackIntervalMs = 16;
    std::optional<bool> nesControllerOverlayEnabled = std::nullopt;

    using serialize = zpp::bits::members<4>;
};

struct NesSessionSettings {
    bool frameDelayEnabled = false;
    double frameDelayMs = 0.0;

    using serialize = zpp::bits::members<2>;
};

struct SearchSettings {
    static constexpr uint32_t MaxSearchedNodeCountMin = 100;
    static constexpr uint32_t MaxSearchedNodeCountMax = 500000;
    static constexpr uint32_t StallFrameLimitMin = 1;
    static constexpr uint32_t StallFrameLimitMax = 300;

    uint32_t maxSearchedNodeCount = 5000;
    uint32_t stallFrameLimit = 30;
    bool velocityPruningEnabled = true;

    using serialize = zpp::bits::members<3>;
};

struct UserSettings {
    Config::Clock clockScenarioConfig;
    Config::Sandbox sandboxScenarioConfig;
    Config::Raining rainingScenarioConfig;
    Config::TreeGermination treeGerminationScenarioConfig;
    NesSessionSettings nesSessionSettings;
    SearchSettings searchSettings;
    int volumePercent = 20;
    Scenario::EnumType defaultScenario = Scenario::EnumType::Sandbox;
    StartMenuIdleAction startMenuIdleAction = StartMenuIdleAction::ClockScenario;
    int startMenuIdleTimeoutMs = 60000;
    TrainingSpec trainingSpec;
    EvolutionConfig evolutionConfig;
    MutationConfig mutationConfig;
    TrainingResumePolicy trainingResumePolicy = TrainingResumePolicy::WarmFromBest;
    UiTrainingConfig uiTraining;
    bool networkLiveScanPreferred = false;

    using serialize = zpp::bits::members<16>;
};

void from_json(const nlohmann::json& j, UiTrainingConfig& settings);
void to_json(nlohmann::json& j, const UiTrainingConfig& settings);

void from_json(const nlohmann::json& j, NesSessionSettings& settings);
void to_json(nlohmann::json& j, const NesSessionSettings& settings);

void from_json(const nlohmann::json& j, SearchSettings& settings);
void to_json(nlohmann::json& j, const SearchSettings& settings);

void from_json(const nlohmann::json& j, UserSettings& settings);
void to_json(nlohmann::json& j, const UserSettings& settings);

} // namespace DirtSim
