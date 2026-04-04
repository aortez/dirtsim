#include "PlanBrowserPanel.h"
#include "core/network/WebSocketServiceInterface.h"
#include "server/api/PlanDelete.h"
#include "server/api/PlanList.h"
#include <sstream>

namespace DirtSim {
namespace Ui {

namespace {

struct PlanListPayload {
    std::vector<Api::PlanList::Entry> plans;

    using serialize = zpp::bits::members<1>;
};

struct PlanListResponsePayload {
    std::optional<PlanListPayload> value;
    std::optional<ApiError> error;

    using serialize = zpp::bits::members<2>;
};

} // namespace

PlanBrowserPanel::PlanBrowserPanel(
    lv_obj_t* parent,
    Network::WebSocketServiceInterface* wsService,
    std::optional<UUID> selectedPlanId,
    StateChangedCallback stateChangedCallback)
    : browser_(
          parent,
          "Plans",
          [this]() { return fetchList(); },
          [this](const BrowserPanel::Item& item) { return fetchDetail(item); },
          [this](const BrowserPanel::Item& item) { return deleteItem(item); },
          std::vector<BrowserPanel::DetailAction>{
              BrowserPanel::DetailAction{
                  .label = "Select",
                  .handler = [this](const BrowserPanel::Item& item) { return selectItem(item); },
                  .color = 0x2A7FDB,
                  .column = BrowserPanel::DetailActionColumn::Left,
              },
          }),
      wsService_(wsService),
      stateChangedCallback_(std::move(stateChangedCallback)),
      selectedPlanId_(selectedPlanId)
{
    refresh();
}

void PlanBrowserPanel::refresh()
{
    browser_.refreshList();
}

Result<UUID, std::string> PlanBrowserPanel::openDetailById(UUID id)
{
    return browser_.openDetailById(id);
}

Result<UUID, std::string> PlanBrowserPanel::openDetailByIndex(size_t index)
{
    return browser_.openDetailByIndex(index);
}

Result<std::monostate, std::string> PlanBrowserPanel::selectDetailForId(UUID id)
{
    return browser_.triggerDetailActionForModalId(id);
}

Result<std::vector<BrowserPanel::Item>, std::string> PlanBrowserPanel::fetchList()
{
    if (!wsService_) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(
            "No WebSocketService available");
    }
    if (!wsService_->isConnected()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error("Server not connected");
    }

    Api::PlanList::Command command{};
    const uint64_t requestId = wsService_->allocateRequestId();
    const auto envelope = Network::make_command_envelope(requestId, command);
    auto responseEnvelope = wsService_->sendBinaryAndReceive(envelope, 5000);
    if (responseEnvelope.isError()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(
            responseEnvelope.errorValue());
    }

    PlanListResponsePayload response{};
    try {
        response =
            Network::deserialize_payload<PlanListResponsePayload>(responseEnvelope.value().payload);
    }
    catch (const std::exception& e) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(
            std::string("Failed to deserialize PlanList response: ") + e.what());
    }

    if (response.error.has_value()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(response.error->message);
    }
    if (!response.value.has_value()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(
            "PlanList response missing value");
    }

    summariesById_.clear();
    latestPlan_.reset();
    std::vector<BrowserPanel::Item> items;
    items.reserve(response.value->plans.size());
    for (const auto& entry : response.value->plans) {
        if (!latestPlan_.has_value()) {
            latestPlan_ = entry.summary;
        }
        summariesById_[entry.summary.id] = entry.summary;
        items.push_back(
            BrowserPanel::Item{
                .id = entry.summary.id,
                .label = formatListLabel(entry.summary),
            });
    }

    if (selectedPlanId_.has_value() && !summariesById_.contains(selectedPlanId_.value())) {
        selectedPlanId_.reset();
    }

    if (stateChangedCallback_) {
        PlanBrowserState state{
            .latestPlan = latestPlan_,
            .selectedPlan = std::nullopt,
        };
        if (selectedPlanId_.has_value()) {
            auto selectedIt = summariesById_.find(selectedPlanId_.value());
            if (selectedIt != summariesById_.end()) {
                state.selectedPlan = selectedIt->second;
            }
        }
        stateChangedCallback_(state);
    }
    return Result<std::vector<BrowserPanel::Item>, std::string>::okay(std::move(items));
}

Result<BrowserPanel::DetailText, std::string> PlanBrowserPanel::fetchDetail(
    const BrowserPanel::Item& item)
{
    auto it = summariesById_.find(item.id);
    if (it == summariesById_.end()) {
        return Result<BrowserPanel::DetailText, std::string>::error("Plan summary not found");
    }

    return Result<BrowserPanel::DetailText, std::string>::okay(
        BrowserPanel::DetailText{ .text = formatDetailText(it->second) });
}

Result<bool, std::string> PlanBrowserPanel::deleteItem(const BrowserPanel::Item& item)
{
    if (!wsService_) {
        return Result<bool, std::string>::error("No WebSocketService available");
    }
    if (!wsService_->isConnected()) {
        return Result<bool, std::string>::error("Server not connected");
    }

    Api::PlanDelete::Command command{
        .planId = item.id,
    };
    const uint64_t requestId = wsService_->allocateRequestId();
    const auto envelope = Network::make_command_envelope(requestId, command);
    auto responseEnvelope = wsService_->sendBinaryAndReceive(envelope, 5000);
    if (responseEnvelope.isError()) {
        return Result<bool, std::string>::error(responseEnvelope.errorValue());
    }

    auto response =
        Network::extract_result<Api::PlanDelete::Okay, ApiError>(responseEnvelope.value());
    if (response.isError()) {
        return Result<bool, std::string>::error(response.errorValue().message);
    }

    if (selectedPlanId_ == item.id) {
        selectedPlanId_.reset();
    }

    return Result<bool, std::string>::okay(response.value().success);
}

Result<std::monostate, std::string> PlanBrowserPanel::selectItem(const BrowserPanel::Item& item)
{
    auto it = summariesById_.find(item.id);
    if (it == summariesById_.end()) {
        return Result<std::monostate, std::string>::error("Plan summary not found");
    }

    selectedPlanId_ = item.id;
    notifyStateChanged();
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void PlanBrowserPanel::notifyStateChanged()
{
    if (!stateChangedCallback_) {
        return;
    }

    PlanBrowserState state;
    state.latestPlan = latestPlan_;
    if (selectedPlanId_.has_value()) {
        auto selectedIt = summariesById_.find(selectedPlanId_.value());
        if (selectedIt != summariesById_.end()) {
            state.selectedPlan = selectedIt->second;
        }
    }

    stateChangedCallback_(state);
}

std::string PlanBrowserPanel::formatDetailText(const Api::PlanSummary& summary) const
{
    std::ostringstream oss;
    oss << "Plan: " << summary.id.toString() << "\n";
    oss << "Elapsed frames: " << summary.elapsedFrames << "\n";
    oss << "Best frontier: " << summary.bestFrontier << "\n";
    return oss.str();
}

std::string PlanBrowserPanel::formatListLabel(const Api::PlanSummary& summary) const
{
    std::ostringstream oss;
    oss << summary.id.toShortString() << "\n";
    oss << "Frames " << summary.elapsedFrames;
    oss << "  Frontier " << summary.bestFrontier;
    return oss.str();
}

} // namespace Ui
} // namespace DirtSim
