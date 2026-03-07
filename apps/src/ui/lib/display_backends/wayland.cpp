/**
 * @file wayland.c
 *
 * The wayland backend.
 *
 * Based on the original file from the repository.
 *
 * Move to a separate file.
 * 2025 EDGEMTech Ltd.
 *
 * Author: EDGEMTech Ltd, Erik Tagirov (erik.tagirov@edgemtech.ch)
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <chrono>
#include <cmath>
#include <stdlib.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "ui/lib/LoopTimingStats.h"
#include "ui/state-machine/StateMachine.h"
#include <spdlog/spdlog.h>

#if LV_USE_WAYLAND
#include "ui/lib/backends.h"
#include "ui/lib/simulator_settings.h"
#include "ui/lib/simulator_util.h"

/**********************
 *  EXTERNAL VARIABLES
 **********************/
extern simulator_settings_t settings;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_display_t* init_wayland(void);
static void run_loop_wayland(DirtSim::Ui::StateMachine& sm);

/**********************
 *  STATIC VARIABLES
 **********************/
static const char* backend_name = "WAYLAND";

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/// Register the backend.
int backend_init_wayland(backend_t* backend)
{
    LV_ASSERT_NULL(backend);
    backend->handle->display = static_cast<display_backend_t*>(malloc(sizeof(display_backend_t)));
    LV_ASSERT_NULL(backend->handle->display);

    backend->handle->display->init_display = init_wayland;
    backend->handle->display->run_loop = run_loop_wayland;
    backend->name = backend_name;
    backend->type = BACKEND_DISPLAY;

    return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/// Initialize the Wayland display driver.
static lv_display_t* init_wayland(void)
{
    lv_display_t* disp;
    lv_group_t* g;

    disp = lv_wayland_window_create(
        settings.window_width, settings.window_height, const_cast<char*>("Dirt Sim"), NULL);

    if (disp == NULL) {
        die("Failed to initialize Wayland backend\n");
    }

    if (settings.fullscreen) {
        lv_wayland_window_set_fullscreen(disp, true);
    }
    else if (settings.maximize) {
        lv_wayland_window_set_maximized(disp, true);
    }

    g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(lv_wayland_get_keyboard(disp), g);
    lv_indev_set_group(lv_wayland_get_pointeraxis(disp), g);

    return disp;
}

/**
 * The run loop of the Wayland driver.
 */
static void run_loop_wayland(DirtSim::Ui::StateMachine& sm)
{
    bool completed;

    DirtSim::Ui::LoopTimingStats stats;
    int completedCount = 0;

    /* Handle LVGL tasks. */
    while (!sm.shouldExit()) {
        auto loopStart = DirtSim::Ui::LoopTimingStats::Clock::now();

        // Process UI state machine events.
        auto eventsStart = DirtSim::Ui::LoopTimingStats::Clock::now();
        sm.processEvents();
        double eventsMs = std::chrono::duration<double, std::milli>(
                              DirtSim::Ui::LoopTimingStats::Clock::now() - eventsStart)
                              .count();

        // Update background animations (event-driven, no timer).
        sm.updateAnimations();

        // Process LVGL timer events.
        auto timerStart = DirtSim::Ui::LoopTimingStats::Clock::now();
        completed = lv_wayland_timer_handler();
        double timerMs = std::chrono::duration<double, std::milli>(
                             DirtSim::Ui::LoopTimingStats::Clock::now() - timerStart)
                             .count();

        if (completed) {
            /* Wait to avoid busy-looping and consuming 100% CPU. */
            usleep(1000);
        }

        double loopMs = std::chrono::duration<double, std::milli>(
                            DirtSim::Ui::LoopTimingStats::Clock::now() - loopStart)
                            .count();
        stats.processEvents.record(eventsMs);
        stats.timerHandler.record(timerMs);
        if (completed) {
            completedCount++;
        }
        stats.recordLoop(loopMs);

        if (stats.shouldLog()) {
            // Override default log to include idle count.
            stats.lastLogTime = DirtSim::Ui::LoopTimingStats::Clock::now();
            if (stats.loopCount > 0) {
                const double avgLoop = stats.totalLoopMs / stats.loopCount;
                const double variance =
                    (stats.totalLoopMsSq / stats.loopCount) - (avgLoop * avgLoop);
                const double stddev = variance > 0.0 ? std::sqrt(variance) : 0.0;
                spdlog::info(
                    "UI loop timing ({} iters, {} idle):", stats.loopCount, completedCount);
                spdlog::info(
                    "  Loop: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
                    avgLoop,
                    stats.minLoopMs,
                    stats.maxLoopMs,
                    stddev);
                spdlog::info(
                    "  processEvents: {:.2f}ms avg (max={:.2f})",
                    stats.processEvents.totalMs / stats.loopCount,
                    stats.processEvents.maxMs);
                spdlog::info(
                    "  lv_wayland_timer_handler: {:.2f}ms avg (max={:.2f})",
                    stats.timerHandler.totalMs / stats.loopCount,
                    stats.timerHandler.maxMs);
            }
            stats.reset();
            completedCount = 0;
        }

        /* Run until the last window closes. */
        if (!lv_wayland_window_is_open(NULL)) {
            spdlog::info("Wayland window closed, exiting");
            sm.setShouldExit(true);
            break;
        }
    }

    // Process any final UI updates.
    for (int i = 0; i < 3; ++i) {
        lv_wayland_timer_handler();
    }

    // TODO: Take exit screenshot.
    // SimulatorUI::takeExitScreenshot();
}

#endif /*#if LV_USE_WAYLAND. */
