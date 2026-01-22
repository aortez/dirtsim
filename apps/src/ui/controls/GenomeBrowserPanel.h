#pragma once

#include "BrowserPanel.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include <unordered_map>

namespace DirtSim {
namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

class GenomeBrowserPanel {
public:
    GenomeBrowserPanel(lv_obj_t* parent, Network::WebSocketServiceInterface* wsService);

    void refresh();

private:
    Network::WebSocketServiceInterface* wsService_ = nullptr;
    BrowserPanel browser_;
    std::unordered_map<GenomeId, GenomeMetadata> metadataById_;

    Result<std::vector<BrowserPanel::Item>, std::string> fetchList();
    Result<BrowserPanel::DetailText, std::string> fetchDetail(const BrowserPanel::Item& item);
    Result<bool, std::string> deleteItem(const BrowserPanel::Item& item);

    std::string formatListLabel(const GenomeId& id, const GenomeMetadata& meta) const;
    std::string formatDetailText(const GenomeId& id, const GenomeMetadata& meta) const;
};

} // namespace Ui
} // namespace DirtSim
