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

    /* Handle LVGL tasks. */
    while (!sm.shouldExit()) {
        // Process UI state machine events.
        sm.processEvents();

        // Update background animations (event-driven, no timer).
        sm.updateAnimations();

        /* Returns the time to the next timer execution. */
        idle_time = lv_timer_handler();

        // Cap idle time to maintain responsiveness for background-invalidated objects.
        if (idle_time > MAX_IDLE_MS) {
            idle_time = MAX_IDLE_MS;
        }
        usleep(idle_time * 1000);
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
