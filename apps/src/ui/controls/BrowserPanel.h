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
#include <variant>
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

    struct ModalStyle {
        int width;
        int height;
        int widthPercent;
        int heightPercent;
        lv_opa_t overlayOpacity;
        lv_opa_t modalOpacity;

        constexpr ModalStyle(
            int widthIn = 420,
            int heightIn = 440,
            int widthPercentIn = 0,
            int heightPercentIn = 0,
            lv_opa_t overlayOpacityIn = LV_OPA_60,
            lv_opa_t modalOpacityIn = LV_OPA_90)
            : width(widthIn),
              height(heightIn),
              widthPercent(widthPercentIn),
              heightPercent(heightPercentIn),
              overlayOpacity(overlayOpacityIn),
              modalOpacity(modalOpacityIn)
        {}
    };

    enum class DetailActionColumn {
        Left,
        Right,
    };

    struct DetailAction {
        std::string label;
        std::function<Result<std::monostate, std::string>(const Item& item)> handler;
        uint32_t color = 0x00AA66;
        DetailActionColumn column = DetailActionColumn::Left;
        bool shareRowWithSidePanel = false;
    };

    struct DetailSidePanel {
        std::string label;
        std::function<void(lv_obj_t* parent, const Item& item)> builder;
        uint32_t color = 0x2A7FDB;
    };

    struct ListActionPanel {
        std::string label;
        std::function<void(lv_obj_t* parent)> builder;
    };

    using ListFetcher = std::function<Result<std::vector<Item>, std::string>()>;
    using DetailFetcher = std::function<Result<DetailText, std::string>(const Item& item)>;
    using DeleteHandler = std::function<Result<bool, std::string>(const Item& item)>;

    BrowserPanel(
        lv_obj_t* parent,
        std::string title,
        ListFetcher listFetcher,
        DetailFetcher detailFetcher,
        DeleteHandler deleteHandler,
        std::vector<DetailAction> detailActions = {},
        std::optional<DetailSidePanel> detailSidePanel = std::nullopt,
        std::optional<ListActionPanel> listActionPanel = std::nullopt,
        ModalStyle modalStyle = ModalStyle{});
    ~BrowserPanel();

    void refreshList();
    Result<GenomeId, std::string> openDetailByIndex(size_t index);
    Result<GenomeId, std::string> openDetailById(const GenomeId& id);
    Result<std::monostate, std::string> triggerDetailActionForModalId(const GenomeId& id);

private:
    struct CallbackContext {
        BrowserPanel* panel = nullptr;
        size_t index = 0;
    };

    struct ModalActionContext {
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
    lv_obj_t* modalSideColumn_ = nullptr;
    lv_obj_t* modalSideContent_ = nullptr;
    lv_obj_t* modalToggleButton_ = nullptr;

    std::string title_;
    std::vector<Item> items_;
    std::vector<RowWidgets> rows_;
    std::vector<std::unique_ptr<CallbackContext>> rowContexts_;
    std::vector<std::unique_ptr<ModalActionContext>> modalActionContexts_;
    std::unordered_set<GenomeId> selectedIds_;
    std::optional<GenomeId> modalItemId_;
    bool sidePanelVisible_ = false;

    ListFetcher listFetcher_;
    DetailFetcher detailFetcher_;
    DeleteHandler deleteHandler_;
    std::vector<DetailAction> detailActions_;
    std::optional<DetailSidePanel> detailSidePanel_;
    std::optional<ListActionPanel> listActionPanel_;
    ModalStyle modalStyle_{};

    void createLayout();
    void rebuildList();
    void updateDeleteSelectedState();
    void updateModalDeleteState();
    void updateSelectionCheckboxes();
    void openDetailModal(size_t index);
    void closeModal();
    void handleModalAction(size_t index);

    bool isDeleteConfirmChecked() const;
    bool isModalDeleteConfirmChecked() const;

    void setButtonEnabled(lv_obj_t* buttonContainer, bool enabled);
    void toggleSidePanel();
    void setSidePanelVisible(bool visible);
    void updateSidePanelToggleIcon();

    static void onItemButtonClicked(lv_event_t* e);
    static void onItemCheckboxToggled(lv_event_t* e);
    static void onSelectAllClicked(lv_event_t* e);
    static void onDeleteSelectedClicked(lv_event_t* e);
    static void onDeleteConfirmToggled(lv_event_t* e);
    static void onModalActionClicked(lv_event_t* e);
    static void onModalDeleteClicked(lv_event_t* e);
    static void onModalDeleteConfirmToggled(lv_event_t* e);
    static void onModalToggleClicked(lv_event_t* e);
    static void onModalOkClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
