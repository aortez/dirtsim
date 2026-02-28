#include "SandboxControls.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
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
    lv_obj_t* container,
    Network::WebSocketServiceInterface* wsService,
    UserSettingsManager& userSettingsManager,
    const Config::Sandbox& config)
    : ScenarioControlsBase(container, wsService, userSettingsManager, "sandbox")
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
    // Row 1: Add Seed and Drop Dirt buttons (evenly spaced).
    lv_obj_t* row1 = lv_obj_create(controlsContainer_);
    lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        row1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row1, 4, 0);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);

    // Add Seed button - green for growth/life (push button).
    addSeedButton_ = LVGLBuilder::actionButton(row1)
                         .text("Add Seed")
                         .icon(LV_SYMBOL_PLUS)
                         .mode(LVGLBuilder::ActionMode::Push)
                         .size(80)
                         .backgroundColor(0x228B22) // Forest green.
                         .callback(onAddSeedClicked, this)
                         .buildOrLog();

    // Drop Dirt Ball button - brown/earth tone (push button).
    dropDirtBallButton_ = LVGLBuilder::actionButton(row1)
                              .text("Drop Dirt")
                              .icon(LV_SYMBOL_DOWNLOAD)
                              .mode(LVGLBuilder::ActionMode::Push)
                              .size(80)
                              .backgroundColor(0x8B4513) // Saddle brown.
                              .callback(onDropDirtBallClicked, this)
                              .buildOrLog();

    // Row 2: Quadrant, Water Column, Right Throw (evenly spaced).
    lv_obj_t* row2 = lv_obj_create(controlsContainer_);
    lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row2, 4, 0);
    lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);

    // Quadrant toggle.
    quadrantSwitch_ = LVGLBuilder::actionButton(row2)
                          .text("Quadrant")
                          .mode(LVGLBuilder::ActionMode::Toggle)
                          .size(80)
                          .checked(false)
                          .glowColor(0x00CC00) // Green glow when on.
                          .callback(onQuadrantToggled, this)
                          .buildOrLog();

    // Water column toggle.
    waterColumnSwitch_ = LVGLBuilder::actionButton(row2)
                             .text("Water Column")
                             .mode(LVGLBuilder::ActionMode::Toggle)
                             .size(80)
                             .checked(false)
                             .glowColor(0x0088FF) // Blue glow for water.
                             .callback(onWaterColumnToggled, this)
                             .buildOrLog();

    // Right throw toggle.
    rightThrowSwitch_ = LVGLBuilder::actionButton(row2)
                            .text("Right Throw")
                            .mode(LVGLBuilder::ActionMode::Toggle)
                            .size(80)
                            .checked(false)
                            .glowColor(0x00CC00)
                            .callback(onRightThrowToggled, this)
                            .buildOrLog();

    // Rain stepper (0 = off, 1-100 = rain rate 0.1-10.0).
    rainStepper_ = LVGLBuilder::actionStepper(controlsContainer_)
                       .label("Rain")
                       .range(0, 100)
                       .step(5)
                       .value(0)
                       .valueFormat("%.1f")
                       .valueScale(0.1)
                       .width(LV_PCT(95))
                       .callback(onRainChanged, this)
                       .buildOrLog();
}

SandboxControls::~SandboxControls()
{
    // Base class handles container deletion.
    spdlog::info("SandboxControls: Destroyed");
}

void SandboxControls::updateFromConfig(const ScenarioConfig& configVariant)
{
    // Extract Config::Sandbox from variant.
    if (!std::holds_alternative<Config::Sandbox>(configVariant)) {
        spdlog::error("SandboxControls: Invalid config type (expected Config::Sandbox)");
        return;
    }

    const Config::Sandbox& config = std::get<Config::Sandbox>(configVariant);

    // Prevent sending updates back to server during UI sync.
    bool wasInitializing = isInitializing();
    if (!wasInitializing) {
        initializing_ = true;
    }

    // Update quadrant button.
    if (quadrantSwitch_) {
        bool currentState = LVGLBuilder::ActionButtonBuilder::isChecked(quadrantSwitch_);
        if (currentState != config.quadrantEnabled) {
            LVGLBuilder::ActionButtonBuilder::setChecked(quadrantSwitch_, config.quadrantEnabled);
            spdlog::debug("SandboxControls: Updated quadrant button to {}", config.quadrantEnabled);
        }
    }

    // Update water column button.
    if (waterColumnSwitch_) {
        bool currentState = LVGLBuilder::ActionButtonBuilder::isChecked(waterColumnSwitch_);
        if (currentState != config.waterColumnEnabled) {
            LVGLBuilder::ActionButtonBuilder::setChecked(
                waterColumnSwitch_, config.waterColumnEnabled);
            spdlog::info(
                "SandboxControls: Updated water column button to {}", config.waterColumnEnabled);
        }
    }

    // Update right throw button.
    if (rightThrowSwitch_) {
        bool currentState = LVGLBuilder::ActionButtonBuilder::isChecked(rightThrowSwitch_);
        if (currentState != config.rightThrowEnabled) {
            LVGLBuilder::ActionButtonBuilder::setChecked(
                rightThrowSwitch_, config.rightThrowEnabled);
            spdlog::debug(
                "SandboxControls: Updated right throw button to {}", config.rightThrowEnabled);
        }
    }

    // Update rain stepper.
    if (rainStepper_) {
        int stepperValue = static_cast<int>(config.rainRate * 10); // Scale to [0, 100].
        LVGLBuilder::ActionStepperBuilder::setValue(rainStepper_, stepperValue);
        spdlog::debug("SandboxControls: Updated rain stepper to {}", stepperValue);
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

Config::Sandbox SandboxControls::getCurrentConfig() const
{
    Config::Sandbox config;

    // Get current state of all controls.
    if (quadrantSwitch_) {
        config.quadrantEnabled = LVGLBuilder::ActionButtonBuilder::isChecked(quadrantSwitch_);
    }

    if (waterColumnSwitch_) {
        config.waterColumnEnabled = LVGLBuilder::ActionButtonBuilder::isChecked(waterColumnSwitch_);
    }

    if (rightThrowSwitch_) {
        config.rightThrowEnabled = LVGLBuilder::ActionButtonBuilder::isChecked(rightThrowSwitch_);
    }

    if (rainStepper_) {
        int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(rainStepper_);
        config.rainRate = value * 0.1; // Convert [0, 100] to [0.0, 10.0].
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
        const Api::SeedAdd::Command cmd{ .x = static_cast<int>(self->worldWidth_ / 2),
                                         .y = 5,
                                         .genome_id = std::nullopt };

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

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("SandboxControls: Ignoring quadrant toggle during initialization");
        return;
    }

    // Get current state from ActionButton.
    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->quadrantSwitch_);
    spdlog::info("SandboxControls: Quadrant toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config from all controls.
    Config::Sandbox config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void SandboxControls::onWaterColumnToggled(lv_event_t* e)
{
    SandboxControls* self = static_cast<SandboxControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("SandboxControls: onWaterColumnToggled called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("SandboxControls: Ignoring water column toggle during initialization");
        return;
    }

    // Get current state from ActionButton.
    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->waterColumnSwitch_);
    spdlog::info("SandboxControls: Water Column toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config from all controls.
    Config::Sandbox config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void SandboxControls::onRightThrowToggled(lv_event_t* e)
{
    SandboxControls* self = static_cast<SandboxControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("SandboxControls: onRightThrowToggled called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("SandboxControls: Ignoring right throw toggle during initialization");
        return;
    }

    // Get current state from ActionButton.
    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->rightThrowSwitch_);
    spdlog::info("SandboxControls: Right Throw toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config from all controls.
    Config::Sandbox config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void SandboxControls::onRainChanged(lv_event_t* e)
{
    SandboxControls* self = static_cast<SandboxControls*>(lv_event_get_user_data(e));
    if (!self || !self->rainStepper_) {
        spdlog::error("SandboxControls: onRainChanged called with null self or stepper");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("SandboxControls: Ignoring rain change during initialization");
        return;
    }

    // Get value from stepper.
    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->rainStepper_);
    double rainRate = value * 0.1;

    spdlog::info("SandboxControls: Rain rate changed to {:.1f}", rainRate);

    // Get complete current config from all controls.
    Config::Sandbox config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

} // namespace Ui
} // namespace DirtSim
