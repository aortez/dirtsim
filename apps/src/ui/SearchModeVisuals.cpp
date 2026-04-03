#include "SearchModeVisuals.h"
#include "core/Assert.h"
#include "ui/controls/MazeSearchView.h"

namespace DirtSim {
namespace Ui {

SearchModeVisuals::SearchModeVisuals(IconRail& iconRail, int mazeWidth, int mazeHeight)
    : animator_(mazeWidth, mazeHeight), iconRail_(iconRail)
{
    if (lv_obj_t* scannerIconHost = iconRail_.activateCustomIconContent(IconId::SCANNER)) {
        iconView_ = std::make_unique<MazeSearchView>(
            scannerIconHost,
            animator_,
            MazeSearchView::ViewportMode::CenteredSquare,
            MazeSearchView::PresentationStyle::IconBadge);
    }
}

SearchModeVisuals::~SearchModeVisuals()
{
    iconView_.reset();
    iconRail_.clearCustomIconContent(IconId::SCANNER);
}

void SearchModeVisuals::updateAnimations()
{
    animator_.advanceTick();

    if (iconView_) {
        iconView_->render();
    }
}

} // namespace Ui
} // namespace DirtSim
