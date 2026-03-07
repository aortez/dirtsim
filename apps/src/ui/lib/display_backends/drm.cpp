/**
 * @file drm.cpp
 *
 * DRM/KMS display backend with hardware page flipping and vsync.
 */

#include <chrono>
#include <cmath>
#include <stdlib.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "ui/lib/LoopTimingStats.h"
#include "ui/state-machine/StateMachine.h"
#include <spdlog/spdlog.h>

#if LV_USE_LINUX_DRM
#include "ui/lib/backends.h"
#include "ui/lib/simulator_settings.h"
#include "ui/lib/simulator_util.h"

extern simulator_settings_t settings;

static lv_display_t* init_drm(void);
static void run_loop_drm(DirtSim::Ui::StateMachine& sm);

static const char* backend_name = "DRM";

int backend_init_drm(backend_t* backend)
{
    LV_ASSERT_NULL(backend);

    backend->handle->display = static_cast<display_backend_t*>(malloc(sizeof(display_backend_t)));
    LV_ASSERT_NULL(backend->handle->display);

    backend->handle->display->init_display = init_drm;
    backend->handle->display->run_loop = run_loop_drm;
    backend->name = backend_name;
    backend->type = BACKEND_DISPLAY;

    return 0;
}

static lv_display_t* init_drm(void)
{
    const char* device = getenv_default("LV_LINUX_DRM_DEVICE", "/dev/dri/card2");
    lv_display_t* disp = lv_linux_drm_create();

    if (disp == NULL) {
        return NULL;
    }

    // Set rotation BEFORE lv_linux_drm_set_file() so the DRM driver can set up
    // rotated draw buffers and FULL render mode when needed.
    const char* rotation_env = getenv("LV_DISPLAY_ROTATION");
    if (rotation_env != NULL) {
        const int rotation = atoi(rotation_env);
        switch (rotation) {
            case 90:
                lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
                spdlog::info("DRM: Display rotation set to 90 degrees");
                break;
            case 180:
                lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
                spdlog::info("DRM: Display rotation set to 180 degrees");
                break;
            case 270:
                lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
                spdlog::info("DRM: Display rotation set to 270 degrees");
                break;
            default:
                spdlog::info("DRM: Display rotation set to 0 degrees (default)");
                break;
        }
    }

    // Connector ID -1 means auto-select first connected output.
    lv_linux_drm_set_file(disp, device, -1);
    spdlog::info("DRM: Using device {}", device);

#if LV_USE_EVDEV
    const char* evdev_device = getenv_default("LV_EVDEV_DEVICE", "/dev/input/event0");
    lv_indev_t* indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, evdev_device);
    if (indev != NULL) {
        lv_indev_set_display(indev, disp);
        spdlog::info("DRM: Touchscreen input initialized from {}", evdev_device);
    }
    else {
        spdlog::warn("DRM: Failed to initialize touchscreen from {}", evdev_device);
    }
#endif

    return disp;
}

/**
 * DRM run loop. lv_timer_handler() blocks on vsync (~16.7ms at 60Hz) via the
 * DRM page flip wait, so no usleep or condvar is needed for frame pacing.
 */
static void run_loop_drm(DirtSim::Ui::StateMachine& sm)
{
    DirtSim::Ui::LoopTimingStats stats;

    while (!sm.shouldExit()) {
        auto loopStart = DirtSim::Ui::LoopTimingStats::Clock::now();

        auto eventsStart = DirtSim::Ui::LoopTimingStats::Clock::now();
        sm.processEvents();
        double eventsMs = std::chrono::duration<double, std::milli>(
                              DirtSim::Ui::LoopTimingStats::Clock::now() - eventsStart)
                              .count();

        sm.updateAnimations();

        // Renders into DRM buffer, submits page flip, blocks on vsync.
        auto timerStart = DirtSim::Ui::LoopTimingStats::Clock::now();
        lv_timer_handler();
        double timerMs = std::chrono::duration<double, std::milli>(
                             DirtSim::Ui::LoopTimingStats::Clock::now() - timerStart)
                             .count();

        // Drain events that arrived during the vsync wait.
        sm.processEvents();

        double loopMs = std::chrono::duration<double, std::milli>(
                            DirtSim::Ui::LoopTimingStats::Clock::now() - loopStart)
                            .count();
        stats.processEvents.record(eventsMs);
        stats.timerHandler.record(timerMs);
        stats.recordLoop(loopMs);

        if (stats.shouldLog()) {
            stats.logAndReset("DRM");
        }
    }

    // Process any final UI updates.
    for (int i = 0; i < 3; ++i) {
        lv_timer_handler();
        usleep(10000); // 10ms.
    }
}

#endif /* LV_USE_LINUX_DRM. */
