#include "ControlPanel.h"
#include "server/api/Exit.h"
#include "server/api/SeedAdd.h"
#include "server/api/SimRun.h"
#include "server/api/SpawnDirtBall.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

ControlPanel::ControlPanel(
    lv_obj_t* container, [[maybe_unused]] void* wsClient, EventSink& eventSink)
    : container_(container), wsClient_(nullptr), eventSink_(eventSink)
{
    if (!container_) {
        spdlog::error("ControlPanel: Null container provided");
        return;
    }

    // Create left-side panel container for controls.
    panelContainer_ = lv_obj_create(container_);
    lv_obj_set_size(panelContainer_, 260, LV_PCT(100)); // 260px wide (30% wider), full height.
    lv_obj_align(panelContainer_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_flex_flow(panelContainer_, LV_FLEX_FLOW_COLUMN); // Stack controls vertically.
    lv_obj_set_flex_align(
        panelContainer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Reduce padding/gaps to fit more controls without scrolling.
    lv_obj_set_style_pad_row(panelContainer_, 2, 0);    // Minimal spacing between items.
    lv_obj_set_style_pad_all(panelContainer_, 5, 0);    // Minimal padding around edges.
    lv_obj_set_scroll_dir(panelContainer_, LV_DIR_VER); // Enable vertical scrolling.
    lv_obj_set_scrollbar_mode(
        panelContainer_, LV_SCROLLBAR_MODE_AUTO); // Show scrollbar when needed.

    // Create core controls.
    createCoreControls();

    spdlog::info("ControlPanel: Initialized with core controls");
}

ControlPanel::~ControlPanel()
{
    // Explicitly delete widgets to prevent use-after-free from queued LVGL events.
    if (panelContainer_) {
        lv_obj_del(panelContainer_); // Recursively deletes all child widgets.
        panelContainer_ = nullptr;
    }
    spdlog::info("ControlPanel: Destroyed");
}

void ControlPanel::updateFromWorldData(
    const WorldData& data, const std::string& scenario_id, const ScenarioConfig& scenario_config)
{
    // Update world dimensions.
    worldWidth_ = data.width;
    worldHeight_ = data.height;

    // Update stats display.
    if (statsLabel_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Server: %.1f FPS", data.fps_server);
        lv_label_set_text(statsLabel_, buf);
    }

    // Rebuild scenario controls if scenario changed.
    if (scenario_id != currentScenarioId_) {
        spdlog::info("ControlPanel: Scenario changed to '{}'", scenario_id);
        clearScenarioControls();
        createScenarioControls(scenario_id, scenario_config);
        currentScenarioId_ = scenario_id;
    }
}

void ControlPanel::createCoreControls()
{
    // Quit button.
    quitButton_ = lv_btn_create(panelContainer_);
    lv_obj_set_width(quitButton_, LV_PCT(90));
    lv_obj_t* quitLabel = lv_label_create(quitButton_);
    lv_label_set_text(quitLabel, "Quit");
    lv_obj_center(quitLabel);
    lv_obj_set_user_data(quitButton_, this);
    lv_obj_add_event_cb(quitButton_, onQuitClicked, LV_EVENT_CLICKED, nullptr);

    // Add spacing after quit button.
    lv_obj_t* spacer1 = lv_obj_create(panelContainer_);
    lv_obj_set_size(spacer1, LV_PCT(100), 10);
    lv_obj_set_style_bg_opa(spacer1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer1, 0, 0);

    // Stats display.
    statsLabel_ = lv_label_create(panelContainer_);
    lv_label_set_text(statsLabel_, "Server: -- FPS");
    lv_obj_set_style_text_font(statsLabel_, &lv_font_montserrat_12, 0);

    statsLabelUI_ = lv_label_create(panelContainer_);
    lv_label_set_text(statsLabelUI_, "UI: -- FPS");
    lv_obj_set_style_text_font(statsLabelUI_, &lv_font_montserrat_12, 0);

    // Add spacing after stats labels.
    lv_obj_t* spacer2 = lv_obj_create(panelContainer_);
    lv_obj_set_size(spacer2, LV_PCT(100), 10);
    lv_obj_set_style_bg_opa(spacer2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer2, 0, 0);

    // Debug toggle.
    debugSwitch_ = LVGLBuilder::actionButton(panelContainer_)
                       .text("Debug Draw")
                       .mode(LVGLBuilder::ActionMode::Toggle)
                       .size(80)
                       .checked(false)
                       .glowColor(0x00CC00)
                       .callback(onDebugToggled, this)
                       .buildOrLog();

    spdlog::debug("ControlPanel: Core controls created");
}

void ControlPanel::createScenarioControls(
    const std::string& scenarioId, [[maybe_unused]] const ScenarioConfig& config)
{
    // Create scenario panel container.
    scenarioPanel_ = lv_obj_create(panelContainer_);
    lv_obj_set_size(
        scenarioPanel_, LV_PCT(100), LV_SIZE_CONTENT); // Full width, height fits content.
    lv_obj_set_flex_flow(scenarioPanel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        scenarioPanel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Scenario dropdown selector with ActionDropdown styling.
    scenarioContainer_ =
        LVGLBuilder::actionDropdown(scenarioPanel_)
            .label("Scenario:")
            .options(
                "Benchmark\nDam Break\nEmpty\nFalling Dirt\nRaining\nSandbox\nTree "
                "Germination\nWater Equalization")
            .selected(0) // "Benchmark" selected by default.
            .width(LV_PCT(95))
            .callback(onScenarioChanged, this)
            .buildOrLog();

    if (scenarioContainer_) {
        spdlog::info("ControlPanel: Scenario dropdown created successfully");
    }
    else {
        spdlog::error("ControlPanel: Failed to create scenario dropdown!");
    }

    // Create controls based on scenario type.
    // DISABLED: SimPlayground already creates SandboxControls, having duplicate controls
    // causes infinite update loops between the two sets of controls
    // if (scenarioId == "sandbox" && std::holds_alternative<Config::Sandbox>(config)) {
    //     createSandboxControls(std::get<Config::Sandbox>(config));
    // }
    // TODO: Add other scenario control creators here.

    spdlog::debug("ControlPanel: Scenario controls created for '{}'", scenarioId);
}

void ControlPanel::clearScenarioControls()
{
    if (scenarioPanel_) {
        lv_obj_del(scenarioPanel_);
        scenarioPanel_ = nullptr;
        scenarioContainer_ = nullptr;
        sandboxAddSeedButton_ = nullptr;
        sandboxQuadrantSwitch_ = nullptr;
        sandboxRainSlider_ = nullptr;
        sandboxRightThrowSwitch_ = nullptr;
        sandboxDropDirtBallButton_ = nullptr;
        sandboxWaterColumnSwitch_ = nullptr;
    }
}

void ControlPanel::createSandboxControls(const Config::Sandbox& config)
{
    // Sandbox-specific controls label.
    lv_obj_t* sandboxLabel = lv_label_create(scenarioPanel_);
    lv_label_set_text(sandboxLabel, "--- Sandbox Controls ---");

    // Add Seed button (push).
    sandboxAddSeedButton_ = LVGLBuilder::actionButton(scenarioPanel_)
                                .text("Add Seed")
                                .icon(LV_SYMBOL_PLUS)
                                .mode(LVGLBuilder::ActionMode::Push)
                                .size(80)
                                .backgroundColor(0x228B22)
                                .callback(onAddSeedClicked, this)
                                .buildOrLog();

    // Quadrant toggle.
    sandboxQuadrantSwitch_ = LVGLBuilder::actionButton(scenarioPanel_)
                                 .text("Quadrant")
                                 .mode(LVGLBuilder::ActionMode::Toggle)
                                 .size(80)
                                 .checked(config.quadrantEnabled)
                                 .glowColor(0x00CC00)
                                 .callback(onSandboxQuadrantToggled, this)
                                 .buildOrLog();

    // Water column toggle.
    sandboxWaterColumnSwitch_ = LVGLBuilder::actionButton(scenarioPanel_)
                                    .text("Water Column")
                                    .mode(LVGLBuilder::ActionMode::Toggle)
                                    .size(80)
                                    .checked(config.waterColumnEnabled)
                                    .glowColor(0x0088FF)
                                    .callback(onSandboxWaterColumnToggled, this)
                                    .buildOrLog();

    // Right throw toggle.
    sandboxRightThrowSwitch_ = LVGLBuilder::actionButton(scenarioPanel_)
                                   .text("Right Throw")
                                   .mode(LVGLBuilder::ActionMode::Toggle)
                                   .size(80)
                                   .checked(config.rightThrowEnabled)
                                   .glowColor(0x00CC00)
                                   .callback(onSandboxRightThrowToggled, this)
                                   .buildOrLog();

    // Drop Dirt Ball button (push).
    sandboxDropDirtBallButton_ = LVGLBuilder::actionButton(scenarioPanel_)
                                     .text("Drop Dirt")
                                     .icon(LV_SYMBOL_DOWNLOAD)
                                     .mode(LVGLBuilder::ActionMode::Push)
                                     .size(80)
                                     .backgroundColor(0x8B4513)
                                     .callback(onDropDirtBallClicked, this)
                                     .buildOrLog();

    // Rain slider.
    sandboxRainSlider_ = LVGLBuilder::slider(scenarioPanel_)
                             .size(LV_PCT(80), 10)
                             .range(0, 100)
                             .value(static_cast<int>(config.rainRate * 10))
                             .label("Rain Rate")
                             .callback(onSandboxRainSliderChanged, this)
                             .buildOrLog();

    spdlog::debug("ControlPanel: Sandbox controls created");
}

// ============================================================================
// Event Handlers
// ============================================================================

void ControlPanel::onScenarioChanged(lv_event_t* e)
{
    auto* panel = static_cast<ControlPanel*>(lv_event_get_user_data(e));
    if (!panel) return;

    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    uint16_t selectedIdx = lv_dropdown_get_selected(dropdown);

    // Map dropdown index to scenario_id (must match dropdown options order).
    const char* scenarioIds[] = { "benchmark",        "dam_break",         "empty",
                                  "falling_dirt",     "raining",           "sandbox",
                                  "tree_germination", "water_equalization" };

    constexpr size_t SCENARIO_COUNT = 8;
    if (selectedIdx >= SCENARIO_COUNT) {
        spdlog::error("ControlPanel: Invalid scenario index {}", selectedIdx);
        return;
    }

    std::string scenario_id = scenarioIds[selectedIdx];
    spdlog::info("ControlPanel: Scenario changed to '{}'", scenario_id);

    // DISABLED: ControlPanel is unused dead code.
    // TODO: Delete ControlPanel entirely or migrate to WebSocketService.
    spdlog::warn("ControlPanel: Scenario change disabled (ControlPanel is deprecated)");
}

void ControlPanel::onAddSeedClicked(lv_event_t* e)
{
    auto* panel = static_cast<ControlPanel*>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    if (!panel) return;

    spdlog::info("ControlPanel: Add Seed button clicked");
    // DISABLED: ControlPanel is unused dead code.
    spdlog::warn("ControlPanel: Add Seed disabled (ControlPanel is deprecated)");
}

void ControlPanel::onDropDirtBallClicked(lv_event_t* e)
{
    auto* panel = static_cast<ControlPanel*>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    if (!panel) return;

    spdlog::info("ControlPanel: Drop Dirt Ball button clicked");
    // DISABLED: ControlPanel is unused dead code.
    spdlog::warn("ControlPanel: Drop Dirt Ball disabled (ControlPanel is deprecated)");
}

void ControlPanel::onQuitClicked(lv_event_t* e)
{
    auto* panel = static_cast<ControlPanel*>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    if (!panel) return;

    spdlog::info("ControlPanel: Quit button clicked");

    // DISABLED: ControlPanel is unused dead code.

    // Also exit the UI itself.
    auto exitCmd = UiApi::Exit::Command{};
    auto exitCwc = UiApi::Exit::Cwc(std::move(exitCmd), [](const auto& /*response*/) {
        // No action needed on response.
    });
    panel->eventSink_.queueEvent(std::move(exitCwc));
}

void ControlPanel::onDebugToggled(lv_event_t* e)
{
    auto* panel = static_cast<ControlPanel*>(lv_event_get_user_data(e));
    if (!panel) return;

    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(panel->debugSwitch_);
    spdlog::info("ControlPanel: Debug draw toggled: {}", enabled);

    panel->sendDebugUpdate(enabled);
}

void ControlPanel::onSandboxQuadrantToggled(lv_event_t* e)
{
    auto* panel = static_cast<ControlPanel*>(lv_event_get_user_data(e));
    if (!panel) return;

    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(panel->sandboxQuadrantSwitch_);
    spdlog::info("ControlPanel: Sandbox quadrant toggled: {}", enabled);

    // Create updated config.
    Config::Sandbox config;
    config.quadrantEnabled = enabled;
    config.waterColumnEnabled = panel->sandboxWaterColumnSwitch_
        ? LVGLBuilder::ActionButtonBuilder::isChecked(panel->sandboxWaterColumnSwitch_)
        : true;
    config.rightThrowEnabled = panel->sandboxRightThrowSwitch_
        ? LVGLBuilder::ActionButtonBuilder::isChecked(panel->sandboxRightThrowSwitch_)
        : true;
    config.rainRate = panel->sandboxRainSlider_
        ? lv_slider_get_value(static_cast<lv_obj_t*>(panel->sandboxRainSlider_)) / 10.0
        : 0.0;

    panel->sendConfigUpdate(config);
}

void ControlPanel::onSandboxWaterColumnToggled(lv_event_t* e)
{
    auto* panel = static_cast<ControlPanel*>(lv_event_get_user_data(e));
    if (!panel) return;

    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(panel->sandboxWaterColumnSwitch_);
    spdlog::info("ControlPanel: Sandbox water column toggled: {}", enabled);

    // Create updated config with all current values.
    Config::Sandbox config;
    config.quadrantEnabled = panel->sandboxQuadrantSwitch_
        ? LVGLBuilder::ActionButtonBuilder::isChecked(panel->sandboxQuadrantSwitch_)
        : true;
    config.waterColumnEnabled = enabled;
    config.rightThrowEnabled = panel->sandboxRightThrowSwitch_
        ? LVGLBuilder::ActionButtonBuilder::isChecked(panel->sandboxRightThrowSwitch_)
        : true;
    config.rainRate = panel->sandboxRainSlider_
        ? lv_slider_get_value(static_cast<lv_obj_t*>(panel->sandboxRainSlider_)) / 10.0
        : 0.0;

    panel->sendConfigUpdate(config);
}

void ControlPanel::onSandboxRightThrowToggled(lv_event_t* e)
{
    auto* panel = static_cast<ControlPanel*>(lv_event_get_user_data(e));
    if (!panel) return;

    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(panel->sandboxRightThrowSwitch_);
    spdlog::info("ControlPanel: Sandbox right throw toggled: {}", enabled);

    Config::Sandbox config;
    config.quadrantEnabled = panel->sandboxQuadrantSwitch_
        ? LVGLBuilder::ActionButtonBuilder::isChecked(panel->sandboxQuadrantSwitch_)
        : true;
    config.waterColumnEnabled = panel->sandboxWaterColumnSwitch_
        ? LVGLBuilder::ActionButtonBuilder::isChecked(panel->sandboxWaterColumnSwitch_)
        : true;
    config.rightThrowEnabled = enabled;
    config.rainRate = panel->sandboxRainSlider_
        ? lv_slider_get_value(static_cast<lv_obj_t*>(panel->sandboxRainSlider_)) / 10.0
        : 0.0;

    panel->sendConfigUpdate(config);
}

void ControlPanel::onSandboxRainSliderChanged(lv_event_t* e)
{
    auto* panel = static_cast<ControlPanel*>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    if (!panel) return;

    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int32_t sliderValue = lv_slider_get_value(slider);
    double rainRate = sliderValue / 10.0;
    spdlog::info("ControlPanel: Sandbox rain rate changed: {}", rainRate);

    Config::Sandbox config;
    config.quadrantEnabled = panel->sandboxQuadrantSwitch_
        ? LVGLBuilder::ActionButtonBuilder::isChecked(panel->sandboxQuadrantSwitch_)
        : true;
    config.waterColumnEnabled = panel->sandboxWaterColumnSwitch_
        ? LVGLBuilder::ActionButtonBuilder::isChecked(panel->sandboxWaterColumnSwitch_)
        : true;
    config.rightThrowEnabled = panel->sandboxRightThrowSwitch_
        ? LVGLBuilder::ActionButtonBuilder::isChecked(panel->sandboxRightThrowSwitch_)
        : true;
    config.rainRate = rainRate;

    panel->sendConfigUpdate(config);
}

// ============================================================================
// Command Sending
// ============================================================================

void ControlPanel::sendConfigUpdate([[maybe_unused]] const ScenarioConfig& config)
{
    // DISABLED: ControlPanel is unused dead code.
    spdlog::warn("ControlPanel: sendConfigUpdate disabled (ControlPanel is deprecated)");
}

void ControlPanel::sendDebugUpdate(bool enabled)
{
    // Queue DrawDebugToggle command to UI state machine (UI-local, not sent to server).
    auto cmd = UiApi::DrawDebugToggle::Command{ enabled };
    auto cwc = UiApi::DrawDebugToggle::Cwc(std::move(cmd), [](const auto& /*response*/) {
        // No action needed on response.
    });

    eventSink_.queueEvent(std::move(cwc));
    spdlog::info("ControlPanel: Queued DrawDebugToggle command (enabled: {})", enabled);
}

} // namespace Ui
} // namespace DirtSim
