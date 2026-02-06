#pragma once

#include "core/organisms/evolution/GenomeMetadata.h"
#include "server/api/TrainingResult.h"
#include "ui/rendering/Starfield.h"
#include <memory>
#include <vector>

typedef struct _lv_obj_t lv_obj_t;

typedef struct _lv_event_t lv_event_t;

namespace DirtSim {

namespace Ui {

class EventSink;
class UiComponentManager;

class TrainingUnsavedResultView {
public:
    TrainingUnsavedResultView(
        UiComponentManager* uiManager,
        EventSink& eventSink,
        const Starfield::Snapshot* starfieldSnapshot = nullptr);
    ~TrainingUnsavedResultView();

    void updateAnimations();

    void showTrainingResultModal(
        const Api::TrainingResult::Summary& summary,
        const std::vector<Api::TrainingResult::Candidate>& candidates);
    void hideTrainingResultModal();
    bool isTrainingResultModalVisible() const;
    std::vector<GenomeId> getTrainingResultSaveIds() const;
    std::vector<GenomeId> getTrainingResultSaveIdsForCount(int count) const;
    Starfield::Snapshot captureStarfieldSnapshot() const;

private:
    void createUI();
    void destroyUI();
    void createUnsavedResultUI();
    void updateTrainingResultSaveButton();

    static void onTrainingResultSaveClicked(lv_event_t* e);
    static void onTrainingResultSaveAndRestartClicked(lv_event_t* e);
    static void onTrainingResultDiscardClicked(lv_event_t* e);
    static void onTrainingResultCountChanged(lv_event_t* e);

    UiComponentManager* uiManager_ = nullptr;
    EventSink& eventSink_;
    const Starfield::Snapshot* starfieldSnapshot_ = nullptr;
    std::unique_ptr<Starfield> starfield_;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* contentRow_ = nullptr;

    Api::TrainingResult::Summary trainingResultSummary_{};
    std::vector<Api::TrainingResult::Candidate> primaryCandidates_;
    lv_obj_t* trainingResultOverlay_ = nullptr;
    lv_obj_t* trainingResultCountLabel_ = nullptr;
    lv_obj_t* trainingResultSaveStepper_ = nullptr;
    lv_obj_t* trainingResultSaveButton_ = nullptr;
    lv_obj_t* trainingResultSaveAndRestartButton_ = nullptr;
};

} // namespace Ui
} // namespace DirtSim
