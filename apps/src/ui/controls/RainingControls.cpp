#include "RainingControls.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

RainingControls::RainingControls(
    lv_obj_t* container,
    Network::WebSocketServiceInterface* wsService,
    const Config::Raining& config)
    : ScenarioControlsBase(container, wsService, "raining")
{
    // Create widgets.
    createWidgets();

    // Initialize widget states from config.
    updateFromConfig(config);

    // Finish initialization - allow callbacks to send updates now.
    finishInitialization();

    spdlog::info("RainingControls: Initialized");
}

RainingControls::~RainingControls()
{
    // Base class handles container deletion.
    spdlog::info("RainingControls: Destroyed");
}

void RainingControls::createWidgets()
{
    // Rain rate toggle slider.
    rainControl_ = ToggleSlider::create(controlsContainer_)
                       .label("Rain Rate")
                       .range(0, 100)
                       .value(0)
                       .defaultValue(50)
                       .valueScale(1.0)
                       .valueFormat("%.0f")
                       .initiallyEnabled(false)
                       .sliderWidth(180)
                       .onToggle([this](bool enabled) { onRainToggled(enabled); })
                       .onValueChange([this](int value) { onRainSliderChanged(value); })
                       .build();

    // Drain size toggle slider.
    drainSizeControl_ = ToggleSlider::create(controlsContainer_)
                            .label("Drain Size")
                            .range(0, 100)
                            .value(0)
                            .defaultValue(20)
                            .valueScale(1.0)
                            .valueFormat("%.0f")
                            .initiallyEnabled(false)
                            .sliderWidth(180)
                            .onToggle([this](bool enabled) { onDrainSizeToggled(enabled); })
                            .onValueChange([this](int value) { onDrainSizeSliderChanged(value); })
                            .build();

    // Max fill toggle slider.
    maxFillControl_ = ToggleSlider::create(controlsContainer_)
                          .label("Max Fill %")
                          .range(10, 100)
                          .value(50)
                          .defaultValue(50)
                          .valueScale(1.0)
                          .valueFormat("%.0f%%")
                          .initiallyEnabled(false)
                          .sliderWidth(180)
                          .onToggle([this](bool enabled) { onMaxFillToggled(enabled); })
                          .onValueChange([this](int value) { onMaxFillSliderChanged(value); })
                          .build();
}

void RainingControls::updateFromConfig(const ScenarioConfig& configVariant)
{
    // Extract Config::Raining from variant.
    if (!std::holds_alternative<Config::Raining>(configVariant)) {
        spdlog::error("RainingControls: Invalid config type (expected Config::Raining)");
        return;
    }

    const Config::Raining& config = std::get<Config::Raining>(configVariant);
    spdlog::info(
        "RainingControls: updateFromConfig called - rainRate={}, drainSize={}, maxFill={}",
        config.rainRate,
        config.drainSize,
        config.maxFillPercent);

    // Prevent sending updates back to server during UI sync.
    bool wasInitializing = isInitializing();
    if (!wasInitializing) {
        initializing_ = true;
    }

    // Update rain control.
    if (rainControl_) {
        bool shouldBeEnabled = config.rainRate > 0.0;
        int sliderValue = static_cast<int>(config.rainRate);
        rainControl_->setEnabled(shouldBeEnabled);
        if (shouldBeEnabled) {
            rainControl_->setValue(sliderValue);
        }
        spdlog::debug(
            "RainingControls: Updated rain control (enabled={}, value={})",
            shouldBeEnabled,
            sliderValue);
    }

    // Update drain size control.
    if (drainSizeControl_) {
        bool shouldBeEnabled = config.drainSize > 0.0;
        int sliderValue = static_cast<int>(config.drainSize);
        drainSizeControl_->setEnabled(shouldBeEnabled);
        if (shouldBeEnabled) {
            drainSizeControl_->setValue(sliderValue);
        }
        spdlog::debug(
            "RainingControls: Updated drain control (enabled={}, value={})",
            shouldBeEnabled,
            sliderValue);
    }

    // Update max fill control.
    if (maxFillControl_) {
        bool shouldBeEnabled = config.maxFillPercent > 0.0;
        int sliderValue = static_cast<int>(config.maxFillPercent);
        maxFillControl_->setEnabled(shouldBeEnabled);
        if (shouldBeEnabled) {
            maxFillControl_->setValue(sliderValue);
        }
        spdlog::debug(
            "RainingControls: Updated max fill control (enabled={}, value={})",
            shouldBeEnabled,
            sliderValue);
    }

    // Restore initializing state.
    if (!wasInitializing) {
        initializing_ = false;
    }
}

Config::Raining RainingControls::getCurrentConfig() const
{
    Config::Raining config;

    // Get rain rate from control.
    if (rainControl_) {
        if (rainControl_->isEnabled()) {
            config.rainRate = rainControl_->getScaledValue();
        }
        else {
            config.rainRate = 0.0;
        }
    }

    // Get drain size from control.
    if (drainSizeControl_) {
        if (drainSizeControl_->isEnabled()) {
            config.drainSize = drainSizeControl_->getScaledValue();
        }
        else {
            config.drainSize = 0.0;
        }
    }

    // Get max fill percent from control.
    if (maxFillControl_) {
        if (maxFillControl_->isEnabled()) {
            config.maxFillPercent = maxFillControl_->getScaledValue();
        }
        else {
            config.maxFillPercent = 0.0;
        }
    }

    return config;
}

void RainingControls::onRainToggled(bool enabled)
{
    // Don't send updates during initialization.
    if (isInitializing()) {
        spdlog::debug("RainingControls: Ignoring rain toggle during initialization");
        return;
    }

    spdlog::info("RainingControls: Rain toggled to {}", enabled ? "ON" : "OFF");

    // Get current config and send update.
    Config::Raining config = getCurrentConfig();
    sendConfigUpdate(config);
}

void RainingControls::onRainSliderChanged(int value)
{
    // Don't send updates during initialization.
    if (isInitializing()) {
        spdlog::debug("RainingControls: Ignoring rain slider during initialization");
        return;
    }

    spdlog::info("RainingControls: Rain rate changed to {}", value);

    // Get complete current config and send update.
    Config::Raining config = getCurrentConfig();
    sendConfigUpdate(config);
}

void RainingControls::onDrainSizeToggled(bool enabled)
{
    // Don't send updates during initialization.
    if (isInitializing()) {
        spdlog::debug("RainingControls: Ignoring drain size toggle during initialization");
        return;
    }

    spdlog::info("RainingControls: Drain size toggled to {}", enabled ? "ON" : "OFF");

    // Get current config and send update.
    Config::Raining config = getCurrentConfig();
    sendConfigUpdate(config);
}

void RainingControls::onDrainSizeSliderChanged(int value)
{
    // Don't send updates during initialization.
    if (isInitializing()) {
        spdlog::debug("RainingControls: Ignoring drain size slider during initialization");
        return;
    }

    spdlog::info("RainingControls: Drain size changed to {}", value);

    // Get complete current config and send update.
    Config::Raining config = getCurrentConfig();
    sendConfigUpdate(config);
}

void RainingControls::onMaxFillToggled(bool enabled)
{
    // Don't send updates during initialization.
    if (isInitializing()) {
        spdlog::debug("RainingControls: Ignoring max fill toggle during initialization");
        return;
    }

    spdlog::info("RainingControls: Max fill toggled to {}", enabled ? "ON" : "OFF");

    // Get current config and send update.
    Config::Raining config = getCurrentConfig();
    sendConfigUpdate(config);
}

void RainingControls::onMaxFillSliderChanged(int value)
{
    // Don't send updates during initialization.
    if (isInitializing()) {
        spdlog::debug("RainingControls: Ignoring max fill slider during initialization");
        return;
    }

    spdlog::info("RainingControls: Max fill percent changed to {}%%", value);

    // Get complete current config and send update.
    Config::Raining config = getCurrentConfig();
    sendConfigUpdate(config);
}

} // namespace Ui
} // namespace DirtSim
