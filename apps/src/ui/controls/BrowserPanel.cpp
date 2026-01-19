#include "BrowserPanel.h"
#include "core/LoggingChannels.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <algorithm>

namespace DirtSim {
namespace Ui {

namespace {
constexpr int kActionColumnWidth = 190;
constexpr int kCheckboxSize = 24;
constexpr int kDeleteButtonWidth = 140;
constexpr int kRowHeight = 44;
} // namespace

BrowserPanel::BrowserPanel(
    lv_obj_t* parent,
    std::string title,
    ListFetcher listFetcher,
    DetailFetcher detailFetcher,
    DeleteHandler deleteHandler)
    : parent_(parent),
      title_(std::move(title)),
      listFetcher_(std::move(listFetcher)),
      detailFetcher_(std::move(detailFetcher)),
      deleteHandler_(std::move(deleteHandler))
{
    createLayout();
}

BrowserPanel::~BrowserPanel()
{
    closeModal();
}

void BrowserPanel::refreshList()
{
    if (!listFetcher_) {
        LOG_WARN(Controls, "BrowserPanel: List fetcher missing");
        return;
    }

    auto result = listFetcher_();
    if (result.isError()) {
        LOG_WARN(Controls, "BrowserPanel: List fetch failed: {}", result.errorValue());
        items_.clear();
    }
    else {
        items_ = std::move(result.value());
    }

    rebuildList();
    updateDeleteSelectedState();
}

void BrowserPanel::createLayout()
{
    container_ = lv_obj_create(parent_);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 6, 0);
    lv_obj_set_style_pad_row(container_, 6, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* titleLabel = lv_label_create(container_);
    lv_label_set_text(titleLabel, title_.c_str());
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);

    lv_obj_t* columns = lv_obj_create(container_);
    lv_obj_set_size(columns, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_grow(columns, 1);
    lv_obj_set_style_bg_opa(columns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(columns, 0, 0);
    lv_obj_set_style_pad_all(columns, 0, 0);
    lv_obj_set_style_pad_column(columns, 8, 0);
    lv_obj_clear_flag(columns, LV_OBJ_FLAG_SCROLLABLE);

    listColumn_ = lv_obj_create(columns);
    lv_obj_set_flex_grow(listColumn_, 1);
    lv_obj_set_height(listColumn_, LV_PCT(100));
    lv_obj_set_flex_flow(listColumn_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(listColumn_, 0, 0);
    lv_obj_set_style_pad_row(listColumn_, 6, 0);
    lv_obj_set_style_bg_opa(listColumn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listColumn_, 0, 0);
    lv_obj_set_scroll_dir(listColumn_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(listColumn_, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* actionColumn = lv_obj_create(columns);
    lv_obj_set_width(actionColumn, kActionColumnWidth);
    lv_obj_set_height(actionColumn, LV_PCT(100));
    lv_obj_set_flex_flow(actionColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(actionColumn, 0, 0);
    lv_obj_set_style_pad_row(actionColumn, 8, 0);
    lv_obj_set_style_bg_opa(actionColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actionColumn, 0, 0);
    lv_obj_clear_flag(actionColumn, LV_OBJ_FLAG_SCROLLABLE);

    selectAllButton_ = LVGLBuilder::actionButton(actionColumn)
                           .text("Select All")
                           .mode(LVGLBuilder::ActionMode::Push)
                           .height(kRowHeight)
                           .layoutRow()
                           .alignLeft()
                           .callback(onSelectAllClicked, this)
                           .buildOrLog();
    if (selectAllButton_) {
        lv_obj_set_width(selectAllButton_, LV_PCT(100));
    }

    lv_obj_t* deleteRow = lv_obj_create(actionColumn);
    lv_obj_set_size(deleteRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(deleteRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(deleteRow, 0, 0);
    lv_obj_set_style_pad_all(deleteRow, 0, 0);
    lv_obj_set_style_pad_column(deleteRow, 6, 0);
    lv_obj_set_flex_flow(deleteRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        deleteRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(deleteRow, LV_OBJ_FLAG_SCROLLABLE);

    deleteSelectedButton_ = LVGLBuilder::actionButton(deleteRow)
                                .text("Delete Selected")
                                .mode(LVGLBuilder::ActionMode::Push)
                                .height(kRowHeight)
                                .backgroundColor(0xCC0000)
                                .layoutRow()
                                .alignLeft()
                                .callback(onDeleteSelectedClicked, this)
                                .buildOrLog();
    if (deleteSelectedButton_) {
        lv_obj_set_width(deleteSelectedButton_, kDeleteButtonWidth);
    }

    deleteConfirmCheckbox_ = lv_checkbox_create(deleteRow);
    lv_checkbox_set_text(deleteConfirmCheckbox_, "Confirm");
    lv_obj_set_style_text_font(deleteConfirmCheckbox_, &lv_font_montserrat_10, 0);
    lv_obj_add_event_cb(
        deleteConfirmCheckbox_, onDeleteConfirmToggled, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_clear_flag(deleteConfirmCheckbox_, LV_OBJ_FLAG_SCROLLABLE);

    setButtonEnabled(deleteSelectedButton_, false);
}

void BrowserPanel::rebuildList()
{
    if (!listColumn_) {
        return;
    }

    std::unordered_set<GenomeId> newSelected;
    for (const auto& item : items_) {
        if (selectedIds_.count(item.id) > 0) {
            newSelected.insert(item.id);
        }
    }
    selectedIds_ = std::move(newSelected);

    lv_obj_clean(listColumn_);
    rows_.clear();
    rowContexts_.clear();

    if (items_.empty()) {
        lv_obj_t* emptyLabel = lv_label_create(listColumn_);
        lv_label_set_text(emptyLabel, "No items found.");
        lv_obj_set_style_text_color(emptyLabel, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_font(emptyLabel, &lv_font_montserrat_12, 0);
        return;
    }

    rows_.reserve(items_.size());
    rowContexts_.reserve(items_.size());

    for (size_t i = 0; i < items_.size(); ++i) {
        RowWidgets row{};
        row.row = lv_obj_create(listColumn_);
        lv_obj_set_width(row.row, LV_PCT(100));
        lv_obj_set_style_bg_opa(row.row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row.row, 0, 0);
        lv_obj_set_style_pad_all(row.row, 0, 0);
        lv_obj_set_style_pad_column(row.row, 6, 0);
        lv_obj_set_flex_flow(row.row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(
            row.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row.row, LV_OBJ_FLAG_SCROLLABLE);

        row.checkbox = lv_checkbox_create(row.row);
        lv_checkbox_set_text(row.checkbox, "");
        lv_obj_set_size(row.checkbox, kCheckboxSize, kCheckboxSize);

        auto context = std::make_unique<CallbackContext>();
        context->panel = this;
        context->index = i;
        lv_obj_add_event_cb(
            row.checkbox, onItemCheckboxToggled, LV_EVENT_VALUE_CHANGED, context.get());

        row.buttonContainer = LVGLBuilder::actionButton(row.row)
                                  .text(items_[i].label.c_str())
                                  .mode(LVGLBuilder::ActionMode::Push)
                                  .height(kRowHeight)
                                  .layoutRow()
                                  .alignLeft()
                                  .callback(onItemButtonClicked, context.get())
                                  .buildOrLog();
        if (row.buttonContainer) {
            lv_obj_set_flex_grow(row.buttonContainer, 1);
            lv_obj_set_width(row.buttonContainer, LV_PCT(100));
        }

        if (selectedIds_.count(items_[i].id) > 0) {
            lv_obj_add_state(row.checkbox, LV_STATE_CHECKED);
        }

        rows_.push_back(row);
        rowContexts_.push_back(std::move(context));
    }
}

void BrowserPanel::updateDeleteSelectedState()
{
    const bool enabled = !selectedIds_.empty() && isDeleteConfirmChecked();
    setButtonEnabled(deleteSelectedButton_, enabled);
}

void BrowserPanel::updateModalDeleteState()
{
    const bool enabled = modalItemId_.has_value() && isModalDeleteConfirmChecked();
    setButtonEnabled(modalDeleteButton_, enabled);
}

void BrowserPanel::updateSelectionCheckboxes()
{
    for (size_t i = 0; i < rows_.size() && i < items_.size(); ++i) {
        if (!rows_[i].checkbox) {
            continue;
        }
        if (selectedIds_.count(items_[i].id) > 0) {
            lv_obj_add_state(rows_[i].checkbox, LV_STATE_CHECKED);
        }
        else {
            lv_obj_clear_state(rows_[i].checkbox, LV_STATE_CHECKED);
        }
    }
}

void BrowserPanel::openDetailModal(size_t index)
{
    if (index >= items_.size() || !detailFetcher_) {
        return;
    }

    const Item& item = items_[index];
    auto detailResult = detailFetcher_(item);
    if (detailResult.isError()) {
        LOG_WARN(Controls, "BrowserPanel: Detail fetch failed: {}", detailResult.errorValue());
        return;
    }

    closeModal();
    modalItemId_ = item.id;

    modalOverlay_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modalOverlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(modalOverlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modalOverlay_, LV_OPA_60, 0);
    lv_obj_clear_flag(modalOverlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(modalOverlay_);

    lv_obj_t* modal = lv_obj_create(modalOverlay_);
    lv_obj_set_size(modal, 420, 440);
    lv_obj_center(modal);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_90, 0);
    lv_obj_set_style_radius(modal, 12, 0);
    lv_obj_set_style_pad_all(modal, 12, 0);
    lv_obj_set_style_pad_row(modal, 8, 0);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLabel = lv_label_create(modal);
    lv_label_set_text(titleLabel, title_.c_str());
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, 0);

    lv_obj_t* itemLabel = lv_label_create(modal);
    lv_label_set_text(itemLabel, item.label.c_str());
    lv_label_set_long_mode(itemLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(itemLabel, LV_PCT(100));
    lv_obj_set_style_text_color(itemLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(itemLabel, &lv_font_montserrat_12, 0);

    lv_obj_t* detailContainer = lv_obj_create(modal);
    lv_obj_set_width(detailContainer, LV_PCT(100));
    lv_obj_set_flex_grow(detailContainer, 1);
    lv_obj_set_style_bg_opa(detailContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(detailContainer, 0, 0);
    lv_obj_set_style_pad_all(detailContainer, 0, 0);
    lv_obj_set_flex_flow(detailContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(detailContainer, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(detailContainer, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* detailLabel = lv_label_create(detailContainer);
    lv_label_set_text(detailLabel, detailResult.value().c_str());
    lv_label_set_long_mode(detailLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(detailLabel, LV_PCT(100));
    lv_obj_set_style_text_color(detailLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(detailLabel, &lv_font_montserrat_12, 0);

    lv_obj_t* buttonRow = lv_obj_create(modal);
    lv_obj_set_size(buttonRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(buttonRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(buttonRow, 0, 0);
    lv_obj_set_style_pad_all(buttonRow, 0, 0);
    lv_obj_set_style_pad_column(buttonRow, 8, 0);
    lv_obj_set_flex_flow(buttonRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        buttonRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(buttonRow, LV_OBJ_FLAG_SCROLLABLE);

    LVGLBuilder::actionButton(buttonRow)
        .text("OK")
        .mode(LVGLBuilder::ActionMode::Push)
        .size(80)
        .backgroundColor(0x00AA66)
        .callback(onModalOkClicked, this)
        .buildOrLog();

    lv_obj_t* deleteRow = lv_obj_create(buttonRow);
    lv_obj_set_style_bg_opa(deleteRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(deleteRow, 0, 0);
    lv_obj_set_style_pad_all(deleteRow, 0, 0);
    lv_obj_set_style_pad_column(deleteRow, 6, 0);
    lv_obj_set_flex_flow(deleteRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        deleteRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(deleteRow, LV_OBJ_FLAG_SCROLLABLE);

    modalDeleteButton_ = LVGLBuilder::actionButton(deleteRow)
                             .text("Delete")
                             .mode(LVGLBuilder::ActionMode::Push)
                             .size(80)
                             .backgroundColor(0xCC0000)
                             .callback(onModalDeleteClicked, this)
                             .buildOrLog();

    modalConfirmCheckbox_ = lv_checkbox_create(deleteRow);
    lv_checkbox_set_text(modalConfirmCheckbox_, "Confirm");
    lv_obj_set_style_text_font(modalConfirmCheckbox_, &lv_font_montserrat_10, 0);
    lv_obj_add_event_cb(
        modalConfirmCheckbox_, onModalDeleteConfirmToggled, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_clear_flag(modalConfirmCheckbox_, LV_OBJ_FLAG_SCROLLABLE);

    updateModalDeleteState();
}

void BrowserPanel::closeModal()
{
    if (modalOverlay_) {
        lv_obj_del(modalOverlay_);
        modalOverlay_ = nullptr;
    }

    modalConfirmCheckbox_ = nullptr;
    modalDeleteButton_ = nullptr;
    modalItemId_.reset();
}

bool BrowserPanel::isDeleteConfirmChecked() const
{
    return deleteConfirmCheckbox_ && lv_obj_has_state(deleteConfirmCheckbox_, LV_STATE_CHECKED);
}

bool BrowserPanel::isModalDeleteConfirmChecked() const
{
    return modalConfirmCheckbox_ && lv_obj_has_state(modalConfirmCheckbox_, LV_STATE_CHECKED);
}

void BrowserPanel::setButtonEnabled(lv_obj_t* buttonContainer, bool enabled)
{
    if (!buttonContainer) {
        return;
    }

    if (enabled) {
        lv_obj_clear_state(buttonContainer, LV_STATE_DISABLED);
        lv_obj_set_style_opa(buttonContainer, LV_OPA_COVER, 0);
    }
    else {
        lv_obj_add_state(buttonContainer, LV_STATE_DISABLED);
        lv_obj_set_style_opa(buttonContainer, LV_OPA_50, 0);
    }
}

void BrowserPanel::onItemButtonClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* context = static_cast<CallbackContext*>(lv_event_get_user_data(e));
    if (!context || !context->panel) {
        return;
    }

    context->panel->openDetailModal(context->index);
}

void BrowserPanel::onItemCheckboxToggled(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    auto* context = static_cast<CallbackContext*>(lv_event_get_user_data(e));
    if (!context || !context->panel) {
        return;
    }

    BrowserPanel* panel = context->panel;
    if (context->index >= panel->items_.size()) {
        return;
    }

    lv_obj_t* checkbox = lv_event_get_target(e);
    const bool checked = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    const auto& item = panel->items_[context->index];
    if (checked) {
        panel->selectedIds_.insert(item.id);
    }
    else {
        panel->selectedIds_.erase(item.id);
    }

    panel->updateDeleteSelectedState();
}

void BrowserPanel::onSelectAllClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<BrowserPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    if (self->items_.empty()) {
        return;
    }

    const bool allSelected = self->selectedIds_.size() == self->items_.size();
    self->selectedIds_.clear();
    if (!allSelected) {
        for (const auto& item : self->items_) {
            self->selectedIds_.insert(item.id);
        }
    }

    self->updateSelectionCheckboxes();
    self->updateDeleteSelectedState();
}

void BrowserPanel::onDeleteSelectedClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<BrowserPanel*>(lv_event_get_user_data(e));
    if (!self || !self->deleteHandler_) {
        return;
    }

    if (!self->isDeleteConfirmChecked()) {
        return;
    }

    if (self->selectedIds_.empty()) {
        return;
    }

    std::vector<Item> toDelete;
    for (const auto& item : self->items_) {
        if (self->selectedIds_.count(item.id) > 0) {
            toDelete.push_back(item);
        }
    }

    for (const auto& item : toDelete) {
        auto result = self->deleteHandler_(item);
        if (result.isError()) {
            LOG_WARN(Controls, "BrowserPanel: Delete failed: {}", result.errorValue());
            continue;
        }
        if (!result.value()) {
            LOG_WARN(Controls, "BrowserPanel: Delete returned false");
        }
        self->selectedIds_.erase(item.id);
    }

    if (self->deleteConfirmCheckbox_) {
        lv_obj_clear_state(self->deleteConfirmCheckbox_, LV_STATE_CHECKED);
    }

    self->refreshList();
}

void BrowserPanel::onDeleteConfirmToggled(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    auto* self = static_cast<BrowserPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->updateDeleteSelectedState();
}

void BrowserPanel::onModalDeleteClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<BrowserPanel*>(lv_event_get_user_data(e));
    if (!self || !self->deleteHandler_ || !self->modalItemId_.has_value()) {
        return;
    }

    if (!self->isModalDeleteConfirmChecked()) {
        return;
    }

    auto it = std::find_if(self->items_.begin(), self->items_.end(), [&](const Item& item) {
        return item.id == self->modalItemId_.value();
    });
    if (it == self->items_.end()) {
        return;
    }

    auto result = self->deleteHandler_(*it);
    if (result.isError()) {
        LOG_WARN(Controls, "BrowserPanel: Modal delete failed: {}", result.errorValue());
        return;
    }
    if (!result.value()) {
        LOG_WARN(Controls, "BrowserPanel: Modal delete returned false");
    }

    self->selectedIds_.erase(it->id);
    self->closeModal();
    self->refreshList();
}

void BrowserPanel::onModalDeleteConfirmToggled(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    auto* self = static_cast<BrowserPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->updateModalDeleteState();
}

void BrowserPanel::onModalOkClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<BrowserPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->closeModal();
}

} // namespace Ui
} // namespace DirtSim
