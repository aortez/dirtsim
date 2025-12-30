#include "PanelViewController.h"
#include "core/LoggingChannels.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

PanelViewController::PanelViewController(lv_obj_t* panelContainer)
    : container_(panelContainer), currentViewId_("")
{
    if (!container_) {
        LOG_ERROR(Controls, "PanelViewController: panelContainer is null");
    }
}

PanelViewController::~PanelViewController()
{
    // LVGL will automatically clean up child objects when container is destroyed.
}

lv_obj_t* PanelViewController::createView(const std::string& viewId)
{
    if (!container_) {
        LOG_ERROR(Controls, "PanelViewController: Cannot create view, container is null");
        return nullptr;
    }

    if (viewId.empty()) {
        LOG_ERROR(Controls, "PanelViewController: viewId cannot be empty");
        return nullptr;
    }

    if (views_.find(viewId) != views_.end()) {
        LOG_WARN(Controls, "PanelViewController: View '{}' already exists", viewId);
        return views_[viewId];
    }

    // Create view container.
    lv_obj_t* view = lv_obj_create(container_);
    lv_obj_set_size(view, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(view, 0, 0);
    lv_obj_set_style_pad_row(view, 4, 0);
    lv_obj_set_style_bg_opa(view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view, 0, 0);
    lv_obj_add_flag(view, LV_OBJ_FLAG_HIDDEN); // Start hidden.

    views_[viewId] = view;

    LOG_DEBUG(Controls, "PanelViewController: Created view '{}'", viewId);

    return view;
}

void PanelViewController::showView(const std::string& viewId)
{
    if (!container_) {
        LOG_ERROR(Controls, "PanelViewController: Cannot show view, container is null");
        return;
    }

    if (viewId.empty()) {
        LOG_ERROR(Controls, "PanelViewController: viewId cannot be empty");
        return;
    }

    auto it = views_.find(viewId);
    if (it == views_.end()) {
        LOG_ERROR(Controls, "PanelViewController: View '{}' not found", viewId);
        return;
    }

    // Hide current view.
    if (!currentViewId_.empty()) {
        auto currentIt = views_.find(currentViewId_);
        if (currentIt != views_.end()) {
            lv_obj_add_flag(currentIt->second, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Show target view.
    lv_obj_remove_flag(it->second, LV_OBJ_FLAG_HIDDEN);
    currentViewId_ = viewId;

    LOG_DEBUG(Controls, "PanelViewController: Showing view '{}'", viewId);
}

std::string PanelViewController::getCurrentView() const
{
    return currentViewId_;
}

bool PanelViewController::hasView(const std::string& viewId) const
{
    return views_.find(viewId) != views_.end();
}

lv_obj_t* PanelViewController::getView(const std::string& viewId) const
{
    auto it = views_.find(viewId);
    if (it != views_.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace Ui
} // namespace DirtSim
