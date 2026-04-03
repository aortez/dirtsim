#include "SearchIdleView.h"
#include "core/Assert.h"
#include "ui/controls/MazeSearchView.h"

namespace DirtSim {
namespace Ui {
namespace {

constexpr int kMazeHeight = 23;
constexpr int kMazeWidth = 39;

lv_obj_t* createOverlayCard(
    lv_obj_t* parent,
    int padHorizontal,
    int padVertical,
    lv_align_t align,
    int offsetX,
    int offsetY)
{
    lv_obj_t* card = lv_obj_create(parent);
    DIRTSIM_ASSERT(card, "SearchIdleView failed to create overlay card");

    lv_obj_set_size(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0B1116), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_80, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x425E71), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_70, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_outline_width(card, 0, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_hor(card, padHorizontal, 0);
    lv_obj_set_style_pad_ver(card, padVertical, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(card, align, offsetX, offsetY);
    return card;
}

} // namespace

SearchIdleView::SearchIdleView(lv_obj_t* parent, IconRail& iconRail)
    : animator_(kMazeWidth, kMazeHeight), iconRail_(iconRail)
{
    createUi(parent);
}

SearchIdleView::~SearchIdleView()
{
    iconView_.reset();
    mazeView_.reset();
    iconRail_.clearCustomIconContent(IconId::SCANNER);

    if (contentRoot_) {
        lv_obj_del(contentRoot_);
        contentRoot_ = nullptr;
        contentViewport_ = nullptr;
    }
}

void SearchIdleView::setLastError(const std::optional<std::string>& error)
{
    lastError_ = error;
    updateErrorVisibility();
}

void SearchIdleView::updateAnimations()
{
    layoutContentViewport();
    animator_.advanceTick();

    if (mazeView_) {
        mazeView_->render();
    }
    if (iconView_) {
        iconView_->render();
    }
}

void SearchIdleView::createUi(lv_obj_t* parent)
{
    DIRTSIM_ASSERT(parent, "SearchIdleView requires a parent");

    contentRoot_ = lv_obj_create(parent);
    DIRTSIM_ASSERT(contentRoot_, "SearchIdleView failed to create root");
    lv_obj_set_size(contentRoot_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(contentRoot_, lv_color_hex(0x06090D), 0);
    lv_obj_set_style_bg_opa(contentRoot_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(contentRoot_, 0, 0);
    lv_obj_set_style_pad_all(contentRoot_, 0, 0);
    lv_obj_clear_flag(contentRoot_, LV_OBJ_FLAG_SCROLLABLE);

    contentViewport_ = lv_obj_create(contentRoot_);
    DIRTSIM_ASSERT(contentViewport_, "SearchIdleView failed to create content viewport");
    lv_obj_set_style_bg_opa(contentViewport_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(contentViewport_, 0, 0);
    lv_obj_set_style_pad_all(contentViewport_, 0, 0);
    lv_obj_clear_flag(contentViewport_, LV_OBJ_FLAG_SCROLLABLE);
    layoutContentViewport();

    mazeView_ = std::make_unique<MazeSearchView>(
        contentViewport_,
        animator_,
        MazeSearchView::ViewportMode::FullMaze,
        MazeSearchView::PresentationStyle::Scene);

    titleCard_ = createOverlayCard(contentViewport_, 18, 8, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_t* titleLabel = lv_label_create(titleCard_);
    DIRTSIM_ASSERT(titleLabel, "SearchIdleView failed to create title label");
    lv_label_set_text(titleLabel, "Search");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xF4FBFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_letter_space(titleLabel, 1, 0);

    errorCard_ = createOverlayCard(contentViewport_, 18, 12, LV_ALIGN_BOTTOM_MID, 0, -20);
    errorLabel_ = lv_label_create(errorCard_);
    DIRTSIM_ASSERT(errorLabel_, "SearchIdleView failed to create error label");
    lv_obj_set_width(errorLabel_, 580);
    lv_label_set_long_mode(errorLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(errorLabel_, lv_color_hex(0xFFB3B3), 0);
    lv_obj_set_style_text_align(errorLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(errorLabel_, &lv_font_montserrat_16, 0);

    if (lv_obj_t* scannerIconHost = iconRail_.activateCustomIconContent(IconId::SCANNER)) {
        iconView_ = std::make_unique<MazeSearchView>(
            scannerIconHost,
            animator_,
            MazeSearchView::ViewportMode::CenteredSquare,
            MazeSearchView::PresentationStyle::IconBadge);
    }

    updateErrorVisibility();
}

void SearchIdleView::layoutContentViewport()
{
    if (!contentRoot_ || !contentViewport_) {
        return;
    }

    int rootWidth = lv_obj_get_width(contentRoot_);
    int rootHeight = lv_obj_get_height(contentRoot_);
    if (rootWidth <= 0 || rootHeight <= 0) {
        if (lv_disp_t* display = lv_disp_get_default()) {
            rootWidth = lv_disp_get_hor_res(display);
            rootHeight = lv_disp_get_ver_res(display);
        }
    }
    if (rootWidth <= 0 || rootHeight <= 0) {
        return;
    }

    int railWidth = IconRail::RAIL_WIDTH;
    if (lv_obj_t* railContainer = iconRail_.getContainer()) {
        const int measuredRailWidth = lv_obj_get_width(railContainer);
        if (measuredRailWidth > 0) {
            railWidth = measuredRailWidth;
        }
    }

    const int viewportWidth = std::max(0, rootWidth - railWidth);
    lv_obj_set_pos(contentViewport_, railWidth, 0);
    lv_obj_set_size(contentViewport_, viewportWidth, rootHeight);
}

void SearchIdleView::updateErrorVisibility()
{
    DIRTSIM_ASSERT(errorCard_, "SearchIdleView error card must exist");
    DIRTSIM_ASSERT(errorLabel_, "SearchIdleView error label must exist");

    if (!lastError_.has_value() || lastError_->empty()) {
        lv_obj_add_flag(errorCard_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(errorLabel_, "");
        return;
    }

    lv_obj_clear_flag(errorCard_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(errorLabel_, lastError_->c_str());
}

} // namespace Ui
} // namespace DirtSim
