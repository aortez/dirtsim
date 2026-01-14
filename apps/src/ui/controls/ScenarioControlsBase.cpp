#include "ScenarioControlsBase.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/ScenarioConfigSet.h"
#include <atomic>
#include <chrono>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

ScenarioControlsBase::ScenarioControlsBase(
    lv_obj_t* parentContainer,
    Network::WebSocketServiceInterface* wsService,
    const std::string& scenarioId)
    : parentContainer_(parentContainer), wsService_(wsService), scenarioId_(scenarioId)
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
    if (!wsService_ || !wsService_->isConnected()) return;

    // Track rapid calls for loop detection.
    static int updateCount = 0;
    static auto lastUpdateTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdateTime).count();

    if (elapsed < 1000) { // Within 1 second.
        updateCount++;
    }
    else {
        updateCount = 1;
    }
    lastUpdateTime = now;

    const Api::ScenarioConfigSet::Command cmd{ .config = config };

    spdlog::info(
        "ScenarioControlsBase: Sending config update for '{}' (update #{} in {}ms)",
        scenarioId_,
        updateCount,
        elapsed);

    // Send binary command.
    static std::atomic<uint64_t> nextId{ 1 };
    auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);
    auto result = wsService_->sendBinary(Network::serialize_envelope(envelope));
    if (result.isError()) {
        spdlog::error(
            "ScenarioControlsBase: Failed to send ScenarioConfigSet: {}", result.errorValue());
    }
}

} // namespace Ui
} // namespace DirtSim
