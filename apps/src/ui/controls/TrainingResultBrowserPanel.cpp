#include "TrainingResultBrowserPanel.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioId.h"
#include "core/network/WebSocketServiceInterface.h"
#include "core/reflect.h"
#include "server/api/TrainingResultDelete.h"
#include "server/api/TrainingResultGet.h"
#include "server/api/TrainingResultList.h"
#include <iomanip>
#include <sstream>

namespace DirtSim {
namespace Ui {

TrainingResultBrowserPanel::TrainingResultBrowserPanel(
    lv_obj_t* parent, Network::WebSocketServiceInterface* wsService)
    : wsService_(wsService),
      browser_(
          parent,
          "Training Results",
          [this]() { return fetchList(); },
          [this](const BrowserPanel::Item& item) { return fetchDetail(item); },
          [this](const BrowserPanel::Item& item) { return deleteItem(item); })
{
    refresh();
}

void TrainingResultBrowserPanel::refresh()
{
    browser_.refreshList();
}

Result<std::vector<BrowserPanel::Item>, std::string> TrainingResultBrowserPanel::fetchList()
{
    if (!wsService_) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(
            "No WebSocketService available");
    }
    if (!wsService_->isConnected()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error("Server not connected");
    }

    Api::TrainingResultList::Command cmd{};
    auto response = wsService_->sendCommandAndGetResponse<Api::TrainingResultList::Okay>(cmd, 5000);
    if (response.isError()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(response.errorValue());
    }
    if (response.value().isError()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(
            response.value().errorValue().message);
    }

    std::vector<BrowserPanel::Item> items;
    const auto& ok = response.value().value();
    items.reserve(ok.results.size());
    for (const auto& entry : ok.results) {
        BrowserPanel::Item item;
        item.id = entry.summary.trainingSessionId;
        item.label = formatListLabel(entry);
        items.push_back(std::move(item));
    }

    return Result<std::vector<BrowserPanel::Item>, std::string>::okay(std::move(items));
}

Result<BrowserPanel::DetailText, std::string> TrainingResultBrowserPanel::fetchDetail(
    const BrowserPanel::Item& item)
{
    if (!wsService_) {
        return Result<BrowserPanel::DetailText, std::string>::error(
            "No WebSocketService available");
    }
    if (!wsService_->isConnected()) {
        return Result<BrowserPanel::DetailText, std::string>::error("Server not connected");
    }

    Api::TrainingResultGet::Command cmd{ .trainingSessionId = item.id };
    auto response = wsService_->sendCommandAndGetResponse<Api::TrainingResultGet::Okay>(cmd, 5000);
    if (response.isError()) {
        return Result<BrowserPanel::DetailText, std::string>::error(response.errorValue());
    }
    if (response.value().isError()) {
        return Result<BrowserPanel::DetailText, std::string>::error(
            response.value().errorValue().message);
    }

    const auto& ok = response.value().value();
    return Result<BrowserPanel::DetailText, std::string>::okay(
        BrowserPanel::DetailText{ .text = formatDetailText(ok.summary, ok.candidates) });
}

Result<bool, std::string> TrainingResultBrowserPanel::deleteItem(const BrowserPanel::Item& item)
{
    if (!wsService_) {
        return Result<bool, std::string>::error("No WebSocketService available");
    }
    if (!wsService_->isConnected()) {
        return Result<bool, std::string>::error("Server not connected");
    }

    Api::TrainingResultDelete::Command cmd{ .trainingSessionId = item.id };
    auto response =
        wsService_->sendCommandAndGetResponse<Api::TrainingResultDelete::Okay>(cmd, 5000);
    if (response.isError()) {
        return Result<bool, std::string>::error(response.errorValue());
    }
    if (response.value().isError()) {
        return Result<bool, std::string>::error(response.value().errorValue().message);
    }

    const bool success = response.value().value().success;
    if (!success) {
        LOG_WARN(
            Controls,
            "TrainingResultBrowser: Delete returned false for {}",
            item.id.toShortString());
    }
    return Result<bool, std::string>::okay(success);
}

std::string TrainingResultBrowserPanel::formatListLabel(
    const Api::TrainingResultList::Entry& entry) const
{
    std::ostringstream oss;
    oss << Scenario::toString(entry.summary.scenarioId) << "\n";
    oss << "Gen " << entry.summary.completedGenerations << "/" << entry.summary.maxGenerations;
    oss << "  Best " << std::fixed << std::setprecision(2) << entry.summary.bestFitness;
    oss << "  Candidates " << entry.candidateCount;
    return oss.str();
}

std::string TrainingResultBrowserPanel::formatDetailText(
    const Api::TrainingResult::Summary& summary,
    const std::vector<Api::TrainingResult::Candidate>& candidates) const
{
    std::ostringstream oss;
    oss << "Session: " << summary.trainingSessionId.toString() << "\n";
    oss << "Scenario: " << Scenario::toString(summary.scenarioId) << "\n";
    oss << "Organism: " << reflect::enum_name(summary.organismType) << "\n";
    oss << "Generations: " << summary.completedGenerations << "/" << summary.maxGenerations << "\n";
    oss << "Population: " << summary.populationSize << "\n";
    oss << "Best Fitness: " << std::fixed << std::setprecision(3) << summary.bestFitness << "\n";
    oss << "Avg Fitness: " << std::fixed << std::setprecision(3) << summary.averageFitness << "\n";
    oss << "Total Time: " << std::fixed << std::setprecision(1) << summary.totalTrainingSeconds
        << "s\n";

    if (!summary.primaryBrainKind.empty()) {
        oss << "Primary Brain: " << summary.primaryBrainKind;
        if (summary.primaryBrainVariant.has_value() && !summary.primaryBrainVariant->empty()) {
            oss << " (" << summary.primaryBrainVariant.value() << ")";
        }
        oss << "\n";
    }

    oss << "\nCandidates (" << candidates.size() << ")\n";
    for (const auto& candidate : candidates) {
        oss << "- " << candidate.id.toShortString();
        oss << "  Fit " << std::fixed << std::setprecision(3) << candidate.fitness;
        oss << "  Gen " << candidate.generation;
        if (!candidate.brainKind.empty()) {
            oss << "  " << candidate.brainKind;
            if (candidate.brainVariant.has_value() && !candidate.brainVariant->empty()) {
                oss << " (" << candidate.brainVariant.value() << ")";
            }
        }
        oss << "\n";
    }

    return oss.str();
}

} // namespace Ui
} // namespace DirtSim
