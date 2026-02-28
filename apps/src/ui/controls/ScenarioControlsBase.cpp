#include "ScenarioControlsBase.h"
#include "core/Assert.h"
#include "server/api/UserSettingsPatch.h"
#include "ui/UserSettingsManager.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

ScenarioControlsBase::ScenarioControlsBase(
    lv_obj_t* parentContainer,
    Network::WebSocketServiceInterface* wsService,
    UserSettingsManager& userSettingsManager,
    const std::string& scenarioId)
    : parentContainer_(parentContainer),
      wsService_(wsService),
      userSettingsManager_(userSettingsManager),
      scenarioId_(scenarioId)
{
    createContainer();
}

ScenarioControlsBase::~ScenarioControlsBase()
{
    // Delete the container which cascade-deletes all child widgets.
    // This prevents orphaned widgets with dangling callback pointers.
    if (controlsContainer_) {
        lv_obj_del(controlsContainer_);
        controlsContainer_ = nullptr;
    }
    spdlog::info("ScenarioControlsBase: Destroyed controls for '{}'", scenarioId_);
}

void ScenarioControlsBase::createContainer()
{
    // Create a container for all scenario controls.
    // This allows cleanup via single lv_obj_del() in destructor.
    controlsContainer_ = lv_obj_create(parentContainer_);
    lv_obj_remove_style_all(controlsContainer_);
    lv_obj_set_size(controlsContainer_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(controlsContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        controlsContainer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(controlsContainer_, 8, 0);
}

void ScenarioControlsBase::sendConfigUpdate(const ScenarioConfig& config)
{
    Api::UserSettingsPatch::Command patchCmd{};
    if (const auto* clockConfig = std::get_if<Config::Clock>(&config)) {
        patchCmd.clockScenarioConfig = *clockConfig;
    }
    else if (const auto* sandboxConfig = std::get_if<Config::Sandbox>(&config)) {
        patchCmd.sandboxScenarioConfig = *sandboxConfig;
    }
    else if (const auto* rainingConfig = std::get_if<Config::Raining>(&config)) {
        patchCmd.rainingScenarioConfig = *rainingConfig;
    }
    else if (const auto* treeGerminationConfig = std::get_if<Config::TreeGermination>(&config)) {
        patchCmd.treeGerminationScenarioConfig = *treeGerminationConfig;
    }
    else {
        DIRTSIM_ASSERT(
            false,
            "ScenarioControlsBase missing UserSettingsPatch mapping for scenario config type");
    }

    spdlog::info("ScenarioControlsBase: Persisting scenario config update for '{}'", scenarioId_);
    userSettingsManager_.patchOrAssert(patchCmd, 500);
}

} // namespace Ui
} // namespace DirtSim
