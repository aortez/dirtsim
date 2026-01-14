#include "ClockControls.h"
#include "core/LoggingChannels.h"
#include "core/reflect.h"
#include "core/scenarios/ClockScenario.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

ClockControls::ClockControls(
    lv_obj_t* container,
    Network::WebSocketServiceInterface* wsService,
    const Config::Clock& config,
    DisplayDimensionsGetter dimensionsGetter)
    : ScenarioControlsBase(container, wsService, "clock"),
      dimensionsGetter_(std::move(dimensionsGetter))
{
    // Create widgets.
    createWidgets();

    // Initialize widget states from config.
    updateFromConfig(config);

    // Finish initialization - allow callbacks to send updates now.
    finishInitialization();

    spdlog::info("ClockControls: Initialized");
}

ClockControls::~ClockControls()
{
    // Base class handles container deletion.
    spdlog::info("ClockControls: Destroyed");
}

void ClockControls::createWidgets()
{
    // Create view controller.
    viewController_ = std::make_unique<PanelViewController>(controlsContainer_);

    // Create main view.
    lv_obj_t* mainView = viewController_->createView("main");
    createMainView(mainView);

    // Create font selection view.
    lv_obj_t* fontView = viewController_->createView("font");
    createFontSelectionView(fontView);

    // Create timezone selection view.
    lv_obj_t* timezoneView = viewController_->createView("timezone");
    createTimezoneSelectionView(timezoneView);

    // Create digit material selection view.
    lv_obj_t* digitMaterialView = viewController_->createView("digit_material");
    createDigitMaterialSelectionView(digitMaterialView);

    // Show main view initially.
    viewController_->showView("main");
}

void ClockControls::createMainView(lv_obj_t* view)
{
    // Font selector button.
    auto currentFont = static_cast<Config::ClockFont>(currentFontIndex_);
    std::string fontText = std::string("Font: ") + Config::getDisplayName(currentFont);

    fontButton_ = LVGLBuilder::actionButton(view)
                      .text(fontText.c_str())
                      .icon(LV_SYMBOL_RIGHT)
                      .width(LV_PCT(95))
                      .height(LVGLBuilder::Style::ACTION_SIZE)
                      .layoutRow()
                      .alignLeft()
                      .callback(onFontButtonClicked, this)
                      .buildOrLog();

    // Timezone selector button.
    std::string timezoneText =
        std::string("Timezone: ") + ClockScenario::TIMEZONES[currentTimezoneIndex_].label;

    timezoneButton_ = LVGLBuilder::actionButton(view)
                          .text(timezoneText.c_str())
                          .icon(LV_SYMBOL_RIGHT)
                          .width(LV_PCT(95))
                          .height(LVGLBuilder::Style::ACTION_SIZE)
                          .layoutRow()
                          .alignLeft()
                          .callback(onTimezoneButtonClicked, this)
                          .buildOrLog();

    // Digit material selector button.
    std::string materialText = std::string("Digit Color: ")
        + toString(static_cast<Material::EnumType>(currentMaterialIndex_));

    digitMaterialButton_ = LVGLBuilder::actionButton(view)
                               .text(materialText.c_str())
                               .icon(LV_SYMBOL_RIGHT)
                               .width(LV_PCT(95))
                               .height(LVGLBuilder::Style::ACTION_SIZE)
                               .layoutRow()
                               .alignLeft()
                               .callback(onDigitMaterialButtonClicked, this)
                               .buildOrLog();

    // Digit emissiveness stepper.
    emissivenessStepper_ = LVGLBuilder::actionStepper(view)
                               .label("Glow")
                               .range(0, 20)
                               .step(1)
                               .value(10)
                               .valueFormat("%.0f")
                               .width(LV_PCT(95))
                               .callback(onEmissivenessChanged, this)
                               .buildOrLog();

    // Row for Show Seconds and Melt buttons.
    lv_obj_t* secondsRow = lv_obj_create(view);
    lv_obj_set_size(secondsRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(secondsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        secondsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(secondsRow, 4, 0);
    lv_obj_set_style_bg_opa(secondsRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(secondsRow, 0, 0);
    lv_obj_clear_flag(secondsRow, LV_OBJ_FLAG_SCROLLABLE);

    // Show seconds toggle.
    secondsSwitch_ = LVGLBuilder::actionButton(secondsRow)
                         .text("Seconds")
                         .mode(LVGLBuilder::ActionMode::Toggle)
                         .size(80)
                         .checked(true)
                         .glowColor(0x00CC00)
                         .callback(onSecondsToggled, this)
                         .buildOrLog();

    // Melt button (one-shot trigger).
    meltButton_ = LVGLBuilder::actionButton(secondsRow)
                      .text(LV_SYMBOL_WARNING "\nMelt")
                      .size(80)
                      .textColor(0xFFA500) // Construction orange.
                      .glowColor(0xFF4400) // Orange glow for meltdown.
                      .callback(onMeltClicked, this)
                      .buildOrLog();

    // Event toggle row.
    lv_obj_t* eventRow = lv_obj_create(view);
    lv_obj_set_size(eventRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(eventRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        eventRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(eventRow, 4, 0);
    lv_obj_set_style_bg_opa(eventRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(eventRow, 0, 0);
    lv_obj_clear_flag(eventRow, LV_OBJ_FLAG_SCROLLABLE);

    // Rain event toggle.
    rainSwitch_ = LVGLBuilder::actionButton(eventRow)
                      .text(LV_SYMBOL_TINT "\nRain")
                      .mode(LVGLBuilder::ActionMode::Toggle)
                      .size(80)
                      .checked(false)
                      .textColor(0x0088FF) // Blue text.
                      .glowColor(0x0088FF) // Blue glow for water/rain.
                      .callback(onRainToggled, this)
                      .buildOrLog();

    // Duck event toggle.
    duckSwitch_ = LVGLBuilder::actionButton(eventRow)
                      .text("Duck")
                      .mode(LVGLBuilder::ActionMode::Toggle)
                      .size(80)
                      .checked(false)
                      .glowColor(0xFFCC00) // Yellow glow for duck.
                      .callback(onDuckToggled, this)
                      .buildOrLog();

    // Color cycle row.
    lv_obj_t* cycleRow = lv_obj_create(view);
    lv_obj_set_size(cycleRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cycleRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        cycleRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cycleRow, 4, 0);
    lv_obj_set_style_bg_opa(cycleRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cycleRow, 0, 0);
    lv_obj_clear_flag(cycleRow, LV_OBJ_FLAG_SCROLLABLE);

    // Color cycle event toggle.
    colorCycleSwitch_ = LVGLBuilder::actionButton(cycleRow)
                            .text("Cycle")
                            .mode(LVGLBuilder::ActionMode::Toggle)
                            .size(80)
                            .checked(false)
                            .textColor(0xFF00FF) // Magenta text.
                            .glowColor(0xFF00FF) // Magenta glow for color cycling.
                            .callback(onColorCycleToggled, this)
                            .buildOrLog();

    // Color showcase event toggle.
    colorShowcaseSwitch_ = LVGLBuilder::actionButton(cycleRow)
                               .text("Showcase")
                               .mode(LVGLBuilder::ActionMode::Toggle)
                               .size(80)
                               .checked(false)
                               .textColor(0x00FFFF) // Cyan text.
                               .glowColor(0x00FFFF) // Cyan glow for showcase.
                               .callback(onColorShowcaseToggled, this)
                               .buildOrLog();

    // Marquee row.
    lv_obj_t* marqueeRow = lv_obj_create(view);
    lv_obj_set_size(marqueeRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(marqueeRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        marqueeRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(marqueeRow, 4, 0);
    lv_obj_set_style_bg_opa(marqueeRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(marqueeRow, 0, 0);
    lv_obj_clear_flag(marqueeRow, LV_OBJ_FLAG_SCROLLABLE);

    // Digit slide event toggle.
    digitSlideSwitch_ = LVGLBuilder::actionButton(marqueeRow)
                            .text("Slide")
                            .mode(LVGLBuilder::ActionMode::Toggle)
                            .size(80)
                            .checked(false)
                            .textColor(0xFFAA00) // Orange text.
                            .glowColor(0xFFAA00) // Orange glow for slide effect.
                            .callback(onDigitSlideToggled, this)
                            .buildOrLog();

    // Marquee event toggle.
    marqueeSwitch_ = LVGLBuilder::actionButton(marqueeRow)
                         .text("Marquee")
                         .mode(LVGLBuilder::ActionMode::Toggle)
                         .size(80)
                         .checked(false)
                         .textColor(0x44FF44) // Green text.
                         .glowColor(0x44FF44) // Green glow for marquee scroll.
                         .callback(onMarqueeToggled, this)
                         .buildOrLog();
}

void ClockControls::createFontSelectionView(lv_obj_t* view)
{
    // Back button.
    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onFontBackClicked, this)
        .buildOrLog();

    // Title.
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Font");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    // Font option buttons - iterate using reflection.
    buttonToFontIndex_.clear();

    for (const auto& [value, name] : reflect::enumerators<Config::ClockFont>) {
        auto font = static_cast<Config::ClockFont>(value);
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(Config::getDisplayName(font))
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();

        if (container) {
            lv_obj_t* button = lv_obj_get_child(container, 0);
            if (button) {
                buttonToFontIndex_[button] = static_cast<int>(value);
                lv_obj_add_event_cb(button, onFontSelected, LV_EVENT_CLICKED, this);
            }
        }
    }
}

void ClockControls::createTimezoneSelectionView(lv_obj_t* view)
{
    // Back button.
    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onTimezoneBackClicked, this)
        .buildOrLog();

    // Title.
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Timezone");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    // Timezone option buttons.
    buttonToTimezoneIndex_.clear();

    for (size_t i = 0; i < ClockScenario::TIMEZONES.size(); i++) {
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(ClockScenario::TIMEZONES[i].label)
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();

        if (container) {
            lv_obj_t* button = lv_obj_get_child(container, 0);
            if (button) {
                buttonToTimezoneIndex_[button] = static_cast<int>(i);
                lv_obj_add_event_cb(button, onTimezoneSelected, LV_EVENT_CLICKED, this);
            }
        }
    }
}

void ClockControls::createDigitMaterialSelectionView(lv_obj_t* view)
{
    // Back button.
    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onDigitMaterialBackClicked, this)
        .buildOrLog();

    // Title.
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Digit Color");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    // Material option buttons (all material types).
    buttonToMaterialIndex_.clear();

    // All material types in enum order.
    static const Material::EnumType materials[] = {
        Material::EnumType::Air,   Material::EnumType::Dirt, Material::EnumType::Leaf,
        Material::EnumType::Metal, Material::EnumType::Root, Material::EnumType::Sand,
        Material::EnumType::Seed,  Material::EnumType::Wall, Material::EnumType::Water,
        Material::EnumType::Wood
    };

    for (Material::EnumType mat : materials) {
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(toString(mat).c_str())
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();

        if (container) {
            lv_obj_t* button = lv_obj_get_child(container, 0);
            if (button) {
                buttonToMaterialIndex_[button] = static_cast<int>(mat);
                lv_obj_add_event_cb(button, onDigitMaterialSelected, LV_EVENT_CLICKED, this);
            }
        }
    }
}

void ClockControls::updateFromConfig(const ScenarioConfig& configVariant)
{
    // Extract Config::Clock from variant.
    if (!std::holds_alternative<Config::Clock>(configVariant)) {
        spdlog::error("ClockControls: Invalid config type (expected Config::Clock)");
        return;
    }

    const Config::Clock& config = std::get<Config::Clock>(configVariant);
    spdlog::debug(
        "ClockControls: updateFromConfig called - font={}, timezoneIndex={}",
        static_cast<int>(config.font),
        config.timezoneIndex);

    // Prevent sending updates back to server during UI sync.
    bool wasInitializing = isInitializing();
    if (!wasInitializing) {
        initializing_ = true;
    }

    // Update font selection and button text.
    currentFontIndex_ = static_cast<int>(config.font);
    if (fontButton_) {
        std::string fontText = std::string("Font: ") + Config::getDisplayName(config.font);

        lv_obj_t* button = lv_obj_get_child(fontButton_, 0);
        if (button) {
            lv_obj_t* label = lv_obj_get_child(button, 1); // Second child is text.
            if (label) {
                lv_label_set_text(label, fontText.c_str());
            }
        }
        LOG_DEBUG(Controls, "ClockControls: Updated font to index {}", currentFontIndex_);
    }

    // Update timezone selection and button text.
    currentTimezoneIndex_ = config.timezoneIndex;
    if (timezoneButton_) {
        std::string timezoneText =
            std::string("Timezone: ") + ClockScenario::TIMEZONES[currentTimezoneIndex_].label;

        lv_obj_t* button = lv_obj_get_child(timezoneButton_, 0);
        if (button) {
            lv_obj_t* label = lv_obj_get_child(button, 1); // Second child is text.
            if (label) {
                lv_label_set_text(label, timezoneText.c_str());
            }
        }
        LOG_DEBUG(Controls, "ClockControls: Updated timezone to index {}", currentTimezoneIndex_);
    }

    // Update seconds button.
    if (secondsSwitch_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(secondsSwitch_, config.showSeconds);
        LOG_DEBUG(Controls, "ClockControls: Updated seconds button to {}", config.showSeconds);
    }

    // Update color cycle button.
    if (colorCycleSwitch_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(colorCycleSwitch_, config.colorCycleEnabled);
        LOG_DEBUG(
            Controls, "ClockControls: Updated color cycle button to {}", config.colorCycleEnabled);
    }

    // Update color showcase button.
    if (colorShowcaseSwitch_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(
            colorShowcaseSwitch_, config.colorShowcaseEnabled);
        LOG_DEBUG(
            Controls,
            "ClockControls: Updated color showcase button to {}",
            config.colorShowcaseEnabled);
    }

    // Update rain button.
    if (rainSwitch_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(rainSwitch_, config.rainEnabled);
        LOG_DEBUG(Controls, "ClockControls: Updated rain button to {}", config.rainEnabled);
    }

    // Update duck button.
    if (duckSwitch_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(duckSwitch_, config.duckEnabled);
        LOG_DEBUG(Controls, "ClockControls: Updated duck button to {}", config.duckEnabled);
    }

    // Update digit slide button.
    if (digitSlideSwitch_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(digitSlideSwitch_, config.digitSlideEnabled);
        LOG_DEBUG(
            Controls, "ClockControls: Updated digit slide button to {}", config.digitSlideEnabled);
    }

    // Update marquee button.
    if (marqueeSwitch_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(marqueeSwitch_, config.marqueeEnabled);
        LOG_DEBUG(Controls, "ClockControls: Updated marquee button to {}", config.marqueeEnabled);
    }

    // Update digit material selection and button text.
    currentMaterialIndex_ = static_cast<int>(config.digitMaterial);
    if (digitMaterialButton_) {
        std::string materialText = std::string("Digit Color: ") + toString(config.digitMaterial);

        lv_obj_t* button = lv_obj_get_child(digitMaterialButton_, 0);
        if (button) {
            lv_obj_t* label = lv_obj_get_child(button, 1); // Second child is text.
            if (label) {
                lv_label_set_text(label, materialText.c_str());
            }
        }
        LOG_DEBUG(
            Controls,
            "ClockControls: Updated digit material to {}",
            toString(config.digitMaterial));
    }

    // Update emissiveness stepper.
    if (emissivenessStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(emissivenessStepper_, config.digitEmissiveness);
        LOG_DEBUG(Controls, "ClockControls: Updated emissiveness to {}", config.digitEmissiveness);
    }

    // Cache current config.
    currentConfig_ = config;

    // Restore initializing state.
    if (!wasInitializing) {
        initializing_ = false;
    }
}

Config::Clock ClockControls::getCurrentConfig() const
{
    // Start with current config (preserves auto-scale settings).
    Config::Clock config = currentConfig_;

    // Get font from current selection.
    config.font = static_cast<Config::ClockFont>(currentFontIndex_);

    // Get timezone index from current selection.
    config.timezoneIndex = static_cast<uint8_t>(currentTimezoneIndex_);

    // Get digit material from current selection.
    config.digitMaterial = static_cast<Material::EnumType>(currentMaterialIndex_);

    // Get emissiveness from stepper.
    if (emissivenessStepper_) {
        config.digitEmissiveness =
            static_cast<uint8_t>(LVGLBuilder::ActionStepperBuilder::getValue(emissivenessStepper_));
    }

    // Get showSeconds from button.
    if (secondsSwitch_) {
        config.showSeconds = LVGLBuilder::ActionButtonBuilder::isChecked(secondsSwitch_);
    }

    // Get color cycle enabled from button.
    if (colorCycleSwitch_) {
        config.colorCycleEnabled = LVGLBuilder::ActionButtonBuilder::isChecked(colorCycleSwitch_);
    }

    // Get color showcase enabled from button.
    if (colorShowcaseSwitch_) {
        config.colorShowcaseEnabled =
            LVGLBuilder::ActionButtonBuilder::isChecked(colorShowcaseSwitch_);
    }

    // Get rain enabled from button.
    if (rainSwitch_) {
        config.rainEnabled = LVGLBuilder::ActionButtonBuilder::isChecked(rainSwitch_);
    }

    // Get duck enabled from button.
    if (duckSwitch_) {
        config.duckEnabled = LVGLBuilder::ActionButtonBuilder::isChecked(duckSwitch_);
    }

    // Get digit slide enabled from button.
    if (digitSlideSwitch_) {
        config.digitSlideEnabled = LVGLBuilder::ActionButtonBuilder::isChecked(digitSlideSwitch_);
    }

    // Get marquee enabled from button.
    if (marqueeSwitch_) {
        config.marqueeEnabled = LVGLBuilder::ActionButtonBuilder::isChecked(marqueeSwitch_);
    }

    // Populate display dimensions from getter for auto-scaling.
    if (dimensionsGetter_) {
        DisplayDimensions dims = dimensionsGetter_();
        config.targetDisplayWidth = dims.width;
        config.targetDisplayHeight = dims.height;
        config.autoScale = true;
        LOG_DEBUG(
            Controls,
            "ClockControls: Setting display dimensions {}x{} for auto-scale",
            dims.width,
            dims.height);
    }

    return config;
}

void ClockControls::onFontButtonClicked(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    LOG_DEBUG(Controls, "ClockControls: Font button clicked");
    self->viewController_->showView("font");
}

void ClockControls::onFontSelected(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Look up font index from button mapping.
    auto it = self->buttonToFontIndex_.find(btn);
    if (it == self->buttonToFontIndex_.end()) {
        LOG_ERROR(Controls, "ClockControls: Unknown font button clicked");
        return;
    }

    int fontIndex = it->second;
    auto font = static_cast<Config::ClockFont>(fontIndex);
    LOG_INFO(
        Controls,
        "ClockControls: Font changed to index {} ({})",
        fontIndex,
        Config::getDisplayName(font));

    // Update selection and button text.
    self->currentFontIndex_ = fontIndex;
    if (self->fontButton_) {
        std::string fontText = std::string("Font: ") + Config::getDisplayName(font);
        lv_obj_t* button = lv_obj_get_child(self->fontButton_, 0);
        if (button) {
            lv_obj_t* label = lv_obj_get_child(button, 1);
            if (label) {
                lv_label_set_text(label, fontText.c_str());
            }
        }
    }

    // Return to main view.
    if (self->viewController_) {
        self->viewController_->showView("main");
    }

    // Send config update.
    Config::Clock config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void ClockControls::onFontBackClicked(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    LOG_DEBUG(Controls, "ClockControls: Font back button clicked");
    self->viewController_->showView("main");
}

void ClockControls::onTimezoneButtonClicked(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    LOG_DEBUG(Controls, "ClockControls: Timezone button clicked");
    self->viewController_->showView("timezone");
}

void ClockControls::onTimezoneSelected(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Look up timezone index from button mapping.
    auto it = self->buttonToTimezoneIndex_.find(btn);
    if (it == self->buttonToTimezoneIndex_.end()) {
        LOG_ERROR(Controls, "ClockControls: Unknown timezone button clicked");
        return;
    }

    int timezoneIndex = it->second;
    LOG_INFO(
        Controls,
        "ClockControls: Timezone changed to index {} ({})",
        timezoneIndex,
        ClockScenario::TIMEZONES[timezoneIndex].label);

    // Update selection and button text.
    self->currentTimezoneIndex_ = timezoneIndex;
    if (self->timezoneButton_) {
        std::string timezoneText =
            std::string("Timezone: ") + ClockScenario::TIMEZONES[timezoneIndex].label;
        lv_obj_t* button = lv_obj_get_child(self->timezoneButton_, 0);
        if (button) {
            lv_obj_t* label = lv_obj_get_child(button, 1);
            if (label) {
                lv_label_set_text(label, timezoneText.c_str());
            }
        }
    }

    // Return to main view.
    if (self->viewController_) {
        self->viewController_->showView("main");
    }

    // Send config update.
    Config::Clock config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void ClockControls::onTimezoneBackClicked(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    LOG_DEBUG(Controls, "ClockControls: Timezone back button clicked");
    self->viewController_->showView("main");
}

void ClockControls::onDigitMaterialButtonClicked(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    LOG_DEBUG(Controls, "ClockControls: Digit material button clicked");
    self->viewController_->showView("digit_material");
}

void ClockControls::onDigitMaterialSelected(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Look up material index from button mapping.
    auto it = self->buttonToMaterialIndex_.find(btn);
    if (it == self->buttonToMaterialIndex_.end()) {
        LOG_ERROR(Controls, "ClockControls: Unknown digit material button clicked");
        return;
    }

    int materialIndex = it->second;
    Material::EnumType material = static_cast<Material::EnumType>(materialIndex);
    LOG_INFO(
        Controls,
        "ClockControls: Digit material changed to {} ({})",
        materialIndex,
        toString(material));

    // Update selection and button text.
    self->currentMaterialIndex_ = materialIndex;
    if (self->digitMaterialButton_) {
        std::string materialText = std::string("Digit Color: ") + toString(material);
        lv_obj_t* button = lv_obj_get_child(self->digitMaterialButton_, 0);
        if (button) {
            lv_obj_t* label = lv_obj_get_child(button, 1);
            if (label) {
                lv_label_set_text(label, materialText.c_str());
            }
        }
    }

    // Return to main view.
    if (self->viewController_) {
        self->viewController_->showView("main");
    }

    // Send config update.
    Config::Clock config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void ClockControls::onDigitMaterialBackClicked(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    LOG_DEBUG(Controls, "ClockControls: Digit material back button clicked");
    self->viewController_->showView("main");
}

void ClockControls::onEmissivenessChanged(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("ClockControls: onEmissivenessChanged called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("ClockControls: Ignoring emissiveness change during initialization");
        return;
    }

    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->emissivenessStepper_);
    spdlog::info("ClockControls: Emissiveness changed to {}", value);

    // Get complete current config and send update.
    Config::Clock config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void ClockControls::onSecondsToggled(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("ClockControls: onSecondsToggled called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("ClockControls: Ignoring seconds toggle during initialization");
        return;
    }

    // Get current state from ActionButton.
    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->secondsSwitch_);

    spdlog::info("ClockControls: Show seconds toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config and send update.
    Config::Clock config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void ClockControls::onMeltClicked(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("ClockControls: onMeltClicked called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("ClockControls: Ignoring melt click during initialization");
        return;
    }

    spdlog::info("ClockControls: Melt button clicked - triggering meltdown");

    // Get current config and set meltdown trigger.
    Config::Clock config = self->getCurrentConfig();
    config.meltdownEnabled = true;
    self->sendConfigUpdate(config);
}

void ClockControls::onColorCycleToggled(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("ClockControls: onColorCycleToggled called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("ClockControls: Ignoring color cycle toggle during initialization");
        return;
    }

    // Get current state from ActionButton.
    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->colorCycleSwitch_);

    spdlog::info("ClockControls: Color cycle toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config and send update.
    Config::Clock config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void ClockControls::onColorShowcaseToggled(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("ClockControls: onColorShowcaseToggled called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("ClockControls: Ignoring color showcase toggle during initialization");
        return;
    }

    // Get current state from ActionButton.
    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->colorShowcaseSwitch_);

    spdlog::info("ClockControls: Color showcase toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config and send update.
    Config::Clock config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void ClockControls::onDigitSlideToggled(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("ClockControls: onDigitSlideToggled called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("ClockControls: Ignoring digit slide toggle during initialization");
        return;
    }

    // Get current state from ActionButton.
    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->digitSlideSwitch_);

    spdlog::info("ClockControls: Digit slide toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config and send update.
    Config::Clock config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void ClockControls::onMarqueeToggled(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("ClockControls: onMarqueeToggled called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("ClockControls: Ignoring marquee toggle during initialization");
        return;
    }

    // Get current state from ActionButton.
    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->marqueeSwitch_);

    spdlog::info("ClockControls: Marquee toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config and send update.
    Config::Clock config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void ClockControls::onRainToggled(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("ClockControls: onRainToggled called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("ClockControls: Ignoring rain toggle during initialization");
        return;
    }

    // Get current state from ActionButton.
    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->rainSwitch_);

    spdlog::info("ClockControls: Rain toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config and send update.
    Config::Clock config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void ClockControls::onDuckToggled(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("ClockControls: onDuckToggled called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("ClockControls: Ignoring duck toggle during initialization");
        return;
    }

    // Get current state from ActionButton.
    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->duckSwitch_);

    spdlog::info("ClockControls: Duck toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config and send update.
    Config::Clock config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

} // namespace Ui
} // namespace DirtSim
