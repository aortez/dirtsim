#pragma once

#include "BrowserPanel.h"
#include "server/api/TrainingResult.h"
#include "server/api/TrainingResultList.h"

namespace DirtSim {
namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

class TrainingResultBrowserPanel {
public:
    TrainingResultBrowserPanel(lv_obj_t* parent, Network::WebSocketServiceInterface* wsService);

    void refresh();

private:
    Network::WebSocketServiceInterface* wsService_ = nullptr;
    BrowserPanel browser_;

    Result<std::vector<BrowserPanel::Item>, std::string> fetchList();
    Result<BrowserPanel::DetailText, std::string> fetchDetail(const BrowserPanel::Item& item);
    Result<bool, std::string> deleteItem(const BrowserPanel::Item& item);

    std::string formatListLabel(const Api::TrainingResultList::Entry& entry) const;
    std::string formatDetailText(
        const Api::TrainingResult::Summary& summary,
        const std::vector<Api::TrainingResult::Candidate>& candidates) const;
};

} // namespace Ui
} // namespace DirtSim
