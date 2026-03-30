#pragma once

#include "BrowserPanel.h"
#include "server/api/Plan.h"
#include <functional>
#include <optional>
#include <unordered_map>

namespace DirtSim {
namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

struct PlanBrowserState {
    std::optional<Api::PlanSummary> latestPlan;
    std::optional<Api::PlanSummary> selectedPlan;
};

class PlanBrowserPanel {
public:
    using StateChangedCallback = std::function<void(const PlanBrowserState& state)>;

    PlanBrowserPanel(
        lv_obj_t* parent,
        Network::WebSocketServiceInterface* wsService,
        std::optional<UUID> selectedPlanId,
        StateChangedCallback stateChangedCallback);

    void refresh();
    Result<UUID, std::string> openDetailById(UUID id);
    Result<UUID, std::string> openDetailByIndex(size_t index);
    Result<std::monostate, std::string> selectDetailForId(UUID id);

private:
    BrowserPanel browser_;
    Network::WebSocketServiceInterface* wsService_ = nullptr;
    StateChangedCallback stateChangedCallback_;
    std::optional<Api::PlanSummary> latestPlan_;
    std::unordered_map<UUID, Api::PlanSummary> summariesById_;
    std::optional<UUID> selectedPlanId_ = std::nullopt;

    Result<std::vector<BrowserPanel::Item>, std::string> fetchList();
    Result<BrowserPanel::DetailText, std::string> fetchDetail(const BrowserPanel::Item& item);
    Result<bool, std::string> deleteItem(const BrowserPanel::Item& item);
    Result<std::monostate, std::string> selectItem(const BrowserPanel::Item& item);

    void notifyStateChanged();

    std::string formatDetailText(const Api::PlanSummary& summary) const;
    std::string formatListLabel(const Api::PlanSummary& summary) const;
};

} // namespace Ui
} // namespace DirtSim
