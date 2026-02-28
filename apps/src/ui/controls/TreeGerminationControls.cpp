#include "TreeGerminationControls.h"
#include "core/IconFont.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketServiceInterface.h"
#include "ui/PanelViewController.h"
#include "ui/state-machine/EventSink.h"
#include "ui/state-machine/api/PlantSeed.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

TreeGerminationControls::TreeGerminationControls(
    lv_obj_t* container,
    Network::WebSocketServiceInterface* wsService,
    UserSettingsManager& userSettingsManager,
    EventSink* eventSink,
    const Config::TreeGermination& config)
    : ScenarioControlsBase(container, wsService, userSettingsManager, "tree_germination"),
      eventSink_(eventSink)
{
    createWidgets();
    updateFromConfig(config);
    finishInitialization();
    spdlog::info("TreeGerminationControls: Initialized");
}

TreeGerminationControls::~TreeGerminationControls()
{
    spdlog::info("TreeGerminationControls: Destroyed");
}

void TreeGerminationControls::createWidgets()
{
    viewController_ = std::make_unique<PanelViewController>(controlsContainer_);
    iconFont_ = std::make_unique<IconFont>(32);

    lv_obj_t* mainView = viewController_->createView("main");
    createMainView(mainView);

    lv_obj_t* brainTypeView = viewController_->createView("brain_type");
    createBrainTypeSelectionView(brainTypeView);

    viewController_->showView("main");
}

void TreeGerminationControls::createMainView(lv_obj_t* view)
{
    lv_obj_t* row = lv_obj_create(view);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    plantSeedButton_ = LVGLBuilder::actionButton(row)
                           .text("Plant Seed")
                           .icon(IconFont::SEEDLING)
                           .font(iconFont_->font())
                           .mode(LVGLBuilder::ActionMode::Push)
                           .size(80)
                           .backgroundColor(0x228B22)
                           .callback(onPlantSeedClicked, this)
                           .buildOrLog();

    std::string brainText = std::string("Brain: ")
        + getBrainTypeName(static_cast<Config::TreeBrainType>(currentBrainTypeIndex_));

    brainTypeButton_ = LVGLBuilder::actionButton(view)
                           .text(brainText.c_str())
                           .icon(LV_SYMBOL_RIGHT)
                           .width(LV_PCT(95))
                           .height(LVGLBuilder::Style::ACTION_SIZE)
                           .layoutRow()
                           .alignLeft()
                           .callback(onBrainTypeButtonClicked, this)
                           .buildOrLog();
}

void TreeGerminationControls::createBrainTypeSelectionView(lv_obj_t* view)
{
    // Back button.
    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onBrainTypeBackClicked, this)
        .buildOrLog();

    // Title.
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Brain Type");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    // Brain type options.
    buttonToBrainTypeIndex_.clear();

    static const Config::TreeBrainType brainTypes[] = {
        Config::TreeBrainType::RULE_BASED,
        Config::TreeBrainType::NEURAL_NET,
    };

    for (Config::TreeBrainType type : brainTypes) {
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(getBrainTypeName(type))
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();

        if (container) {
            lv_obj_t* button = lv_obj_get_child(container, 0);
            if (button) {
                buttonToBrainTypeIndex_[button] = static_cast<int>(type);
                lv_obj_add_event_cb(button, onBrainTypeSelected, LV_EVENT_CLICKED, this);
            }
        }
    }
}

void TreeGerminationControls::updateFromConfig(const ScenarioConfig& configVariant)
{
    if (!std::holds_alternative<Config::TreeGermination>(configVariant)) {
        spdlog::error("TreeGerminationControls: Invalid config type");
        return;
    }

    const Config::TreeGermination& config = std::get<Config::TreeGermination>(configVariant);
    LOG_DEBUG(
        Controls,
        "TreeGerminationControls: updateFromConfig - brain_type={}",
        static_cast<int>(config.brain_type));

    bool wasInitializing = isInitializing();
    if (!wasInitializing) {
        initializing_ = true;
    }

    currentBrainTypeIndex_ = static_cast<int>(config.brain_type);
    if (brainTypeButton_) {
        std::string brainText = std::string("Brain: ") + getBrainTypeName(config.brain_type);
        lv_obj_t* button = lv_obj_get_child(brainTypeButton_, 0);
        if (button) {
            lv_obj_t* label = lv_obj_get_child(button, 1);
            if (label) {
                lv_label_set_text(label, brainText.c_str());
            }
        }
        LOG_DEBUG(
            Controls,
            "TreeGerminationControls: Updated brain type to {}",
            getBrainTypeName(config.brain_type));
    }

    currentConfig_ = config;

    if (!wasInitializing) {
        initializing_ = false;
    }
}

Config::TreeGermination TreeGerminationControls::getCurrentConfig() const
{
    Config::TreeGermination config = currentConfig_;
    config.brain_type = static_cast<Config::TreeBrainType>(currentBrainTypeIndex_);
    return config;
}

void TreeGerminationControls::onBrainTypeButtonClicked(lv_event_t* e)
{
    TreeGerminationControls* self =
        static_cast<TreeGerminationControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    LOG_DEBUG(Controls, "TreeGerminationControls: Brain type button clicked");
    self->viewController_->showView("brain_type");
}

void TreeGerminationControls::onBrainTypeSelected(lv_event_t* e)
{
    TreeGerminationControls* self =
        static_cast<TreeGerminationControls*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));

    auto it = self->buttonToBrainTypeIndex_.find(btn);
    if (it == self->buttonToBrainTypeIndex_.end()) {
        LOG_ERROR(Controls, "TreeGerminationControls: Unknown brain type button clicked");
        return;
    }

    int brainTypeIndex = it->second;
    Config::TreeBrainType brainType = static_cast<Config::TreeBrainType>(brainTypeIndex);
    LOG_INFO(
        Controls, "TreeGerminationControls: Brain type changed to {}", getBrainTypeName(brainType));

    self->currentBrainTypeIndex_ = brainTypeIndex;
    if (self->brainTypeButton_) {
        std::string brainText = std::string("Brain: ") + getBrainTypeName(brainType);
        lv_obj_t* button = lv_obj_get_child(self->brainTypeButton_, 0);
        if (button) {
            lv_obj_t* label = lv_obj_get_child(button, 1);
            if (label) {
                lv_label_set_text(label, brainText.c_str());
            }
        }
    }

    if (self->viewController_) {
        self->viewController_->showView("main");
    }

    Config::TreeGermination config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void TreeGerminationControls::onBrainTypeBackClicked(lv_event_t* e)
{
    TreeGerminationControls* self =
        static_cast<TreeGerminationControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    LOG_DEBUG(Controls, "TreeGerminationControls: Brain type back button clicked");
    self->viewController_->showView("main");
}

void TreeGerminationControls::onPlantSeedClicked(lv_event_t* e)
{
    TreeGerminationControls* self =
        static_cast<TreeGerminationControls*>(lv_event_get_user_data(e));
    if (!self) {
        LOG_ERROR(Controls, "TreeGerminationControls: Plant Seed clicked with null self");
        return;
    }

    if (!self->eventSink_) {
        LOG_WARN(Controls, "TreeGerminationControls: Plant Seed clicked without EventSink");
        return;
    }

    UiApi::PlantSeed::Cwc cwc;
    cwc.callback = [](auto&&) {};
    self->eventSink_->queueEvent(cwc);
    LOG_INFO(Controls, "TreeGerminationControls: Queued PlantSeed");
}

const char* TreeGerminationControls::getBrainTypeName(Config::TreeBrainType type)
{
    switch (type) {
        case Config::TreeBrainType::RULE_BASED:
            return "Rule Based";
        case Config::TreeBrainType::NEURAL_NET:
            return "Neural Net";
    }
    return "Unknown";
}

} // namespace Ui
} // namespace DirtSim
