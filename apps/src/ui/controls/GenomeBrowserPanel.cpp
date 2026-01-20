#include "GenomeBrowserPanel.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioId.h"
#include "core/network/WebSocketServiceInterface.h"
#include "core/reflect.h"
#include "server/api/GenomeDelete.h"
#include "server/api/GenomeList.h"
#include <iomanip>
#include <sstream>

namespace DirtSim {
namespace Ui {

GenomeBrowserPanel::GenomeBrowserPanel(
    lv_obj_t* parent, Network::WebSocketServiceInterface* wsService)
    : wsService_(wsService),
      browser_(
          parent,
          "Genome Browser",
          [this]() { return fetchList(); },
          [this](const BrowserPanel::Item& item) { return fetchDetail(item); },
          [this](const BrowserPanel::Item& item) { return deleteItem(item); })
{
    refresh();
}

void GenomeBrowserPanel::refresh()
{
    browser_.refreshList();
}

Result<std::vector<BrowserPanel::Item>, std::string> GenomeBrowserPanel::fetchList()
{
    if (!wsService_) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(
            "No WebSocketService available");
    }
    if (!wsService_->isConnected()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error("Server not connected");
    }

    Api::GenomeList::Command cmd{};
    auto response = wsService_->sendCommandAndGetResponse<Api::GenomeList::Okay>(cmd, 5000);
    if (response.isError()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(response.errorValue());
    }
    if (response.value().isError()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(
            response.value().errorValue().message);
    }

    metadataById_.clear();
    std::vector<BrowserPanel::Item> items;
    const auto& ok = response.value().value();
    items.reserve(ok.genomes.size());
    for (const auto& entry : ok.genomes) {
        metadataById_[entry.id] = entry.metadata;
        BrowserPanel::Item item;
        item.id = entry.id;
        item.label = formatListLabel(entry.id, entry.metadata);
        items.push_back(std::move(item));
    }

    return Result<std::vector<BrowserPanel::Item>, std::string>::okay(std::move(items));
}

Result<BrowserPanel::DetailText, std::string> GenomeBrowserPanel::fetchDetail(
    const BrowserPanel::Item& item)
{
    auto it = metadataById_.find(item.id);
    if (it == metadataById_.end()) {
        return Result<BrowserPanel::DetailText, std::string>::error("Genome metadata not found");
    }

    return Result<BrowserPanel::DetailText, std::string>::okay(
        BrowserPanel::DetailText{ .text = formatDetailText(item.id, it->second) });
}

Result<bool, std::string> GenomeBrowserPanel::deleteItem(const BrowserPanel::Item& item)
{
    if (!wsService_) {
        return Result<bool, std::string>::error("No WebSocketService available");
    }
    if (!wsService_->isConnected()) {
        return Result<bool, std::string>::error("Server not connected");
    }

    Api::GenomeDelete::Command cmd{ .id = item.id };
    auto response = wsService_->sendCommandAndGetResponse<Api::GenomeDelete::Okay>(cmd, 5000);
    if (response.isError()) {
        return Result<bool, std::string>::error(response.errorValue());
    }
    if (response.value().isError()) {
        return Result<bool, std::string>::error(response.value().errorValue().message);
    }

    const bool success = response.value().value().success;
    if (!success) {
        LOG_WARN(Controls, "GenomeBrowser: Delete returned false for {}", item.id.toShortString());
    }
    return Result<bool, std::string>::okay(success);
}

std::string GenomeBrowserPanel::formatListLabel(
    const GenomeId& id, const GenomeMetadata& meta) const
{
    std::ostringstream oss;
    const std::string name = meta.name.empty() ? id.toShortString() : meta.name;
    oss << name << "\n";
    oss << "Fitness: " << std::fixed << std::setprecision(2) << meta.fitness;
    oss << "  Gen: " << meta.generation;
    oss << "  " << Scenario::toString(meta.scenarioId);
    return oss.str();
}

std::string GenomeBrowserPanel::formatDetailText(
    const GenomeId& id, const GenomeMetadata& meta) const
{
    std::ostringstream oss;
    oss << "Genome ID: " << id.toString() << "\n";
    if (!meta.name.empty()) {
        oss << "Name: " << meta.name << "\n";
    }
    oss << "Scenario: " << Scenario::toString(meta.scenarioId) << "\n";
    oss << "Fitness: " << std::fixed << std::setprecision(3) << meta.fitness << "\n";
    oss << "Generation: " << meta.generation << "\n";
    oss << "Created: " << meta.createdTimestamp << "\n";
    if (!meta.notes.empty()) {
        oss << "Notes: " << meta.notes << "\n";
    }
    if (meta.organismType.has_value()) {
        oss << "Organism Type: " << reflect::enum_name(meta.organismType.value()) << "\n";
    }
    if (meta.brainKind.has_value()) {
        oss << "Brain Kind: " << meta.brainKind.value() << "\n";
    }
    if (meta.brainVariant.has_value()) {
        oss << "Brain Variant: " << meta.brainVariant.value() << "\n";
    }
    if (meta.trainingSessionId.has_value()) {
        oss << "Training Session: " << meta.trainingSessionId->toShortString() << "\n";
    }
    return oss.str();
}

} // namespace Ui
} // namespace DirtSim
