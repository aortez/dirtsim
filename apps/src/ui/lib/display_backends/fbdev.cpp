/**
 * @file fbdev.c
 *
 * Legacy framebuffer device
 *
 * Based on the original file from the repository
 *
 * Move to a separate file
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
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "ui/state-machine/StateMachine.h"
#include <spdlog/spdlog.h>

#if LV_USE_LINUX_FBDEV
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

static lv_display_t* init_fbdev(void);
static void run_loop_fbdev(DirtSim::Ui::StateMachine& sm);

/**********************
 *  STATIC VARIABLES
 **********************/

static const char* backend_name = "FBDEV";

/**********************
 *  EXTERNAL VARIABLES
 **********************/
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
int backend_init_fbdev(backend_t* backend)
{
    LV_ASSERT_NULL(backend);

    backend->handle->display = static_cast<display_backend_t*>(malloc(sizeof(display_backend_t)));
    LV_ASSERT_NULL(backend->handle->display);

    backend->handle->display->init_display = init_fbdev;
    backend->handle->display->run_loop = run_loop_fbdev;
    backend->name = backend_name;
    backend->type = BACKEND_DISPLAY;

    return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Initialize the fbdev driver
 *
 * @return the LVGL display
 */
static lv_display_t* init_fbdev(void)
{
    const char* device = getenv_default("LV_LINUX_FBDEV_DEVICE", "/dev/fb0");
    lv_display_t* disp = lv_linux_fbdev_create();

    if (disp == NULL) {
        return NULL;
    }

    lv_linux_fbdev_set_file(disp, device);

    // Check for display rotation via environment variable.
    // Useful when hardware rotates display (e.g., HyperPixel dtparam=rotate=90).
    // LVGL needs to match the rotation so content appears correctly.
    lv_display_rotation_t display_rotation = LV_DISPLAY_ROTATION_0;
    const char* rotation_env = getenv("LV_DISPLAY_ROTATION");
    if (rotation_env != NULL) {
        int rotation = atoi(rotation_env);
        switch (rotation) {
            case 90:
                display_rotation = LV_DISPLAY_ROTATION_90;
                lv_display_set_rotation(disp, display_rotation);
                spdlog::info("FBDEV: Display rotation set to 90 degrees");
                break;
            case 180:
                display_rotation = LV_DISPLAY_ROTATION_180;
                lv_display_set_rotation(disp, display_rotation);
                spdlog::info("FBDEV: Display rotation set to 180 degrees");
                break;
            case 270:
                display_rotation = LV_DISPLAY_ROTATION_270;
                lv_display_set_rotation(disp, display_rotation);
                spdlog::info("FBDEV: Display rotation set to 270 degrees");
                break;
            default:
                spdlog::info("FBDEV: Display rotation set to 0 degrees (default)");
                break;
        }
    }

#if LV_USE_EVDEV
    // Initialize touchscreen input via evdev.
    // Default device can be overridden with LV_EVDEV_DEVICE environment variable.
    const char* evdev_device = getenv_default("LV_EVDEV_DEVICE", "/dev/input/event0");
    lv_indev_t* indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, evdev_device);
    if (indev != NULL) {
        lv_indev_set_display(indev, disp);
        spdlog::info("FBDEV: Touchscreen input initialized from {}", evdev_device);
    }
    else {
        spdlog::warn("FBDEV: Failed to initialize touchscreen from {}", evdev_device);
    }
#endif

    return disp;
}

/**
 * The run loop of the fbdev driver
 */
static void run_loop_fbdev(DirtSim::Ui::StateMachine& sm)
{
    uint32_t idle_time;

    // Target ~30 FPS for smooth animation (33ms per frame).
    // LVGL may suggest longer sleep times when it thinks nothing changed,
    // but background threads (like the fractal renderer) may have invalidated
    // objects that need to be flushed.
    constexpr uint32_t MAX_IDLE_MS = 33;

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
    double totalSleepMs = 0.0;
    double maxSleepMs = 0.0;
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

        /* Returns the time to the next timer execution. */
        auto timerStart = Clock::now();
        idle_time = lv_timer_handler();
        double timerMs =
            std::chrono::duration<double, std::milli>(Clock::now() - timerStart).count();

        // Cap idle time to maintain responsiveness for background-invalidated objects.
        if (idle_time > MAX_IDLE_MS) {
            idle_time = MAX_IDLE_MS;
        }
        auto sleepStart = Clock::now();
        usleep(idle_time * 1000);
        double sleepMs =
            std::chrono::duration<double, std::milli>(Clock::now() - sleepStart).count();

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
        totalSleepMs += sleepMs;
        if (sleepMs > maxSleepMs) {
            maxSleepMs = sleepMs;
        }
        loopCount++;

        if (Clock::now() - lastLoopLog >= std::chrono::seconds(10)) {
            lastLoopLog = Clock::now();
            if (loopCount > 0) {
                const double avgLoop = totalLoopMs / loopCount;
                const double variance = (totalLoopMsSq / loopCount) - (avgLoop * avgLoop);
                const double stddev = variance > 0.0 ? std::sqrt(variance) : 0.0;
                spdlog::info("FBDEV loop timing ({} iters):", loopCount);
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
                    "  lv_timer_handler: {:.2f}ms avg (max={:.2f})",
                    totalTimerHandlerMs / loopCount,
                    maxTimerHandlerMs);
                spdlog::info(
                    "  usleep: {:.2f}ms avg (max={:.2f})", totalSleepMs / loopCount, maxSleepMs);
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
            totalSleepMs = 0.0;
            maxSleepMs = 0.0;
        }
    }

    // Process any final UI updates.
    for (int i = 0; i < 3; ++i) {
        lv_timer_handler();
        usleep(10000); // 10ms.
    }

    // TODO: Take exit screenshot.
    // SimulatorUI::takeExitScreenshot();
}

#endif /*LV_USE_LINUX_FBDEV. */
