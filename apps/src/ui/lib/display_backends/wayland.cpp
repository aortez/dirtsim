/**
 * @file wayland.c
 *
 * The wayland backend
 *
 * Based on the original file from the repository
 *
 * - Move to a seperate file
 *   2025 EDGEMTech Ltd.
 *
 * Author: EDGEMTech Ltd, Erik Tagirov (erik.tagirov@edgemtech.ch)
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "stdio.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "ui/state-machine/StateMachine.h"
#include <spdlog/spdlog.h>

#if LV_USE_WAYLAND
#include "ui/lib/backends.h"
#include "ui/lib/simulator_settings.h"
#include "ui/lib/simulator_util.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

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
 *  EXTERNAL VARIABLES
 **********************/
extern simulator_settings_t settings;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Register the backend
 *
 * @param backend the backend descriptor
 * @description configures the descriptor
 */
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

/**
 * Initialize the Wayland display driver
 *
 * @return the LVGL display
 */
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

    // Loop timing instrumentation.
    using Clock = std::chrono::steady_clock;
    int loopCount = 0;
    double totalLoopMs = 0.0;
    double totalLoopMsSq = 0.0;
    double minLoopMs = 1e9;
    double maxLoopMs = 0.0;
    double totalProcessEventsMs = 0.0;
    double maxProcessEventsMs = 0.0;
    double totalTimerHandlerMs = 0.0;
    double maxTimerHandlerMs = 0.0;
    int completedCount = 0;
    auto lastLoopLog = Clock::now();

    /* Handle LVGL tasks. */
    while (!sm.shouldExit()) {
        auto loopStart = Clock::now();

        // Process UI state machine events.
        auto eventsStart = Clock::now();
        sm.processEvents();
        double eventsMs =
            std::chrono::duration<double, std::milli>(Clock::now() - eventsStart).count();

        // Update background animations (event-driven, no timer).
        sm.updateAnimations();

        // Process LVGL timer events.
        auto timerStart = Clock::now();
        completed = lv_wayland_timer_handler();
        double timerMs =
            std::chrono::duration<double, std::milli>(Clock::now() - timerStart).count();

        if (completed) {
            /* Wait to avoid busy-looping and consuming 100% CPU. */
            usleep(1000);
        }

        double loopMs = std::chrono::duration<double, std::milli>(Clock::now() - loopStart).count();
        totalLoopMs += loopMs;
        totalLoopMsSq += loopMs * loopMs;
        if (loopMs < minLoopMs) {
            minLoopMs = loopMs;
        }
        if (loopMs > maxLoopMs) {
            maxLoopMs = loopMs;
        }
        totalProcessEventsMs += eventsMs;
        if (eventsMs > maxProcessEventsMs) {
            maxProcessEventsMs = eventsMs;
        }
        totalTimerHandlerMs += timerMs;
        if (timerMs > maxTimerHandlerMs) {
            maxTimerHandlerMs = timerMs;
        }
        if (completed) {
            completedCount++;
        }
        loopCount++;

        if (Clock::now() - lastLoopLog >= std::chrono::seconds(10)) {
            lastLoopLog = Clock::now();
            if (loopCount > 0) {
                const double avgLoop = totalLoopMs / loopCount;
                const double variance = (totalLoopMsSq / loopCount) - (avgLoop * avgLoop);
                const double stddev = variance > 0.0 ? std::sqrt(variance) : 0.0;
                spdlog::info("UI loop timing ({} iters, {} idle):", loopCount, completedCount);
                spdlog::info(
                    "  Loop: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
                    avgLoop,
                    minLoopMs,
                    maxLoopMs,
                    stddev);
                spdlog::info(
                    "  processEvents: {:.2f}ms avg (max={:.2f})",
                    totalProcessEventsMs / loopCount,
                    maxProcessEventsMs);
                spdlog::info(
                    "  lv_wayland_timer_handler: {:.2f}ms avg (max={:.2f})",
                    totalTimerHandlerMs / loopCount,
                    maxTimerHandlerMs);
            }
            loopCount = 0;
            totalLoopMs = 0.0;
            totalLoopMsSq = 0.0;
            minLoopMs = 1e9;
            maxLoopMs = 0.0;
            totalProcessEventsMs = 0.0;
            maxProcessEventsMs = 0.0;
            totalTimerHandlerMs = 0.0;
            maxTimerHandlerMs = 0.0;
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
