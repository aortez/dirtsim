#pragma once

#include "core/Result.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "lvgl/lvgl.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace DirtSim {
namespace Ui {

class BrowserPanel {
public:
    struct DetailText {
        std::string text;
    };

    struct Item {
        GenomeId id{};
        std::string label;
    };

    using ListFetcher = std::function<Result<std::vector<Item>, std::string>()>;
    using DetailFetcher = std::function<Result<DetailText, std::string>(const Item& item)>;
    using DeleteHandler = std::function<Result<bool, std::string>(const Item& item)>;

    BrowserPanel(
        lv_obj_t* parent,
        std::string title,
        ListFetcher listFetcher,
        DetailFetcher detailFetcher,
        DeleteHandler deleteHandler);
    ~BrowserPanel();

    void refreshList();

private:
    struct CallbackContext {
        BrowserPanel* panel = nullptr;
        size_t index = 0;
    };

    struct RowWidgets {
        lv_obj_t* checkbox = nullptr;
        lv_obj_t* row = nullptr;
        lv_obj_t* buttonContainer = nullptr;
    };

    lv_obj_t* parent_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* listColumn_ = nullptr;
    lv_obj_t* deleteConfirmCheckbox_ = nullptr;
    lv_obj_t* deleteSelectedButton_ = nullptr;
    lv_obj_t* modalConfirmCheckbox_ = nullptr;
    lv_obj_t* modalDeleteButton_ = nullptr;
    lv_obj_t* modalOverlay_ = nullptr;
    lv_obj_t* selectAllButton_ = nullptr;

    std::string title_;
    std::vector<Item> items_;
    std::vector<RowWidgets> rows_;
    std::vector<std::unique_ptr<CallbackContext>> rowContexts_;
    std::unordered_set<GenomeId> selectedIds_;
    std::optional<GenomeId> modalItemId_;

    ListFetcher listFetcher_;
    DetailFetcher detailFetcher_;
    DeleteHandler deleteHandler_;

    void createLayout();
    void rebuildList();
    void updateDeleteSelectedState();
    void updateModalDeleteState();
    void updateSelectionCheckboxes();
    void openDetailModal(size_t index);
    void closeModal();

    bool isDeleteConfirmChecked() const;
    bool isModalDeleteConfirmChecked() const;

    void setButtonEnabled(lv_obj_t* buttonContainer, bool enabled);

    static void onItemButtonClicked(lv_event_t* e);
    static void onItemCheckboxToggled(lv_event_t* e);
    static void onSelectAllClicked(lv_event_t* e);
    static void onDeleteSelectedClicked(lv_event_t* e);
    static void onDeleteConfirmToggled(lv_event_t* e);
    static void onModalDeleteClicked(lv_event_t* e);
    static void onModalDeleteConfirmToggled(lv_event_t* e);
    static void onModalOkClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
