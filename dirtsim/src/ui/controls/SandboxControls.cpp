#include "SandboxControls.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/ScenarioConfigSet.h"
#include "server/api/SeedAdd.h"
#include "server/api/SpawnDirtBall.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

SandboxControls::SandboxControls(
    lv_obj_t* container, Network::WebSocketService* wsService, const SandboxConfig& config)
    : ScenarioControlsBase(container, wsService, "sandbox")
{
    // Create widgets with initial config.
    createWidgets();

    // Initialize widget states from config.
    updateFromConfig(config);

    // Finish initialization - allow callbacks to send updates now.
    finishInitialization();

    spdlog::info("SandboxControls: Initialized");
}

void SandboxControls::createWidgets()
{
    // Add Seed button - green for growth/life.
    addSeedButton_ = LVGLBuilder::button(controlsContainer_)
                         .text("Add Seed")
                         .icon(LV_SYMBOL_PLUS)
                         .backgroundColor(0x228B22) // Forest green.
                         .pressedColor(0x186618)
                         .callback(onAddSeedClicked, this)
                         .buildOrLog();

    // Drop Dirt Ball button - brown/earth tone.
    dropDirtBallButton_ = LVGLBuilder::button(controlsContainer_)
                              .text("Drop Dirt")
                              .icon(LV_SYMBOL_DOWNLOAD)
                              .backgroundColor(0x8B4513) // Saddle brown.
                              .pressedColor(0x5C2E0D)
                              .callback(onDropDirtBallClicked, this)
                              .buildOrLog();

    // Quadrant toggle.
    quadrantSwitch_ = LVGLBuilder::labeledSwitch(controlsContainer_)
                          .label("Quadrant")
                          .initialState(false)
                          .callback(onQuadrantToggled, this)
                          .buildOrLog();

    // Water column toggle.
    waterColumnSwitch_ = LVGLBuilder::labeledSwitch(controlsContainer_)
                             .label("Water Column")
                             .initialState(false)
                             .callback(onWaterColumnToggled, this)
                             .buildOrLog();

    // Right throw toggle.
    rightThrowSwitch_ = LVGLBuilder::labeledSwitch(controlsContainer_)
                            .label("Right Throw")
                            .initialState(false)
                            .callback(onRightThrowToggled, this)
                            .buildOrLog();

    // Rain toggle slider - fancy control with enable/disable toggle.
    rainControl_ = ToggleSlider::create(controlsContainer_)
                       .label("Rain")
                       .range(0, 100)
                       .value(0)
                       .defaultValue(50)
                       .valueScale(0.1)
                       .valueFormat("%.1f")
                       .initiallyEnabled(false)
                       .sliderWidth(180)
                       .onToggle([this](bool enabled) { onRainToggled(enabled); })
                       .onValueChange([this](int value) { onRainSliderChanged(value); })
                       .build();
}

SandboxControls::~SandboxControls()
{
    // Base class handles container deletion.
    spdlog::info("SandboxControls: Destroyed");
}

void SandboxControls::updateFromConfig(const ScenarioConfig& configVariant)
{
    // Extract SandboxConfig from variant.
    if (!std::holds_alternative<SandboxConfig>(configVariant)) {
        spdlog::error("SandboxControls: Invalid config type (expected SandboxConfig)");
        return;
    }

    const SandboxConfig& config = std::get<SandboxConfig>(configVariant);

    // Prevent sending updates back to server during UI sync.
    bool wasInitializing = isInitializing();
    if (!wasInitializing) {
        initializing_ = true;
    }

    // Update quadrant switch.
    if (quadrantSwitch_) {
        bool currentState = lv_obj_has_state(quadrantSwitch_, LV_STATE_CHECKED);
        if (currentState != config.quadrant_enabled) {
            if (config.quadrant_enabled) {
                lv_obj_add_state(quadrantSwitch_, LV_STATE_CHECKED);
            }
            else {
                lv_obj_remove_state(quadrantSwitch_, LV_STATE_CHECKED);
            }
            spdlog::debug(
                "SandboxControls: Updated quadrant switch to {}", config.quadrant_enabled);
        }
    }

    // Update water column switch.
    if (waterColumnSwitch_) {
        bool currentState = lv_obj_has_state(waterColumnSwitch_, LV_STATE_CHECKED);
        if (currentState != config.water_column_enabled) {
            if (config.water_column_enabled) {
                lv_obj_add_state(waterColumnSwitch_, LV_STATE_CHECKED);
            }
            else {
                lv_obj_remove_state(waterColumnSwitch_, LV_STATE_CHECKED);
            }
            spdlog::info(
                "SandboxControls: Updated water column switch to {}", config.water_column_enabled);
        }
    }

    // Update right throw switch.
    if (rightThrowSwitch_) {
        bool currentState = lv_obj_has_state(rightThrowSwitch_, LV_STATE_CHECKED);
        if (currentState != config.right_throw_enabled) {
            if (config.right_throw_enabled) {
                lv_obj_add_state(rightThrowSwitch_, LV_STATE_CHECKED);
            }
            else {
                lv_obj_remove_state(rightThrowSwitch_, LV_STATE_CHECKED);
            }
            spdlog::debug(
                "SandboxControls: Updated right throw switch to {}", config.right_throw_enabled);
        }
    }

    // Update rain control.
    if (rainControl_) {
        bool shouldBeEnabled = config.rain_rate > 0.0;
        int sliderValue = static_cast<int>(config.rain_rate * 10); // Scale to [0, 100].
        rainControl_->setEnabled(shouldBeEnabled);
        if (shouldBeEnabled) {
            rainControl_->setValue(sliderValue);
        }
        spdlog::debug(
            "SandboxControls: Updated rain control (enabled={}, value={})", shouldBeEnabled, sliderValue);
    }

    // Restore initializing state.
    if (!wasInitializing) {
        initializing_ = false;
    }
}

void SandboxControls::updateWorldDimensions(uint32_t width, uint32_t height)
{
    worldWidth_ = width;
    worldHeight_ = height;
    spdlog::debug("SandboxControls: Updated world dimensions to {}×{}", width, height);
}

SandboxConfig SandboxControls::getCurrentConfig() const
{
    SandboxConfig config;

    // Get current state of all controls
    if (quadrantSwitch_) {
        config.quadrant_enabled = lv_obj_has_state(quadrantSwitch_, LV_STATE_CHECKED);
    }

    if (waterColumnSwitch_) {
        config.water_column_enabled = lv_obj_has_state(waterColumnSwitch_, LV_STATE_CHECKED);
    }

    if (rightThrowSwitch_) {
        config.right_throw_enabled = lv_obj_has_state(rightThrowSwitch_, LV_STATE_CHECKED);
    }

    if (rainControl_) {
        if (rainControl_->isEnabled()) {
            config.rain_rate = rainControl_->getScaledValue();
        }
        else {
            config.rain_rate = 0.0;
        }
    }

    return config;
}

void SandboxControls::onAddSeedClicked(lv_event_t* e)
{
    SandboxControls* self = static_cast<SandboxControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("SandboxControls: onAddSeedClicked called with null self");
        return;
    }

    spdlog::info("SandboxControls: Add Seed button clicked");

    if (self->wsService_ && self->wsService_->isConnected()) {
        const Api::SeedAdd::Command cmd{ .x = static_cast<int>(self->worldWidth_ / 2), .y = 5 };

        spdlog::info("SandboxControls: Sending seed_add at ({}, {})", cmd.x, cmd.y);

        // Send binary command.
        static std::atomic<uint64_t> nextId{ 1 };
        auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);
        auto result = self->wsService_->sendBinary(Network::serialize_envelope(envelope));
        if (result.isError()) {
            spdlog::error("SandboxControls: Failed to send SeedAdd: {}", result.errorValue());
        }
    }
}

void SandboxControls::onDropDirtBallClicked(lv_event_t* e)
{
    SandboxControls* self = static_cast<SandboxControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("SandboxControls: onDropDirtBallClicked called with null self");
        return;
    }

    spdlog::info("SandboxControls: Drop Dirt Ball button clicked");

    if (self->wsService_ && self->wsService_->isConnected()) {
        const Api::SpawnDirtBall::Command cmd{};

        spdlog::info("SandboxControls: Sending spawn_dirt_ball command");

        // Send binary command.
        static std::atomic<uint64_t> nextId{ 1 };
        auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);
        auto result = self->wsService_->sendBinary(Network::serialize_envelope(envelope));
        if (result.isError()) {
            spdlog::error("SandboxControls: Failed to send SpawnDirtBall: {}", result.errorValue());
        }
    }
}

void SandboxControls::onQuadrantToggled(lv_event_t* e)
{
    SandboxControls* self = static_cast<SandboxControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("SandboxControls: onQuadrantToggled called with null self");
        return;
    }

    // Don't send updates during initialization
    if (self->initializing_) {
        spdlog::debug("SandboxControls: Ignoring quadrant toggle during initialization");
        return;
    }

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
    spdlog::info("SandboxControls: Quadrant toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config from all controls
    SandboxConfig config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void SandboxControls::onWaterColumnToggled(lv_event_t* e)
{
    SandboxControls* self = static_cast<SandboxControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("SandboxControls: onWaterColumnToggled called with null self");
        return;
    }

    // Don't send updates during initialization
    if (self->initializing_) {
        spdlog::debug("SandboxControls: Ignoring water column toggle during initialization");
        return;
    }

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
    spdlog::info("SandboxControls: Water Column toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config from all controls
    SandboxConfig config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void SandboxControls::onRightThrowToggled(lv_event_t* e)
{
    SandboxControls* self = static_cast<SandboxControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("SandboxControls: onRightThrowToggled called with null self");
        return;
    }

    // Don't send updates during initialization
    if (self->initializing_) {
        spdlog::debug("SandboxControls: Ignoring right throw toggle during initialization");
        return;
    }

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
    spdlog::info("SandboxControls: Right Throw toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config from all controls
    SandboxConfig config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void SandboxControls::onRainToggled(bool enabled)
{
    // Don't send updates during initialization.
    if (initializing_) {
        spdlog::debug("SandboxControls: Ignoring rain toggle during initialization");
        return;
    }

    spdlog::info("SandboxControls: Rain toggled to {}", enabled ? "ON" : "OFF");

    // Get current config and update.
    SandboxConfig config = getCurrentConfig();
    sendConfigUpdate(config);
}

void SandboxControls::onRainSliderChanged(int value)
{
    // Don't send updates during initialization.
    if (initializing_) {
        spdlog::debug("SandboxControls: Ignoring rain slider during initialization");
        return;
    }

    double rainRate = value * 0.1;

    // Track last value to prevent redundant updates.
    static double lastRainRate = -1.0;
    if (std::abs(rainRate - lastRainRate) < 0.01) {
        // Value hasn't changed - don't send update.
        return;
    }
    lastRainRate = rainRate;

    spdlog::info("SandboxControls: Rain rate changed to {:.1f}", rainRate);

    // Get complete current config from all controls.
    SandboxConfig config = getCurrentConfig();
    sendConfigUpdate(config);
}

} // namespace Ui
} // namespace DirtSim
