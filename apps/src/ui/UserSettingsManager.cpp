#include "ui/UserSettingsManager.h"

#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketServiceInterface.h"
#include "server/api/UserSettingsGet.h"
#include "server/api/UserSettingsPatch.h"
#include "server/api/UserSettingsReset.h"
#include "server/api/UserSettingsSet.h"

namespace DirtSim::Ui {

void InMemoryUserSettingsManager::setWebSocketService(Network::WebSocketServiceInterface* wsService)
{
    wsService_ = wsService;
}

void InMemoryUserSettingsManager::syncFromServerOrAssert(int timeoutMs)
{
    DIRTSIM_ASSERT(wsService_ != nullptr, "UserSettingsManager missing WebSocketService");
    DIRTSIM_ASSERT(wsService_->isConnected(), "UserSettingsManager not connected");

    const Api::UserSettingsGet::Command cmd{};
    const auto result =
        wsService_->sendCommandAndGetResponse<Api::UserSettingsGet::Okay>(cmd, timeoutMs);
    DIRTSIM_ASSERT(!result.isError(), "UserSettingsGet failed: " + result.errorValue());
    DIRTSIM_ASSERT(
        !result.value().isError(),
        "UserSettingsGet rejected: " + result.value().errorValue().message);

    settings_ = result.value().value().settings;
}

void InMemoryUserSettingsManager::applyServerUpdate(const DirtSim::UserSettings& settings)
{
    settings_ = settings;
}

void InMemoryUserSettingsManager::patchOrAssert(
    const Api::UserSettingsPatch::Command& patch, int timeoutMs)
{
    DIRTSIM_ASSERT(wsService_ != nullptr, "UserSettingsManager missing WebSocketService");
    DIRTSIM_ASSERT(wsService_->isConnected(), "UserSettingsManager not connected");

    if (patch.clockScenarioConfig.has_value()) {
        settings_.clockScenarioConfig = *patch.clockScenarioConfig;
    }
    if (patch.sandboxScenarioConfig.has_value()) {
        settings_.sandboxScenarioConfig = *patch.sandboxScenarioConfig;
    }
    if (patch.rainingScenarioConfig.has_value()) {
        settings_.rainingScenarioConfig = *patch.rainingScenarioConfig;
    }
    if (patch.treeGerminationScenarioConfig.has_value()) {
        settings_.treeGerminationScenarioConfig = *patch.treeGerminationScenarioConfig;
    }
    if (patch.volumePercent.has_value()) {
        settings_.volumePercent = *patch.volumePercent;
    }
    if (patch.defaultScenario.has_value()) {
        settings_.defaultScenario = *patch.defaultScenario;
    }
    if (patch.startMenuIdleAction.has_value()) {
        settings_.startMenuIdleAction = *patch.startMenuIdleAction;
    }
    if (patch.startMenuIdleTimeoutMs.has_value()) {
        settings_.startMenuIdleTimeoutMs = *patch.startMenuIdleTimeoutMs;
    }
    if (patch.trainingSpec.has_value()) {
        settings_.trainingSpec = *patch.trainingSpec;
    }
    if (patch.evolutionConfig.has_value()) {
        settings_.evolutionConfig = *patch.evolutionConfig;
    }
    if (patch.mutationConfig.has_value()) {
        settings_.mutationConfig = *patch.mutationConfig;
    }
    if (patch.trainingResumePolicy.has_value()) {
        settings_.trainingResumePolicy = *patch.trainingResumePolicy;
    }
    if (patch.uiTraining.has_value()) {
        settings_.uiTraining = *patch.uiTraining;
    }

    const auto result =
        wsService_->sendCommandAndGetResponse<Api::UserSettingsPatch::Okay>(patch, timeoutMs);
    DIRTSIM_ASSERT(!result.isError(), "UserSettingsPatch failed: " + result.errorValue());
    DIRTSIM_ASSERT(
        !result.value().isError(),
        "UserSettingsPatch rejected: " + result.value().errorValue().message);

    settings_ = result.value().value().settings;
}

void InMemoryUserSettingsManager::setOrAssert(const DirtSim::UserSettings& settings, int timeoutMs)
{
    DIRTSIM_ASSERT(wsService_ != nullptr, "UserSettingsManager missing WebSocketService");
    DIRTSIM_ASSERT(wsService_->isConnected(), "UserSettingsManager not connected");

    settings_ = settings;

    const Api::UserSettingsSet::Command cmd{ .settings = settings };
    const auto result =
        wsService_->sendCommandAndGetResponse<Api::UserSettingsSet::Okay>(cmd, timeoutMs);
    DIRTSIM_ASSERT(!result.isError(), "UserSettingsSet failed: " + result.errorValue());
    DIRTSIM_ASSERT(
        !result.value().isError(),
        "UserSettingsSet rejected: " + result.value().errorValue().message);

    settings_ = result.value().value().settings;
}

void InMemoryUserSettingsManager::resetOrAssert(int timeoutMs)
{
    DIRTSIM_ASSERT(wsService_ != nullptr, "UserSettingsManager missing WebSocketService");
    DIRTSIM_ASSERT(wsService_->isConnected(), "UserSettingsManager not connected");

    const Api::UserSettingsReset::Command cmd{};
    const auto result =
        wsService_->sendCommandAndGetResponse<Api::UserSettingsReset::Okay>(cmd, timeoutMs);
    DIRTSIM_ASSERT(!result.isError(), "UserSettingsReset failed: " + result.errorValue());
    DIRTSIM_ASSERT(
        !result.value().isError(),
        "UserSettingsReset rejected: " + result.value().errorValue().message);

    settings_ = result.value().value().settings;
}

} // namespace DirtSim::Ui
