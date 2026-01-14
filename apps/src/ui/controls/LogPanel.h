#pragma once

#include <chrono>
#include <deque>
#include <string>

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;
struct _lv_timer_t;
typedef struct _lv_timer_t lv_timer_t;

namespace DirtSim {
namespace Ui {

/**
 * Panel for displaying log file tails with auto-refresh.
 */
class LogPanel {
public:
    LogPanel(lv_obj_t* parent, const std::string& logFilePath, size_t maxLines = 50);
    ~LogPanel();

    LogPanel(const LogPanel&) = delete;
    LogPanel& operator=(const LogPanel&) = delete;

    lv_obj_t* getContainer() const;
    void refresh();
    void setFilter(const std::string& filter);
    void setLogFilePath(const std::string& path);
    void setRefreshInterval(double seconds);
    void update();

private:
    lv_obj_t* container_ = nullptr;
    lv_obj_t* headerLabel_ = nullptr;
    lv_obj_t* logTextArea_ = nullptr;
    lv_timer_t* refreshTimer_ = nullptr;

    std::string logFilePath_;
    std::string filter_;
    size_t maxLines_;
    double refreshIntervalSeconds_ = 2.0;
    std::chrono::steady_clock::time_point lastRefreshTime_;

    std::deque<std::string> readLastLines();
    void updateDisplay(const std::deque<std::string>& lines);

    static void onRefreshTimer(lv_timer_t* timer);
};

} // namespace Ui
} // namespace DirtSim
