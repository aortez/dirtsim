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

extern SMOLNES_THREAD_LOCAL uint8_t frame_buffer_palette[61440];

struct SmolnesRuntimeHandle {
    pthread_cond_t runtimeCond;
    pthread_mutex_t runtimeMutex;

    pthread_t runtimeThread;
    bool hasLatestFrame;
    bool hasLatestPaletteFrame;
    bool hasMemorySnapshot;
    bool healthy;
    bool stopRequested;
    bool threadJoinable;
    bool threadRunning;

    uint64_t latestFrameId;
    uint64_t renderedFrames;
    uint64_t targetFrames;

    double runFramesWaitMs;
    uint64_t runFramesWaitCalls;
    double runtimeThreadIdleWaitMs;
    uint64_t runtimeThreadIdleWaitCalls;
    double runtimeThreadCpuStepMs;
    uint64_t runtimeThreadCpuStepCalls;
    double runtimeThreadFrameExecutionMs;
    uint64_t runtimeThreadFrameExecutionCalls;
    double runtimeThreadPpuStepMs;
    uint64_t runtimeThreadPpuStepCalls;
    double runtimeThreadPpuVisiblePixelsMs;
    uint64_t runtimeThreadPpuVisiblePixelsCalls;
    double runtimeThreadPpuSpriteEvalMs;
    uint64_t runtimeThreadPpuSpriteEvalCalls;
    double runtimeThreadPpuPrefetchMs;
    uint64_t runtimeThreadPpuPrefetchCalls;
    double runtimeThreadPpuOtherMs;
    uint64_t runtimeThreadPpuOtherCalls;
    double runtimeThreadFrameSubmitMs;
    uint64_t runtimeThreadFrameSubmitCalls;
    double runtimeThreadEventPollMs;
    uint64_t runtimeThreadEventPollCalls;
    double runtimeThreadPresentMs;
    uint64_t runtimeThreadPresentCalls;
    double memorySnapshotCopyMs;
    uint64_t memorySnapshotCopyCalls;

    uint8_t controller1State;
    uint8_t cpuRamSnapshot[SMOLNES_RUNTIME_CPU_RAM_BYTES];
    uint8_t latestFrame[SMOLNES_RUNTIME_FRAME_BYTES];
    uint8_t latestPaletteFrame[SMOLNES_RUNTIME_PALETTE_FRAME_BYTES];
    uint8_t prgRamSnapshot[SMOLNES_RUNTIME_PRG_RAM_BYTES];
    uint8_t rendererStub;
    uint8_t textureStub;
    uint8_t windowStub;

    char lastError[256];
    char romPath[1024];
};

static SMOLNES_THREAD_LOCAL SmolnesRuntimeHandle* gCurrentRuntime = NULL;
static const uint8_t gEmptyKeyboardState[SDL_NUM_SCANCODES] = { 0 };
static SMOLNES_THREAD_LOCAL uint8_t gThreadKeyboardState[SDL_NUM_SCANCODES] = { 0 };
static SMOLNES_THREAD_LOCAL bool gCpuStepActive = false;
static SMOLNES_THREAD_LOCAL double gCpuStepStartMs = 0.0;
static SMOLNES_THREAD_LOCAL bool gEventPollActive = false;
static SMOLNES_THREAD_LOCAL double gEventPollStartMs = 0.0;
static SMOLNES_THREAD_LOCAL bool gFrameExecutionActive = false;
static SMOLNES_THREAD_LOCAL double gFrameExecutionStartMs = 0.0;
static SMOLNES_THREAD_LOCAL bool gPpuStepActive = false;
static SMOLNES_THREAD_LOCAL double gPpuStepStartMs = 0.0;
typedef enum SmolnesPpuPhaseBucket {
    SmolnesPpuPhaseBucketNone = 0,
    SmolnesPpuPhaseBucketVisiblePixels = 1,
    SmolnesPpuPhaseBucketPrefetch = 2,
    SmolnesPpuPhaseBucketOther = 3,
    SmolnesPpuPhaseBucketSpriteEval = 4
} SmolnesPpuPhaseBucket;
static SMOLNES_THREAD_LOCAL SmolnesPpuPhaseBucket gPpuPhaseBucket = SmolnesPpuPhaseBucketNone;
static SMOLNES_THREAD_LOCAL double gPpuPhaseBucketStartMs = 0.0;
static SMOLNES_THREAD_LOCAL double gPpuVisiblePixelsAccumMs = 0.0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuVisiblePixelsAccumCalls = 0;
static SMOLNES_THREAD_LOCAL double gPpuSpriteEvalAccumMs = 0.0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuSpriteEvalAccumCalls = 0;
static SMOLNES_THREAD_LOCAL double gPpuPrefetchAccumMs = 0.0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuPrefetchAccumCalls = 0;
static SMOLNES_THREAD_LOCAL double gPpuOtherAccumMs = 0.0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuOtherAccumCalls = 0;
static SMOLNES_THREAD_LOCAL bool gFrameSubmitActive = false;
static SMOLNES_THREAD_LOCAL double gFrameSubmitStartMs = 0.0;

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

static void mapController1StateToKeyboard(uint8_t controller1State, uint8_t* keyboardState)
{
    memset(keyboardState, 0, SDL_NUM_SCANCODES * sizeof(uint8_t));

    keyboardState[SDL_SCANCODE_X] = (controller1State & SMOLNES_RUNTIME_BUTTON_A) ? 1 : 0;
    keyboardState[SDL_SCANCODE_Z] = (controller1State & SMOLNES_RUNTIME_BUTTON_B) ? 1 : 0;
    keyboardState[SDL_SCANCODE_TAB] = (controller1State & SMOLNES_RUNTIME_BUTTON_SELECT) ? 1 : 0;
    keyboardState[SDL_SCANCODE_RETURN] = (controller1State & SMOLNES_RUNTIME_BUTTON_START) ? 1 : 0;
    keyboardState[SDL_SCANCODE_UP] = (controller1State & SMOLNES_RUNTIME_BUTTON_UP) ? 1 : 0;
    keyboardState[SDL_SCANCODE_DOWN] = (controller1State & SMOLNES_RUNTIME_BUTTON_DOWN) ? 1 : 0;
    keyboardState[SDL_SCANCODE_LEFT] = (controller1State & SMOLNES_RUNTIME_BUTTON_LEFT) ? 1 : 0;
    keyboardState[SDL_SCANCODE_RIGHT] = (controller1State & SMOLNES_RUNTIME_BUTTON_RIGHT) ? 1 : 0;
}

static void refreshThreadKeyboardStateFromRuntime(SmolnesRuntimeHandle* runtime)
{
    if (runtime == NULL) {
        memset(gThreadKeyboardState, 0, SDL_NUM_SCANCODES * sizeof(uint8_t));
        return;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    const uint8_t controller1State = runtime->controller1State;
    pthread_mutex_unlock(&runtime->runtimeMutex);
    mapController1StateToKeyboard(controller1State, gThreadKeyboardState);
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

static double monotonicNowMs(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return ((double)now.tv_sec * 1000.0) + ((double)now.tv_nsec / 1000000.0);
}

static void resetPpuPhaseBreakdown(void)
{
    gPpuPhaseBucket = SmolnesPpuPhaseBucketNone;
    gPpuPhaseBucketStartMs = 0.0;
    gPpuVisiblePixelsAccumMs = 0.0;
    gPpuVisiblePixelsAccumCalls = 0;
    gPpuSpriteEvalAccumMs = 0.0;
    gPpuSpriteEvalAccumCalls = 0;
    gPpuPrefetchAccumMs = 0.0;
    gPpuPrefetchAccumCalls = 0;
    gPpuOtherAccumMs = 0.0;
    gPpuOtherAccumCalls = 0;
}

static void accumulatePpuPhaseDuration(SmolnesPpuPhaseBucket phase, double durationMs)
{
    if (durationMs <= 0.0) {
        return;
    }

    switch (phase) {
    case SmolnesPpuPhaseBucketVisiblePixels:
        gPpuVisiblePixelsAccumMs += durationMs;
        gPpuVisiblePixelsAccumCalls++;
        break;
    case SmolnesPpuPhaseBucketSpriteEval:
        gPpuSpriteEvalAccumMs += durationMs;
        gPpuSpriteEvalAccumCalls++;
        break;
    case SmolnesPpuPhaseBucketPrefetch:
        gPpuPrefetchAccumMs += durationMs;
        gPpuPrefetchAccumCalls++;
        break;
    case SmolnesPpuPhaseBucketOther:
        gPpuOtherAccumMs += durationMs;
        gPpuOtherAccumCalls++;
        break;
    case SmolnesPpuPhaseBucketNone:
    default:
        break;
    }
}

static void setPpuPhaseBucket(SmolnesPpuPhaseBucket nextPhase)
{
    if (nextPhase == gPpuPhaseBucket) {
        return;
    }

    const double nowMs = monotonicNowMs();
    if (gPpuPhaseBucket != SmolnesPpuPhaseBucketNone && gPpuPhaseBucketStartMs > 0.0) {
        accumulatePpuPhaseDuration(gPpuPhaseBucket, nowMs - gPpuPhaseBucketStartMs);
    }

    gPpuPhaseBucket = nextPhase;
    gPpuPhaseBucketStartMs = (nextPhase == SmolnesPpuPhaseBucketNone) ? 0.0 : nowMs;
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
    gCpuStepActive = false;
    gCpuStepStartMs = 0.0;
    gEventPollActive = false;
    gEventPollStartMs = 0.0;
    gFrameExecutionActive = false;
    gFrameExecutionStartMs = 0.0;
    gPpuStepActive = false;
    gPpuStepStartMs = 0.0;
    resetPpuPhaseBreakdown();
    gFrameSubmitActive = false;
    gFrameSubmitStartMs = 0.0;
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
    gCpuStepActive = false;
    gCpuStepStartMs = 0.0;
    gEventPollActive = false;
    gEventPollStartMs = 0.0;
    gFrameExecutionActive = false;
    gFrameExecutionStartMs = 0.0;
    gPpuStepActive = false;
    gPpuStepStartMs = 0.0;
    resetPpuPhaseBreakdown();
    gFrameSubmitActive = false;
    gFrameSubmitStartMs = 0.0;
    return NULL;
}

int smolnesRuntimeWrappedInit(Uint32 flags)
{
    (void)flags;

    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return -1;
    }

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

    pthread_mutex_lock(&runtime->runtimeMutex);
    const uint8_t controller1State = runtime->controller1State;
    pthread_mutex_unlock(&runtime->runtimeMutex);
    mapController1StateToKeyboard(controller1State, gThreadKeyboardState);
    return gThreadKeyboardState;
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
    const uint8_t* paletteSrc =
        frame_buffer_palette + (SMOLNES_RUNTIME_FRAME_WIDTH * 8u);
    for (uint32_t row = 0; row < SMOLNES_RUNTIME_FRAME_HEIGHT; ++row) {
        const uint8_t* src = paletteSrc + (row * SMOLNES_RUNTIME_FRAME_WIDTH);
        uint8_t* dst = runtime->latestPaletteFrame + (row * SMOLNES_RUNTIME_FRAME_WIDTH);
        memcpy(dst, src, SMOLNES_RUNTIME_FRAME_WIDTH);
    }
    runtime->hasLatestFrame = true;
    runtime->hasLatestPaletteFrame = true;
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

void smolnesRuntimeWrappedCpuStepBegin(void)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL || gCpuStepActive) {
        return;
    }

    gCpuStepStartMs = monotonicNowMs();
    gCpuStepActive = true;
}

void smolnesRuntimeWrappedCpuStepEnd(void)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL || !gCpuStepActive) {
        return;
    }

    const double cpuStepMs = monotonicNowMs() - gCpuStepStartMs;
    gCpuStepActive = false;
    gCpuStepStartMs = 0.0;
    if (cpuStepMs <= 0.0) {
        return;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->runtimeThreadCpuStepMs += cpuStepMs;
    runtime->runtimeThreadCpuStepCalls++;
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

void smolnesRuntimeWrappedFrameExecutionBegin(void)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return;
    }

    refreshThreadKeyboardStateFromRuntime(runtime);
    gFrameExecutionStartMs = monotonicNowMs();
    gFrameExecutionActive = true;
}

void smolnesRuntimeWrappedFrameExecutionEnd(void)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL || !gFrameExecutionActive) {
        return;
    }

    const double frameExecutionMs = monotonicNowMs() - gFrameExecutionStartMs;
    gFrameExecutionActive = false;
    gFrameExecutionStartMs = 0.0;
    if (frameExecutionMs <= 0.0) {
        return;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->runtimeThreadFrameExecutionMs += frameExecutionMs;
    runtime->runtimeThreadFrameExecutionCalls++;
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

void smolnesRuntimeWrappedPpuStepBegin(void)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL || gPpuStepActive) {
        return;
    }

    resetPpuPhaseBreakdown();
    gPpuStepStartMs = monotonicNowMs();
    gPpuStepActive = true;
}

void smolnesRuntimeWrappedPpuStepEnd(void)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL || !gPpuStepActive) {
        return;
    }

    setPpuPhaseBucket(SmolnesPpuPhaseBucketNone);
    const double ppuStepMs = monotonicNowMs() - gPpuStepStartMs;
    gPpuStepActive = false;
    gPpuStepStartMs = 0.0;
    if (ppuStepMs <= 0.0) {
        return;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->runtimeThreadPpuStepMs += ppuStepMs;
    runtime->runtimeThreadPpuStepCalls++;
    runtime->runtimeThreadPpuVisiblePixelsMs += gPpuVisiblePixelsAccumMs;
    runtime->runtimeThreadPpuVisiblePixelsCalls += gPpuVisiblePixelsAccumCalls;
    runtime->runtimeThreadPpuSpriteEvalMs += gPpuSpriteEvalAccumMs;
    runtime->runtimeThreadPpuSpriteEvalCalls += gPpuSpriteEvalAccumCalls;
    runtime->runtimeThreadPpuPrefetchMs += gPpuPrefetchAccumMs;
    runtime->runtimeThreadPpuPrefetchCalls += gPpuPrefetchAccumCalls;
    runtime->runtimeThreadPpuOtherMs += gPpuOtherAccumMs;
    runtime->runtimeThreadPpuOtherCalls += gPpuOtherAccumCalls;
    pthread_mutex_unlock(&runtime->runtimeMutex);
    resetPpuPhaseBreakdown();
}

void smolnesRuntimeWrappedPpuPhaseSet(uint32_t phaseId)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL || !gPpuStepActive) {
        return;
    }

    SmolnesPpuPhaseBucket nextPhase = SmolnesPpuPhaseBucketOther;
    switch (phaseId) {
    case 1u:
        nextPhase = SmolnesPpuPhaseBucketVisiblePixels;
        break;
    case 2u:
        nextPhase = SmolnesPpuPhaseBucketPrefetch;
        break;
    case 3u:
        nextPhase = SmolnesPpuPhaseBucketOther;
        break;
    case 4u:
        nextPhase = SmolnesPpuPhaseBucketSpriteEval;
        break;
    default:
        nextPhase = SmolnesPpuPhaseBucketNone;
        break;
    }
    setPpuPhaseBucket(nextPhase);
}

void smolnesRuntimeWrappedPpuPhaseClear(void)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL || !gPpuStepActive) {
        return;
    }

    setPpuPhaseBucket(SmolnesPpuPhaseBucketNone);
}

void smolnesRuntimeWrappedFrameSubmitBegin(void)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return;
    }

    gFrameSubmitStartMs = monotonicNowMs();
    gFrameSubmitActive = true;
}

void smolnesRuntimeWrappedFrameSubmitEnd(void)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL || !gFrameSubmitActive) {
        return;
    }

    const double frameSubmitMs = monotonicNowMs() - gFrameSubmitStartMs;
    gFrameSubmitActive = false;
    gFrameSubmitStartMs = 0.0;
    if (frameSubmitMs <= 0.0) {
        return;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->runtimeThreadFrameSubmitMs += frameSubmitMs;
    runtime->runtimeThreadFrameSubmitCalls++;
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

void smolnesRuntimeWrappedEventPollBegin(void)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return;
    }

    gEventPollStartMs = monotonicNowMs();
    gEventPollActive = true;
}

void smolnesRuntimeWrappedEventPollEnd(void)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL || !gEventPollActive) {
        return;
    }

    const double eventPollMs = monotonicNowMs() - gEventPollStartMs;
    gEventPollActive = false;
    gEventPollStartMs = 0.0;
    if (eventPollMs <= 0.0) {
        return;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->runtimeThreadEventPollMs += eventPollMs;
    runtime->runtimeThreadEventPollCalls++;
    pthread_mutex_unlock(&runtime->runtimeMutex);
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
        const double waitStartMs = monotonicNowMs();
        pthread_cond_wait(&runtime->runtimeCond, &runtime->runtimeMutex);
        runtime->runtimeThreadIdleWaitMs += monotonicNowMs() - waitStartMs;
        runtime->runtimeThreadIdleWaitCalls++;
    }
    if (!runtime->stopRequested) {
        const double presentStartMs = monotonicNowMs();
        refreshMemorySnapshotLocked(runtime);
        ++runtime->renderedFrames;
        runtime->latestFrameId = runtime->renderedFrames;
        runtime->runtimeThreadPresentMs += monotonicNowMs() - presentStartMs;
        runtime->runtimeThreadPresentCalls++;
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
#define SMOLNES_CPU_STEP_BEGIN smolnesRuntimeWrappedCpuStepBegin
#define SMOLNES_CPU_STEP_END smolnesRuntimeWrappedCpuStepEnd
#define SMOLNES_FRAME_EXEC_BEGIN smolnesRuntimeWrappedFrameExecutionBegin
#define SMOLNES_FRAME_EXEC_END smolnesRuntimeWrappedFrameExecutionEnd
#define SMOLNES_PPU_STEP_BEGIN smolnesRuntimeWrappedPpuStepBegin
#define SMOLNES_PPU_STEP_END smolnesRuntimeWrappedPpuStepEnd
#define SMOLNES_PPU_PHASE_SET smolnesRuntimeWrappedPpuPhaseSet
#define SMOLNES_PPU_PHASE_CLEAR smolnesRuntimeWrappedPpuPhaseClear
#define SMOLNES_FRAME_SUBMIT_BEGIN smolnesRuntimeWrappedFrameSubmitBegin
#define SMOLNES_FRAME_SUBMIT_END smolnesRuntimeWrappedFrameSubmitEnd
#define SMOLNES_EVENT_POLL_BEGIN smolnesRuntimeWrappedEventPollBegin
#define SMOLNES_EVENT_POLL_END smolnesRuntimeWrappedEventPollEnd
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
#undef SMOLNES_CPU_STEP_BEGIN
#undef SMOLNES_CPU_STEP_END
#undef SMOLNES_FRAME_EXEC_BEGIN
#undef SMOLNES_FRAME_EXEC_END
#undef SMOLNES_PPU_STEP_BEGIN
#undef SMOLNES_PPU_STEP_END
#undef SMOLNES_PPU_PHASE_SET
#undef SMOLNES_PPU_PHASE_CLEAR
#undef SMOLNES_FRAME_SUBMIT_BEGIN
#undef SMOLNES_FRAME_SUBMIT_END
#undef SMOLNES_EVENT_POLL_BEGIN
#undef SMOLNES_EVENT_POLL_END
#undef SMOLNES_TLS
#undef main

static void refreshMemorySnapshotLocked(SmolnesRuntimeHandle* runtime)
{
    const double snapshotStartMs = monotonicNowMs();
    memcpy(runtime->cpuRamSnapshot, ram, SMOLNES_RUNTIME_CPU_RAM_BYTES);
    memcpy(runtime->prgRamSnapshot, prgram, SMOLNES_RUNTIME_PRG_RAM_BYTES);
    runtime->memorySnapshotCopyMs += monotonicNowMs() - snapshotStartMs;
    runtime->memorySnapshotCopyCalls++;
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
    runtime->hasLatestPaletteFrame = false;
    runtime->hasMemorySnapshot = false;
    runtime->runFramesWaitMs = 0.0;
    runtime->runFramesWaitCalls = 0;
    runtime->runtimeThreadIdleWaitMs = 0.0;
    runtime->runtimeThreadIdleWaitCalls = 0;
    runtime->runtimeThreadCpuStepMs = 0.0;
    runtime->runtimeThreadCpuStepCalls = 0;
    runtime->runtimeThreadFrameExecutionMs = 0.0;
    runtime->runtimeThreadFrameExecutionCalls = 0;
    runtime->runtimeThreadPpuStepMs = 0.0;
    runtime->runtimeThreadPpuStepCalls = 0;
    runtime->runtimeThreadPpuVisiblePixelsMs = 0.0;
    runtime->runtimeThreadPpuVisiblePixelsCalls = 0;
    runtime->runtimeThreadPpuSpriteEvalMs = 0.0;
    runtime->runtimeThreadPpuSpriteEvalCalls = 0;
    runtime->runtimeThreadPpuPrefetchMs = 0.0;
    runtime->runtimeThreadPpuPrefetchCalls = 0;
    runtime->runtimeThreadPpuOtherMs = 0.0;
    runtime->runtimeThreadPpuOtherCalls = 0;
    runtime->runtimeThreadFrameSubmitMs = 0.0;
    runtime->runtimeThreadFrameSubmitCalls = 0;
    runtime->runtimeThreadEventPollMs = 0.0;
    runtime->runtimeThreadEventPollCalls = 0;
    runtime->runtimeThreadPresentMs = 0.0;
    runtime->runtimeThreadPresentCalls = 0;
    runtime->memorySnapshotCopyMs = 0.0;
    runtime->memorySnapshotCopyCalls = 0;
    memset(runtime->latestFrame, 0, sizeof(runtime->latestFrame));
    memset(runtime->latestPaletteFrame, 0, sizeof(runtime->latestPaletteFrame));
    memset(runtime->cpuRamSnapshot, 0, sizeof(runtime->cpuRamSnapshot));
    memset(runtime->prgRamSnapshot, 0, sizeof(runtime->prgRamSnapshot));
    runtime->controller1State = 0;
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
        const double waitStartMs = monotonicNowMs();
        int waitResult = 0;
        if (timeoutMs == 0) {
            waitResult = pthread_cond_wait(&runtime->runtimeCond, &runtime->runtimeMutex);
        }
        else {
            waitResult = pthread_cond_timedwait(&runtime->runtimeCond, &runtime->runtimeMutex, &deadline);
        }
        runtime->runFramesWaitMs += monotonicNowMs() - waitStartMs;
        runtime->runFramesWaitCalls++;

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

bool smolnesRuntimeCopyLatestPaletteIndices(
    const SmolnesRuntimeHandle* runtime, uint8_t* buffer, uint32_t bufferSize, uint64_t* frameId)
{
    if (runtime == NULL || buffer == NULL || bufferSize < SMOLNES_RUNTIME_PALETTE_FRAME_BYTES) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    if (!mutableRuntime->hasLatestPaletteFrame) {
        pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
        return false;
    }

    memcpy(buffer, mutableRuntime->latestPaletteFrame, SMOLNES_RUNTIME_PALETTE_FRAME_BYTES);
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

bool smolnesRuntimeCopyProfilingSnapshot(
    const SmolnesRuntimeHandle* runtime, SmolnesRuntimeProfilingSnapshot* snapshotOut)
{
    if (runtime == NULL || snapshotOut == NULL) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    snapshotOut->run_frames_wait_ms = mutableRuntime->runFramesWaitMs;
    snapshotOut->run_frames_wait_calls = mutableRuntime->runFramesWaitCalls;
    snapshotOut->runtime_thread_idle_wait_ms = mutableRuntime->runtimeThreadIdleWaitMs;
    snapshotOut->runtime_thread_idle_wait_calls = mutableRuntime->runtimeThreadIdleWaitCalls;
    snapshotOut->runtime_thread_cpu_step_ms = mutableRuntime->runtimeThreadCpuStepMs;
    snapshotOut->runtime_thread_cpu_step_calls = mutableRuntime->runtimeThreadCpuStepCalls;
    snapshotOut->runtime_thread_frame_execution_ms = mutableRuntime->runtimeThreadFrameExecutionMs;
    snapshotOut->runtime_thread_frame_execution_calls = mutableRuntime->runtimeThreadFrameExecutionCalls;
    snapshotOut->runtime_thread_ppu_step_ms = mutableRuntime->runtimeThreadPpuStepMs;
    snapshotOut->runtime_thread_ppu_step_calls = mutableRuntime->runtimeThreadPpuStepCalls;
    snapshotOut->runtime_thread_ppu_visible_pixels_ms = mutableRuntime->runtimeThreadPpuVisiblePixelsMs;
    snapshotOut->runtime_thread_ppu_visible_pixels_calls = mutableRuntime->runtimeThreadPpuVisiblePixelsCalls;
    snapshotOut->runtime_thread_ppu_sprite_eval_ms = mutableRuntime->runtimeThreadPpuSpriteEvalMs;
    snapshotOut->runtime_thread_ppu_sprite_eval_calls = mutableRuntime->runtimeThreadPpuSpriteEvalCalls;
    snapshotOut->runtime_thread_ppu_prefetch_ms = mutableRuntime->runtimeThreadPpuPrefetchMs;
    snapshotOut->runtime_thread_ppu_prefetch_calls = mutableRuntime->runtimeThreadPpuPrefetchCalls;
    snapshotOut->runtime_thread_ppu_other_ms = mutableRuntime->runtimeThreadPpuOtherMs;
    snapshotOut->runtime_thread_ppu_other_calls = mutableRuntime->runtimeThreadPpuOtherCalls;
    snapshotOut->runtime_thread_frame_submit_ms = mutableRuntime->runtimeThreadFrameSubmitMs;
    snapshotOut->runtime_thread_frame_submit_calls = mutableRuntime->runtimeThreadFrameSubmitCalls;
    snapshotOut->runtime_thread_event_poll_ms = mutableRuntime->runtimeThreadEventPollMs;
    snapshotOut->runtime_thread_event_poll_calls = mutableRuntime->runtimeThreadEventPollCalls;
    snapshotOut->runtime_thread_present_ms = mutableRuntime->runtimeThreadPresentMs;
    snapshotOut->runtime_thread_present_calls = mutableRuntime->runtimeThreadPresentCalls;
    snapshotOut->memory_snapshot_copy_ms = mutableRuntime->memorySnapshotCopyMs;
    snapshotOut->memory_snapshot_copy_calls = mutableRuntime->memorySnapshotCopyCalls;
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
