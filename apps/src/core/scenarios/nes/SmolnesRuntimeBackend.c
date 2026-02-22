#include "SmolnesRuntimeBackend.h"

#include <SDL2/SDL.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_MSC_VER)
#define SMOLNES_THREAD_LOCAL __declspec(thread)
#else
#define SMOLNES_THREAD_LOCAL __thread
#endif

struct SmolnesRuntimeHandle {
    pthread_cond_t runtimeCond;
    pthread_mutex_t runtimeMutex;

    pthread_t runtimeThread;
    bool hasLatestFrame;
    bool hasMemorySnapshot;
    bool healthy;
    bool stopRequested;
    bool threadJoinable;
    bool threadRunning;

    uint64_t latestFrameId;
    uint64_t renderedFrames;
    uint64_t targetFrames;

    uint8_t controller1State;
    uint8_t cpuRamSnapshot[SMOLNES_RUNTIME_CPU_RAM_BYTES];
    uint8_t keyboardState[SDL_NUM_SCANCODES];
    uint8_t latestFrame[SMOLNES_RUNTIME_FRAME_BYTES];
    uint8_t prgRamSnapshot[SMOLNES_RUNTIME_PRG_RAM_BYTES];
    uint8_t rendererStub;
    uint8_t textureStub;
    uint8_t windowStub;

    char lastError[256];
    char romPath[1024];
};

static SMOLNES_THREAD_LOCAL SmolnesRuntimeHandle* gCurrentRuntime = NULL;
static const uint8_t gEmptyKeyboardState[SDL_NUM_SCANCODES] = { 0 };

int smolnesRuntimeEntryPoint(int argc, char** argv);

static void clearLastErrorLocked(SmolnesRuntimeHandle* runtime)
{
    runtime->lastError[0] = '\0';
}

static void setLastErrorLocked(SmolnesRuntimeHandle* runtime, const char* message)
{
    if (message == NULL) {
        clearLastErrorLocked(runtime);
        return;
    }
    snprintf(runtime->lastError, sizeof(runtime->lastError), "%s", message);
}

static void setLastError(SmolnesRuntimeHandle* runtime, const char* message)
{
    if (runtime == NULL) {
        return;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    setLastErrorLocked(runtime, message);
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

static void applyController1StateToKeyboardLocked(SmolnesRuntimeHandle* runtime)
{
    memset(runtime->keyboardState, 0, sizeof(runtime->keyboardState));

    runtime->keyboardState[SDL_SCANCODE_X] =
        (runtime->controller1State & SMOLNES_RUNTIME_BUTTON_A) ? 1 : 0;
    runtime->keyboardState[SDL_SCANCODE_Z] =
        (runtime->controller1State & SMOLNES_RUNTIME_BUTTON_B) ? 1 : 0;
    runtime->keyboardState[SDL_SCANCODE_TAB] =
        (runtime->controller1State & SMOLNES_RUNTIME_BUTTON_SELECT) ? 1 : 0;
    runtime->keyboardState[SDL_SCANCODE_RETURN] =
        (runtime->controller1State & SMOLNES_RUNTIME_BUTTON_START) ? 1 : 0;
    runtime->keyboardState[SDL_SCANCODE_UP] =
        (runtime->controller1State & SMOLNES_RUNTIME_BUTTON_UP) ? 1 : 0;
    runtime->keyboardState[SDL_SCANCODE_DOWN] =
        (runtime->controller1State & SMOLNES_RUNTIME_BUTTON_DOWN) ? 1 : 0;
    runtime->keyboardState[SDL_SCANCODE_LEFT] =
        (runtime->controller1State & SMOLNES_RUNTIME_BUTTON_LEFT) ? 1 : 0;
    runtime->keyboardState[SDL_SCANCODE_RIGHT] =
        (runtime->controller1State & SMOLNES_RUNTIME_BUTTON_RIGHT) ? 1 : 0;
}

static struct timespec buildDeadline(uint32_t timeoutMs)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);

    deadline.tv_sec += timeoutMs / 1000;
    deadline.tv_nsec += (long)(timeoutMs % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }
    return deadline;
}

static SmolnesRuntimeHandle* getCurrentRuntime(void)
{
    return gCurrentRuntime;
}

static void refreshMemorySnapshotLocked(SmolnesRuntimeHandle* runtime);

static void* runtimeThreadMain(void* arg)
{
    SmolnesRuntimeHandle* runtime = (SmolnesRuntimeHandle*)arg;
    if (runtime == NULL) {
        return NULL;
    }

    gCurrentRuntime = runtime;
    char* argv[] = { "smolnes", runtime->romPath };
    const int exitCode = smolnesRuntimeEntryPoint(2, argv);

    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->threadRunning = false;
    if (!runtime->stopRequested && exitCode != 0) {
        runtime->healthy = false;
        setLastErrorLocked(runtime, "smolnes runtime exited with an error.");
    }
    pthread_cond_broadcast(&runtime->runtimeCond);
    pthread_mutex_unlock(&runtime->runtimeMutex);
    gCurrentRuntime = NULL;
    return NULL;
}

int smolnesRuntimeWrappedInit(Uint32 flags)
{
    (void)flags;

    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return -1;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    applyController1StateToKeyboardLocked(runtime);
    pthread_mutex_unlock(&runtime->runtimeMutex);
    return 0;
}

const Uint8* smolnesRuntimeWrappedGetKeyboardState(int* numkeys)
{
    if (numkeys != NULL) {
        *numkeys = SDL_NUM_SCANCODES;
    }

    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return gEmptyKeyboardState;
    }
    return runtime->keyboardState;
}

SDL_Window* smolnesRuntimeWrappedCreateWindow(
    const char* title, int x, int y, int w, int h, Uint32 flags)
{
    (void)title;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)flags;

    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return NULL;
    }
    return (SDL_Window*)&runtime->windowStub;
}

SDL_Renderer* smolnesRuntimeWrappedCreateRenderer(SDL_Window* window, int index, Uint32 flags)
{
    (void)window;
    (void)index;
    (void)flags;

    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return NULL;
    }
    return (SDL_Renderer*)&runtime->rendererStub;
}

SDL_Texture* smolnesRuntimeWrappedCreateTexture(
    SDL_Renderer* renderer, Uint32 format, int access, int w, int h)
{
    (void)renderer;
    (void)format;
    (void)access;
    (void)w;
    (void)h;

    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return NULL;
    }
    return (SDL_Texture*)&runtime->textureStub;
}

int smolnesRuntimeWrappedUpdateTexture(
    SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch)
{
    (void)texture;

    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return -1;
    }

    if (pixels == NULL) {
        return 0;
    }

    if (rect != NULL) {
        return 0;
    }

    if (pitch < (int)SMOLNES_RUNTIME_FRAME_PITCH_BYTES) {
        return 0;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    for (uint32_t row = 0; row < SMOLNES_RUNTIME_FRAME_HEIGHT; ++row) {
        const uint8_t* src = (const uint8_t*)pixels + ((size_t)row * (size_t)pitch);
        uint8_t* dst = runtime->latestFrame + (row * SMOLNES_RUNTIME_FRAME_PITCH_BYTES);
        memcpy(dst, src, SMOLNES_RUNTIME_FRAME_PITCH_BYTES);
    }
    runtime->hasLatestFrame = true;
    pthread_mutex_unlock(&runtime->runtimeMutex);

    return 0;
}

int smolnesRuntimeWrappedRenderCopy(
    SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcRect, const SDL_Rect* dstRect)
{
    (void)renderer;
    (void)texture;
    (void)srcRect;
    (void)dstRect;
    return 0;
}

void smolnesRuntimeWrappedRenderPresent(SDL_Renderer* renderer)
{
    (void)renderer;

    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    while (!runtime->stopRequested && runtime->renderedFrames >= runtime->targetFrames) {
        pthread_cond_wait(&runtime->runtimeCond, &runtime->runtimeMutex);
    }
    if (!runtime->stopRequested) {
        refreshMemorySnapshotLocked(runtime);
        ++runtime->renderedFrames;
        runtime->latestFrameId = runtime->renderedFrames;
        pthread_cond_broadcast(&runtime->runtimeCond);
    }
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

int smolnesRuntimeWrappedPollEvent(SDL_Event* event)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return 1;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    const bool shouldStop = runtime->stopRequested;
    pthread_mutex_unlock(&runtime->runtimeMutex);

    if (!shouldStop) {
        return 0;
    }

    if (event != NULL) {
        memset(event, 0, sizeof(*event));
        event->type = SDL_QUIT;
    }
    return 1;
}

#define SDL_CreateWindow smolnesRuntimeWrappedCreateWindow
#define SDL_CreateRenderer smolnesRuntimeWrappedCreateRenderer
#define SDL_CreateTexture smolnesRuntimeWrappedCreateTexture
#define SDL_GetKeyboardState smolnesRuntimeWrappedGetKeyboardState
#define SDL_Init smolnesRuntimeWrappedInit
#define SDL_PollEvent smolnesRuntimeWrappedPollEvent
#define SDL_RenderCopy smolnesRuntimeWrappedRenderCopy
#define SDL_RenderPresent smolnesRuntimeWrappedRenderPresent
#define SDL_UpdateTexture smolnesRuntimeWrappedUpdateTexture
#define SMOLNES_TLS SMOLNES_THREAD_LOCAL
#define main smolnesRuntimeEntryPoint

#include "../../../../external/smolnes/deobfuscated.c"

#undef SDL_CreateWindow
#undef SDL_CreateRenderer
#undef SDL_CreateTexture
#undef SDL_GetKeyboardState
#undef SDL_Init
#undef SDL_PollEvent
#undef SDL_RenderCopy
#undef SDL_RenderPresent
#undef SDL_UpdateTexture
#undef SMOLNES_TLS
#undef main

static void refreshMemorySnapshotLocked(SmolnesRuntimeHandle* runtime)
{
    memcpy(runtime->cpuRamSnapshot, ram, SMOLNES_RUNTIME_CPU_RAM_BYTES);
    memcpy(runtime->prgRamSnapshot, prgram, SMOLNES_RUNTIME_PRG_RAM_BYTES);
    runtime->hasMemorySnapshot = true;
}

SmolnesRuntimeHandle* smolnesRuntimeCreate(void)
{
    SmolnesRuntimeHandle* runtime =
        (SmolnesRuntimeHandle*)calloc(1u, sizeof(SmolnesRuntimeHandle));
    if (runtime == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&runtime->runtimeMutex, NULL) != 0) {
        free(runtime);
        return NULL;
    }
    if (pthread_cond_init(&runtime->runtimeCond, NULL) != 0) {
        pthread_mutex_destroy(&runtime->runtimeMutex);
        free(runtime);
        return NULL;
    }

    return runtime;
}

void smolnesRuntimeDestroy(SmolnesRuntimeHandle* runtime)
{
    if (runtime == NULL) {
        return;
    }

    smolnesRuntimeStop(runtime);
    pthread_cond_destroy(&runtime->runtimeCond);
    pthread_mutex_destroy(&runtime->runtimeMutex);
    free(runtime);
}

bool smolnesRuntimeStart(SmolnesRuntimeHandle* runtime, const char* romPath)
{
    if (runtime == NULL) {
        return false;
    }

    if (romPath == NULL || romPath[0] == '\0') {
        setLastError(runtime, "ROM path is empty.");
        return false;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    if (runtime->threadRunning) {
        setLastErrorLocked(runtime, "smolnes runtime is already running.");
        pthread_mutex_unlock(&runtime->runtimeMutex);
        return false;
    }
    const bool joinOldThread = runtime->threadJoinable;
    pthread_mutex_unlock(&runtime->runtimeMutex);

    if (joinOldThread) {
        pthread_join(runtime->runtimeThread, NULL);
        pthread_mutex_lock(&runtime->runtimeMutex);
        runtime->threadJoinable = false;
        pthread_mutex_unlock(&runtime->runtimeMutex);
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    clearLastErrorLocked(runtime);

    snprintf(runtime->romPath, sizeof(runtime->romPath), "%s", romPath);

    runtime->stopRequested = false;
    runtime->healthy = true;
    runtime->renderedFrames = 0;
    runtime->targetFrames = 0;
    runtime->latestFrameId = 0;
    runtime->hasLatestFrame = false;
    runtime->hasMemorySnapshot = false;
    memset(runtime->latestFrame, 0, sizeof(runtime->latestFrame));
    memset(runtime->cpuRamSnapshot, 0, sizeof(runtime->cpuRamSnapshot));
    memset(runtime->prgRamSnapshot, 0, sizeof(runtime->prgRamSnapshot));
    runtime->controller1State = 0;
    applyController1StateToKeyboardLocked(runtime);
    runtime->threadRunning = true;

    const int createResult = pthread_create(&runtime->runtimeThread, NULL, runtimeThreadMain, runtime);
    if (createResult != 0) {
        runtime->threadRunning = false;
        runtime->healthy = false;
        setLastErrorLocked(runtime, "Failed to start smolnes runtime thread.");
        pthread_mutex_unlock(&runtime->runtimeMutex);
        return false;
    }

    runtime->threadJoinable = true;
    pthread_mutex_unlock(&runtime->runtimeMutex);
    return true;
}

bool smolnesRuntimeRunFrames(SmolnesRuntimeHandle* runtime, uint32_t frameCount, uint32_t timeoutMs)
{
    if (runtime == NULL) {
        return false;
    }

    if (frameCount == 0) {
        return true;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    if (!runtime->threadRunning || !runtime->healthy) {
        setLastErrorLocked(runtime, "smolnes runtime is not healthy.");
        pthread_mutex_unlock(&runtime->runtimeMutex);
        return false;
    }

    const uint64_t requestedFrames = runtime->targetFrames + frameCount;
    runtime->targetFrames = requestedFrames;
    pthread_cond_broadcast(&runtime->runtimeCond);

    const struct timespec deadline = buildDeadline(timeoutMs);
    while (runtime->renderedFrames < requestedFrames && runtime->threadRunning && runtime->healthy) {
        int waitResult = 0;
        if (timeoutMs == 0) {
            waitResult = pthread_cond_wait(&runtime->runtimeCond, &runtime->runtimeMutex);
        }
        else {
            waitResult = pthread_cond_timedwait(&runtime->runtimeCond, &runtime->runtimeMutex, &deadline);
        }

        if (waitResult == ETIMEDOUT) {
            runtime->healthy = false;
            setLastErrorLocked(runtime, "Timed out waiting for smolnes frame progression.");
            pthread_mutex_unlock(&runtime->runtimeMutex);
            return false;
        }
    }

    if (runtime->renderedFrames < requestedFrames) {
        runtime->healthy = false;
        setLastErrorLocked(runtime, "smolnes runtime stopped before requested frames completed.");
        pthread_mutex_unlock(&runtime->runtimeMutex);
        return false;
    }

    pthread_mutex_unlock(&runtime->runtimeMutex);
    return true;
}

void smolnesRuntimeStop(SmolnesRuntimeHandle* runtime)
{
    if (runtime == NULL) {
        return;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    const bool joinThread = runtime->threadJoinable;
    runtime->stopRequested = true;
    pthread_cond_broadcast(&runtime->runtimeCond);
    pthread_mutex_unlock(&runtime->runtimeMutex);

    if (joinThread) {
        pthread_join(runtime->runtimeThread, NULL);
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->threadJoinable = false;
    runtime->threadRunning = false;
    runtime->stopRequested = false;
    runtime->targetFrames = runtime->renderedFrames;
    pthread_cond_broadcast(&runtime->runtimeCond);
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

bool smolnesRuntimeIsHealthy(const SmolnesRuntimeHandle* runtime)
{
    if (runtime == NULL) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    const bool healthy = mutableRuntime->healthy;
    pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
    return healthy;
}

bool smolnesRuntimeIsRunning(const SmolnesRuntimeHandle* runtime)
{
    if (runtime == NULL) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    const bool running = mutableRuntime->threadRunning;
    pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
    return running;
}

uint64_t smolnesRuntimeGetRenderedFrameCount(const SmolnesRuntimeHandle* runtime)
{
    if (runtime == NULL) {
        return 0;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    const uint64_t frameCount = mutableRuntime->renderedFrames;
    pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
    return frameCount;
}

void smolnesRuntimeSetController1State(SmolnesRuntimeHandle* runtime, uint8_t buttonMask)
{
    if (runtime == NULL) {
        return;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->controller1State = buttonMask;
    applyController1StateToKeyboardLocked(runtime);
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

bool smolnesRuntimeCopyLatestFrame(
    const SmolnesRuntimeHandle* runtime, uint8_t* buffer, uint32_t bufferSize, uint64_t* frameId)
{
    if (runtime == NULL || buffer == NULL || bufferSize < SMOLNES_RUNTIME_FRAME_BYTES) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    if (!mutableRuntime->hasLatestFrame) {
        pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
        return false;
    }

    memcpy(buffer, mutableRuntime->latestFrame, SMOLNES_RUNTIME_FRAME_BYTES);
    if (frameId != NULL) {
        *frameId = mutableRuntime->latestFrameId;
    }
    pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
    return true;
}

bool smolnesRuntimeCopyCpuRam(const SmolnesRuntimeHandle* runtime, uint8_t* buffer, uint32_t bufferSize)
{
    if (runtime == NULL || buffer == NULL || bufferSize < SMOLNES_RUNTIME_CPU_RAM_BYTES) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    if (!mutableRuntime->threadRunning || !mutableRuntime->healthy || !mutableRuntime->hasMemorySnapshot) {
        pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
        return false;
    }

    memcpy(buffer, mutableRuntime->cpuRamSnapshot, SMOLNES_RUNTIME_CPU_RAM_BYTES);
    pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
    return true;
}

bool smolnesRuntimeCopyPrgRam(const SmolnesRuntimeHandle* runtime, uint8_t* buffer, uint32_t bufferSize)
{
    if (runtime == NULL || buffer == NULL || bufferSize < SMOLNES_RUNTIME_PRG_RAM_BYTES) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    if (!mutableRuntime->threadRunning || !mutableRuntime->healthy || !mutableRuntime->hasMemorySnapshot) {
        pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
        return false;
    }

    memcpy(buffer, mutableRuntime->prgRamSnapshot, SMOLNES_RUNTIME_PRG_RAM_BYTES);
    pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
    return true;
}

void smolnesRuntimeGetLastErrorCopy(
    const SmolnesRuntimeHandle* runtime, char* buffer, uint32_t bufferSize)
{
    if (buffer == NULL || bufferSize == 0) {
        return;
    }

    if (runtime == NULL) {
        buffer[0] = '\0';
        return;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    snprintf(buffer, bufferSize, "%s", mutableRuntime->lastError);
    pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
}
