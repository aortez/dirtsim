#include "TrainingIdleView.h"
#include "UiComponentManager.h"
#include "controls/EvolutionControls.h"
#include "controls/ExpandablePanel.h"
#include "controls/GenomeBrowserPanel.h"
#include "controls/IconRail.h"
#include "controls/TrainingConfigPanel.h"
#include "controls/TrainingResultBrowserPanel.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include <algorithm>
#include <lvgl/lvgl.h>

namespace DirtSim {
namespace Ui {

namespace {
constexpr int kBrowserRightGap = 60;

int computeBrowserPanelWidth()
{
    const int displayWidth = lv_disp_get_hor_res(lv_disp_get_default());
    const int maxWidth =
        displayWidth > 0 ? displayWidth - IconRail::RAIL_WIDTH - kBrowserRightGap : 0;
    int panelWidth = ExpandablePanel::DefaultWidth * 2;
    if (maxWidth > 0) {
        panelWidth = std::min(panelWidth, maxWidth);
    }
    if (panelWidth < ExpandablePanel::DefaultWidth) {
        panelWidth = ExpandablePanel::DefaultWidth;
    }
    return panelWidth;
}

} // namespace

TrainingIdleView::TrainingIdleView(
    UiComponentManager* uiManager,
    EventSink& eventSink,
    Network::WebSocketServiceInterface* wsService,
    UserSettings& userSettings,
    const Starfield::Snapshot* starfieldSnapshot)
    : uiManager_(uiManager),
      eventSink_(eventSink),
      wsService_(wsService),
      userSettings_(userSettings),
      starfieldSnapshot_(starfieldSnapshot)
{
    createUI();
}

TrainingIdleView::~TrainingIdleView()
{
    destroyUI();
}

void TrainingIdleView::createUI()
{
    DIRTSIM_ASSERT(uiManager_, "TrainingIdleView requires valid UiComponentManager");

    container_ = uiManager_->getWorldDisplayArea();
    DIRTSIM_ASSERT(container_, "Failed to get world display area");

    lv_obj_clean(container_);
    lv_obj_update_layout(container_);

    int displayWidth = lv_obj_get_width(container_);
    int displayHeight = lv_obj_get_height(container_);
    if (displayWidth <= 0 || displayHeight <= 0) {
        lv_disp_t* display = lv_disp_get_default();
        if (display) {
            displayWidth = lv_disp_get_hor_res(display);
            displayHeight = lv_disp_get_ver_res(display);
        }
    }

    starfield_ =
        std::make_unique<Starfield>(container_, displayWidth, displayHeight, starfieldSnapshot_);

    createIdleUI();
}

void TrainingIdleView::createIdleUI()
{
    contentRow_ = lv_obj_create(container_);
    lv_obj_set_size(contentRow_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(contentRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(contentRow_, 0, 0);
    lv_obj_set_style_pad_all(contentRow_, 0, 0);
    lv_obj_set_style_pad_gap(contentRow_, 10, 0);
    lv_obj_set_flex_flow(contentRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        contentRow_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(contentRow_, LV_OBJ_FLAG_SCROLLABLE);

    panel_ = std::make_unique<ExpandablePanel>(contentRow_);
    panel_->setWidth(ExpandablePanel::DefaultWidth);
    panelContent_ = panel_->getContentArea();

    idleSpacer_ = lv_obj_create(contentRow_);
    lv_obj_set_size(idleSpacer_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(idleSpacer_, 1);
    lv_obj_set_style_bg_opa(idleSpacer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(idleSpacer_, 0, 0);
    lv_obj_clear_flag(idleSpacer_, LV_OBJ_FLAG_SCROLLABLE);

    updateIconRailOffset();

    LOG_INFO(Controls, "Training idle UI created with panel column only");
}

void TrainingIdleView::destroyUI()
{
    clearPanelContent();
    starfield_.reset();
    panel_.reset();

    if (container_) {
        lv_obj_clean(container_);
    }

    container_ = nullptr;
    contentRow_ = nullptr;
    idleSpacer_ = nullptr;
    panelContent_ = nullptr;
}

void TrainingIdleView::updateAnimations()
{
    if (starfield_ && starfield_->isVisible()) {
        starfield_->update();
    }
}

void TrainingIdleView::hidePanel()
{
    if (panel_) {
        panel_->hide();
    }
}

void TrainingIdleView::showPanel()
{
    if (panel_) {
        panel_->show();
    }
}

void TrainingIdleView::clearPanelContent()
{
    evolutionControls_.reset();
    genomeBrowserPanel_.reset();
    trainingConfigPanel_.reset();
    trainingResultBrowserPanel_.reset();
    if (panel_) {
        panel_->clearContent();
        panel_->setWidth(ExpandablePanel::DefaultWidth);
    }
}

Starfield::Snapshot TrainingIdleView::captureStarfieldSnapshot() const
{
    DIRTSIM_ASSERT(starfield_, "TrainingIdleView requires Starfield");
    return starfield_->capture();
}

void TrainingIdleView::createCorePanel()
{
    if (!panel_) {
        LOG_ERROR(Controls, "TrainingIdleView: No training panel available");
        return;
    }
    panel_->setWidth(ExpandablePanel::DefaultWidth);

    if (!panelContent_) {
        LOG_ERROR(Controls, "TrainingIdleView: No panel content area available");
        return;
    }

    evolutionControls_ = std::make_unique<EvolutionControls>(
        panelContent_, eventSink_, evolutionStarted_, userSettings_.trainingSpec);
    LOG_INFO(Controls, "TrainingIdleView: Created Training Home panel");
}

void TrainingIdleView::createGenomeBrowserPanel()
{
    createGenomeBrowserPanelInternal();
}

void TrainingIdleView::createGenomeBrowserPanelInternal()
{
    if (!panel_) {
        LOG_ERROR(Controls, "TrainingIdleView: No training panel available");
        return;
    }
    panel_->setWidth(computeBrowserPanelWidth());

    if (!panelContent_) {
        LOG_ERROR(Controls, "TrainingIdleView: No panel content area available");
        return;
    }

    genomeBrowserPanel_ =
        std::make_unique<GenomeBrowserPanel>(panelContent_, wsService_, &eventSink_);
    LOG_INFO(Controls, "TrainingIdleView: Created Genome browser panel");
}

void TrainingIdleView::createTrainingConfigPanel()
{
    if (!panel_) {
        LOG_ERROR(Controls, "TrainingIdleView: No training panel available");
        return;
    }
    panel_->setWidth(ExpandablePanel::DefaultWidth);

    if (!panelContent_) {
        LOG_ERROR(Controls, "TrainingIdleView: No panel content area available");
        return;
    }

    trainingConfigPanel_ = std::make_unique<TrainingConfigPanel>(
        panelContent_,
        eventSink_,
        panel_.get(),
        wsService_,
        evolutionStarted_,
        userSettings_.evolutionConfig,
        userSettings_.mutationConfig,
        userSettings_.trainingSpec,
        userSettings_.streamIntervalMs);
    LOG_INFO(Controls, "TrainingIdleView: Created Training config panel");
}

Result<std::monostate, std::string> TrainingIdleView::showTrainingConfigView(
    TrainingConfigView view)
{
    if (!trainingConfigPanel_) {
        return Result<std::monostate, std::string>::error("Training config panel not available");
    }

    TrainingConfigPanel::View panelView = TrainingConfigPanel::View::None;
    switch (view) {
        case TrainingConfigView::None:
            panelView = TrainingConfigPanel::View::None;
            break;
        case TrainingConfigView::Evolution:
            panelView = TrainingConfigPanel::View::Evolution;
            break;
        case TrainingConfigView::Population:
            panelView = TrainingConfigPanel::View::Population;
            break;
    }

    trainingConfigPanel_->showView(panelView);
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void TrainingIdleView::createTrainingResultBrowserPanel()
{
    if (!panel_) {
        LOG_ERROR(Controls, "TrainingIdleView: No training panel available");
        return;
    }
    panel_->setWidth(computeBrowserPanelWidth());

    if (!panelContent_) {
        LOG_ERROR(Controls, "TrainingIdleView: No panel content area available");
        return;
    }

    trainingResultBrowserPanel_ =
        std::make_unique<TrainingResultBrowserPanel>(panelContent_, wsService_);
    LOG_INFO(Controls, "TrainingIdleView: Created Training result browser panel");
}

void TrainingIdleView::setStreamIntervalMs(int value)
{
    userSettings_.streamIntervalMs = value;
    if (trainingConfigPanel_) {
        trainingConfigPanel_->setStreamIntervalMs(value);
    }
}

void TrainingIdleView::setEvolutionStarted(bool started)
{
    evolutionStarted_ = started;

    if (evolutionControls_) {
        evolutionControls_->setEvolutionStarted(started);
    }
    if (trainingConfigPanel_) {
        trainingConfigPanel_->setEvolutionStarted(started);
    }
}

void TrainingIdleView::updateIconRailOffset()
{
    if (!contentRow_) {
        return;
    }

    int leftPadding = 0;
    IconRail* iconRail = uiManager_ ? uiManager_->getIconRail() : nullptr;
    if (iconRail) {
        if (lv_obj_t* railContainer = iconRail->getContainer()) {
            leftPadding = lv_obj_get_width(railContainer);
        }
        if (leftPadding <= 0 && !iconRail->isMinimized()) {
            leftPadding = IconRail::RAIL_WIDTH;
        }
    }
    lv_obj_set_style_pad_left(contentRow_, leftPadding, 0);
}

Result<GenomeId, std::string> TrainingIdleView::openGenomeDetailByIndex(int index)
{
    if (index < 0) {
        return Result<GenomeId, std::string>::error("Genome detail index must be non-negative");
    }

    if (!genomeBrowserPanel_) {
        createGenomeBrowserPanel();
    }
    if (!genomeBrowserPanel_) {
        return Result<GenomeId, std::string>::error("Genome browser panel not available");
    }

    return genomeBrowserPanel_->openDetailByIndex(static_cast<size_t>(index));
}

Result<GenomeId, std::string> TrainingIdleView::openGenomeDetailById(const GenomeId& genomeId)
{
    if (!genomeBrowserPanel_) {
        createGenomeBrowserPanel();
    }
    if (!genomeBrowserPanel_) {
        return Result<GenomeId, std::string>::error("Genome browser panel not available");
    }

    return genomeBrowserPanel_->openDetailById(genomeId);
}

Result<std::monostate, std::string> TrainingIdleView::loadGenomeDetail(const GenomeId& genomeId)
{
    if (!genomeBrowserPanel_) {
        return Result<std::monostate, std::string>::error("Genome browser panel not available");
    }

    return genomeBrowserPanel_->loadDetailForId(genomeId);
}

void TrainingIdleView::addGenomeToTraining(const GenomeId& genomeId, Scenario::EnumType scenarioId)
{
    if (genomeId.isNil()) {
        return;
    }

    if (trainingConfigPanel_) {
        trainingConfigPanel_->addSeedGenome(genomeId, scenarioId);
        return;
    }

    PopulationSpec* targetSpec = nullptr;
    for (auto& spec : userSettings_.trainingSpec.population) {
        if (spec.scenarioId == scenarioId) {
            targetSpec = &spec;
            break;
        }
    }

    if (!targetSpec) {
        PopulationSpec spec;
        spec.scenarioId = scenarioId;
        switch (userSettings_.trainingSpec.organismType) {
            case OrganismType::TREE:
                spec.brainKind = TrainingBrainKind::NeuralNet;
                break;
            case OrganismType::DUCK:
            case OrganismType::GOOSE:
                spec.brainKind = TrainingBrainKind::Random;
                break;
            default:
                spec.brainKind = TrainingBrainKind::Random;
                break;
        }
        spec.count = std::max(1, userSettings_.evolutionConfig.populationSize);
        userSettings_.trainingSpec.population.push_back(spec);
        targetSpec = &userSettings_.trainingSpec.population.back();
    }

    TrainingBrainRegistry registry = TrainingBrainRegistry::createDefault();
    const std::string variant = targetSpec->brainVariant.value_or("");
    const BrainRegistryEntry* entry =
        registry.find(userSettings_.trainingSpec.organismType, targetSpec->brainKind, variant);
    if (!entry || !entry->requiresGenome) {
        LOG_WARN(Controls, "TrainingIdleView: Genome add ignored for non-genome brain");
        return;
    }

    if (std::find(targetSpec->seedGenomes.begin(), targetSpec->seedGenomes.end(), genomeId)
        != targetSpec->seedGenomes.end()) {
        return;
    }

    targetSpec->seedGenomes.push_back(genomeId);
    const int seedCount = static_cast<int>(targetSpec->seedGenomes.size());
    if (targetSpec->count < seedCount) {
        targetSpec->count = seedCount;
    }
    targetSpec->randomCount = targetSpec->count - seedCount;
    int totalPopulation = 0;
    for (const auto& spec : userSettings_.trainingSpec.population) {
        const std::string specVariant = spec.brainVariant.value_or("");
        const BrainRegistryEntry* specEntry =
            registry.find(userSettings_.trainingSpec.organismType, spec.brainKind, specVariant);
        if (specEntry && specEntry->requiresGenome) {
            totalPopulation += static_cast<int>(spec.seedGenomes.size()) + spec.randomCount;
        }
        else {
            totalPopulation += spec.count;
        }
    }
    userSettings_.evolutionConfig.populationSize = totalPopulation;
    if (!userSettings_.trainingSpec.population.empty()) {
        userSettings_.trainingSpec.scenarioId =
            userSettings_.trainingSpec.population.front().scenarioId;
    }
}

bool TrainingIdleView::isTrainingResultModalVisible() const
{
    return false;
}

} // namespace Ui
} // namespace DirtSim
