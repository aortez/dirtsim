#include "LogPanel.h"
#include "core/LoggingChannels.h"
#include <fstream>
#include <lvgl/lvgl.h>
#include <sstream>

namespace DirtSim {
namespace Ui {

LogPanel::LogPanel(lv_obj_t* parent, const std::string& logFilePath, size_t maxLines)
    : logFilePath_(logFilePath),
      maxLines_(maxLines),
      lastRefreshTime_(std::chrono::steady_clock::now())
{
    // Create container that fills parent.
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container_, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 8, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Create header label.
    headerLabel_ = lv_label_create(container_);
    lv_label_set_text(headerLabel_, "System Logs");
    lv_obj_set_style_text_color(headerLabel_, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_text_font(headerLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_bottom(headerLabel_, 8, 0);

    // Create scrollable text area for log content.
    logTextArea_ = lv_textarea_create(container_);
    lv_obj_set_flex_grow(logTextArea_, 1);
    lv_obj_set_width(logTextArea_, LV_PCT(100));
    lv_textarea_set_text(logTextArea_, "Loading logs...");
    lv_obj_set_style_bg_color(logTextArea_, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_text_color(logTextArea_, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(logTextArea_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_color(logTextArea_, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(logTextArea_, 1, 0);
    lv_obj_set_style_radius(logTextArea_, 4, 0);
    lv_textarea_set_cursor_click_pos(logTextArea_, false);
    lv_obj_clear_flag(logTextArea_, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    // Create auto-refresh timer.
    refreshTimer_ = lv_timer_create(
        onRefreshTimer, static_cast<uint32_t>(refreshIntervalSeconds_ * 1000), this);

    // Initial refresh.
    refresh();

    LOG_INFO(Controls, "LogPanel created for {}", logFilePath_);
}

LogPanel::~LogPanel()
{
    if (refreshTimer_) {
        lv_timer_delete(refreshTimer_);
        refreshTimer_ = nullptr;
    }
    LOG_INFO(Controls, "LogPanel destroyed");
}

lv_obj_t* LogPanel::getContainer() const
{
    return container_;
}

void LogPanel::refresh()
{
    auto lines = readLastLines();
    updateDisplay(lines);
    lastRefreshTime_ = std::chrono::steady_clock::now();
}

void LogPanel::setFilter(const std::string& filter)
{
    filter_ = filter;
    refresh();
}

void LogPanel::setLogFilePath(const std::string& path)
{
    logFilePath_ = path;
    refresh();
}

void LogPanel::setRefreshInterval(double seconds)
{
    refreshIntervalSeconds_ = seconds;
    if (refreshTimer_) {
        lv_timer_set_period(refreshTimer_, static_cast<uint32_t>(seconds * 1000));
    }
}

void LogPanel::update()
{
    // Manual update check - timer handles auto-refresh.
}

std::deque<std::string> LogPanel::readLastLines()
{
    std::deque<std::string> lines;

    std::ifstream file(logFilePath_);
    if (!file.is_open()) {
        lines.push_back("Unable to open log file: " + logFilePath_);
        return lines;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Apply filter if set.
        if (!filter_.empty() && line.find(filter_) == std::string::npos) {
            continue;
        }

        lines.push_back(line);

        // Keep only maxLines_.
        if (lines.size() > maxLines_) {
            lines.pop_front();
        }
    }

    if (lines.empty()) {
        std::string msg = "(no log entries";
        msg += filter_.empty() ? ")" : " matching filter)";
        lines.push_back(msg);
    }

    return lines;
}

void LogPanel::updateDisplay(const std::deque<std::string>& lines)
{
    std::ostringstream oss;
    for (const auto& line : lines) {
        oss << line << "\n";
    }

    lv_textarea_set_text(logTextArea_, oss.str().c_str());

    // Scroll to bottom to show latest entries.
    lv_textarea_set_cursor_pos(logTextArea_, LV_TEXTAREA_CURSOR_LAST);
}

void LogPanel::onRefreshTimer(lv_timer_t* timer)
{
    auto* self = static_cast<LogPanel*>(lv_timer_get_user_data(timer));
    if (self) {
        self->refresh();
    }
}

} // namespace Ui
} // namespace DirtSim
