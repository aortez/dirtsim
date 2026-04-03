#include "SmolnesRuntimeBackend.h"

#include "SmolnesApu.h"
#include <SDL2/SDL.h>
#include <errno.h>
#include <math.h>
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

#define SMOLNES_APU_SAMPLE_COPY_MAX 1024u

extern SMOLNES_THREAD_LOCAL uint8_t frame_buffer_palette[61440];

static SMOLNES_THREAD_LOCAL SmolnesApuState gApuState;

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
    bool waitingForInitialFrameRequest;

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
    double runtimeThreadApuStepMs;
    uint64_t runtimeThreadApuStepCalls;
    double runtimeThreadPpuStepMs;
    uint64_t runtimeThreadPpuStepCalls;
    double runtimeThreadPpuVisiblePixelsMs;
    uint64_t runtimeThreadPpuVisiblePixelsCalls;
    uint64_t runtimeThreadPpuVisibleBgOnlySpanCalls;
    uint64_t runtimeThreadPpuVisibleBgOnlySpanPixels;
    uint64_t runtimeThreadPpuVisibleBgOnlyScalarPixels;
    uint64_t runtimeThreadPpuVisibleBgOnlyBatchedPixels;
    uint64_t runtimeThreadPpuVisibleBgOnlyBatchedCalls;
    double runtimeThreadPpuSpriteEvalMs;
    uint64_t runtimeThreadPpuSpriteEvalCalls;
    double runtimeThreadPpuPostVisibleMs;
    uint64_t runtimeThreadPpuPostVisibleCalls;
    double runtimeThreadPpuPrefetchMs;
    uint64_t runtimeThreadPpuPrefetchCalls;
    double runtimeThreadPpuNonVisibleScanlinesMs;
    uint64_t runtimeThreadPpuNonVisibleScanlinesCalls;
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

    uint8_t latchedController1State;
    uint8_t pendingController1State;
    uint64_t pendingController1ObservedTimestampNs;
    uint64_t pendingController1RequestTimestampNs;
    uint64_t pendingController1SequenceId;
    uint64_t latchedController1ObservedTimestampNs;
    uint64_t latchedController1LatchTimestampNs;
    uint64_t latchedController1AppliedFrameId;
    uint64_t latchedController1RequestTimestampNs;
    uint64_t latchedController1SequenceId;
    uint64_t latestFrameController1AppliedFrameId;
    uint64_t latestFrameController1ObservedTimestampNs;
    uint64_t latestFrameController1LatchTimestampNs;
    uint64_t latestFrameController1RequestTimestampNs;
    uint64_t latestFrameController1SequenceId;
    uint8_t latestFrameController1State;
    uint64_t nextController1SequenceId;
    uint8_t cpuRamSnapshot[SMOLNES_RUNTIME_CPU_RAM_BYTES];
    uint8_t latestFrame[SMOLNES_RUNTIME_FRAME_BYTES];
    uint8_t latestPaletteFrame[SMOLNES_RUNTIME_PALETTE_FRAME_BYTES];
    uint8_t prgRamSnapshot[SMOLNES_RUNTIME_PRG_RAM_BYTES];
    uint8_t rendererStub;
    uint8_t textureStub;
    uint8_t windowStub;

    SmolnesApuSnapshot apuSnapshot;
    bool hasApuSnapshot;
    float apuSampleBuffer[SMOLNES_APU_SAMPLE_COPY_MAX];
    uint32_t apuSampleBufferCount;
    uint64_t apuSampleBufferLastIndex;

    char lastError[256];
    char romPath[1024];

    SmolnesApuSampleCallback apuSampleCallback;
    void* apuSampleCallbackUserdata;

    bool apuEnabled;
    bool pixelOutputEnabled;
    bool rgbaOutputEnabled;
    bool detailedTimingEnabled;
    uint32_t timingSampleRate;
    SmolnesRuntimePacingModeValue pacingMode;
    double realtimePacingOriginMs;
    uint64_t realtimePacingOriginFrame;
};

// NTSC NES frame period: CPU clock 1789773 Hz / 29780.5 cycles per frame ≈ 60.0988 fps.
static const double kNtscFramePeriodMs = 1000.0 / 60.0988;

static SMOLNES_THREAD_LOCAL SmolnesRuntimeHandle* gCurrentRuntime = NULL;
static const uint8_t gEmptyKeyboardState[SDL_NUM_SCANCODES] = { 0 };
static SMOLNES_THREAD_LOCAL uint8_t gThreadKeyboardState[SDL_NUM_SCANCODES] = { 0 };
// Per-instruction CPU/APU/PPU timing. When detailedTimingEnabled is false,
// these are no-ops. When enabled, only every Nth instruction is timed
// (controlled by timingSampleRate) and results are scaled at flush time.
// Frame-level timing (FRAME_EXEC) is always active.
static SMOLNES_THREAD_LOCAL bool gApuEnabled = true;
static SMOLNES_THREAD_LOCAL bool gPixelOutputEnabled = true;
static SMOLNES_THREAD_LOCAL bool gRgbaOutputEnabled = true;
static SMOLNES_THREAD_LOCAL bool gDetailedTimingEnabled = true;
static SMOLNES_THREAD_LOCAL uint32_t gTimingSampleRate = 64;
static SMOLNES_THREAD_LOCAL uint32_t gTimingSampleCounter = 0;
static SMOLNES_THREAD_LOCAL bool gTimingThisInstruction = false;
static SMOLNES_THREAD_LOCAL uint64_t gTotalInstructions = 0;
static SMOLNES_THREAD_LOCAL uint64_t gSampledInstructions = 0;
static SMOLNES_THREAD_LOCAL bool gApuStepActive = false;
static SMOLNES_THREAD_LOCAL double gApuStepStartMs = 0.0;
static SMOLNES_THREAD_LOCAL double gApuStepAccumMs = 0.0;
static SMOLNES_THREAD_LOCAL uint64_t gApuStepAccumCalls = 0;
static SMOLNES_THREAD_LOCAL bool gCpuStepActive = false;
static SMOLNES_THREAD_LOCAL double gCpuStepStartMs = 0.0;
static SMOLNES_THREAD_LOCAL double gCpuStepAccumMs = 0.0;
static SMOLNES_THREAD_LOCAL uint64_t gCpuStepAccumCalls = 0;
static SMOLNES_THREAD_LOCAL bool gEventPollActive = false;
static SMOLNES_THREAD_LOCAL double gEventPollStartMs = 0.0;
static SMOLNES_THREAD_LOCAL bool gFrameExecutionActive = false;
static SMOLNES_THREAD_LOCAL double gFrameExecutionStartMs = 0.0;
static SMOLNES_THREAD_LOCAL bool gPpuStepActive = false;
static SMOLNES_THREAD_LOCAL double gPpuStepStartMs = 0.0;
static SMOLNES_THREAD_LOCAL double gPpuStepAccumMs = 0.0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuStepAccumCalls = 0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuVisibleBgOnlySpanCalls = 0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuVisibleBgOnlySpanPixels = 0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuVisibleBgOnlyScalarPixels = 0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuVisibleBgOnlyBatchedPixels = 0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuVisibleBgOnlyBatchedCalls = 0;
typedef enum SmolnesPpuPhaseBucket {
    SmolnesPpuPhaseBucketNone = 0,
    SmolnesPpuPhaseBucketVisiblePixels = 1,
    SmolnesPpuPhaseBucketPrefetch = 2,
    SmolnesPpuPhaseBucketOther = 3,
    SmolnesPpuPhaseBucketSpriteEval = 4,
    SmolnesPpuPhaseBucketPostVisible = 5,
    SmolnesPpuPhaseBucketNonVisibleScanlines = 6
} SmolnesPpuPhaseBucket;
static SMOLNES_THREAD_LOCAL SmolnesPpuPhaseBucket gPpuPhaseBucket = SmolnesPpuPhaseBucketNone;
static SMOLNES_THREAD_LOCAL double gPpuPhaseBucketStartMs = 0.0;
static SMOLNES_THREAD_LOCAL double gPpuVisiblePixelsAccumMs = 0.0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuVisiblePixelsAccumCalls = 0;
static SMOLNES_THREAD_LOCAL double gPpuSpriteEvalAccumMs = 0.0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuSpriteEvalAccumCalls = 0;
static SMOLNES_THREAD_LOCAL double gPpuPostVisibleAccumMs = 0.0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuPostVisibleAccumCalls = 0;
static SMOLNES_THREAD_LOCAL double gPpuPrefetchAccumMs = 0.0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuPrefetchAccumCalls = 0;
static SMOLNES_THREAD_LOCAL double gPpuNonVisibleScanlinesAccumMs = 0.0;
static SMOLNES_THREAD_LOCAL uint64_t gPpuNonVisibleScanlinesAccumCalls = 0;
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

static void recordIdleWaitLocked(SmolnesRuntimeHandle* runtime, double waitMs)
{
    runtime->runtimeThreadIdleWaitMs += waitMs;
    runtime->runtimeThreadIdleWaitCalls++;
}

static bool isRealtimePacingMode(const SmolnesRuntimeHandle* runtime)
{
    return runtime != NULL && runtime->pacingMode == SMOLNES_RUNTIME_PACING_MODE_REALTIME;
}

static uint8_t latchThreadKeyboardStateFromRuntime(SmolnesRuntimeHandle* runtime)
{
    if (runtime == NULL) {
        memset(gThreadKeyboardState, 0, SDL_NUM_SCANCODES * sizeof(uint8_t));
        return 0;
    }

    const uint8_t controller1State = runtime->latchedController1State;
    mapController1StateToKeyboard(controller1State, gThreadKeyboardState);
    return controller1State;
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

static uint64_t monotonicNowNs(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return ((uint64_t)now.tv_sec * 1000000000ull) + (uint64_t)now.tv_nsec;
}

static void resetPpuPhaseBreakdown(void)
{
    gPpuPhaseBucket = SmolnesPpuPhaseBucketNone;
    gPpuPhaseBucketStartMs = 0.0;
    gPpuVisiblePixelsAccumMs = 0.0;
    gPpuVisiblePixelsAccumCalls = 0;
    gPpuSpriteEvalAccumMs = 0.0;
    gPpuSpriteEvalAccumCalls = 0;
    gPpuPostVisibleAccumMs = 0.0;
    gPpuPostVisibleAccumCalls = 0;
    gPpuPrefetchAccumMs = 0.0;
    gPpuPrefetchAccumCalls = 0;
    gPpuNonVisibleScanlinesAccumMs = 0.0;
    gPpuNonVisibleScanlinesAccumCalls = 0;
    gPpuOtherAccumMs = 0.0;
    gPpuOtherAccumCalls = 0;
}

static void resetPerInstructionAccumulators(void)
{
    gTimingSampleCounter = 0;
    gTimingThisInstruction = false;
    gTotalInstructions = 0;
    gSampledInstructions = 0;
    gCpuStepAccumMs = 0.0;
    gCpuStepAccumCalls = 0;
    gApuStepAccumMs = 0.0;
    gApuStepAccumCalls = 0;
    gPpuStepAccumMs = 0.0;
    gPpuStepAccumCalls = 0;
    gPpuVisibleBgOnlySpanCalls = 0;
    gPpuVisibleBgOnlySpanPixels = 0;
    gPpuVisibleBgOnlyScalarPixels = 0;
    gPpuVisibleBgOnlyBatchedPixels = 0;
    gPpuVisibleBgOnlyBatchedCalls = 0;
    resetPpuPhaseBreakdown();
}

static void flushPerInstructionAccumulatorsLocked(SmolnesRuntimeHandle* runtime)
{
    runtime->runtimeThreadCpuStepMs += gCpuStepAccumMs;
    runtime->runtimeThreadCpuStepCalls += gSampledInstructions;
    runtime->runtimeThreadApuStepMs += gApuStepAccumMs;
    runtime->runtimeThreadApuStepCalls += gSampledInstructions;
    runtime->runtimeThreadPpuStepMs += gPpuStepAccumMs;
    runtime->runtimeThreadPpuStepCalls += gSampledInstructions;
    runtime->runtimeThreadPpuVisiblePixelsMs += gPpuVisiblePixelsAccumMs;
    runtime->runtimeThreadPpuVisiblePixelsCalls += gPpuVisiblePixelsAccumCalls;
    runtime->runtimeThreadPpuVisibleBgOnlySpanCalls += gPpuVisibleBgOnlySpanCalls;
    runtime->runtimeThreadPpuVisibleBgOnlySpanPixels += gPpuVisibleBgOnlySpanPixels;
    runtime->runtimeThreadPpuVisibleBgOnlyScalarPixels += gPpuVisibleBgOnlyScalarPixels;
    runtime->runtimeThreadPpuVisibleBgOnlyBatchedPixels += gPpuVisibleBgOnlyBatchedPixels;
    runtime->runtimeThreadPpuVisibleBgOnlyBatchedCalls += gPpuVisibleBgOnlyBatchedCalls;
    runtime->runtimeThreadPpuSpriteEvalMs += gPpuSpriteEvalAccumMs;
    runtime->runtimeThreadPpuSpriteEvalCalls += gPpuSpriteEvalAccumCalls;
    runtime->runtimeThreadPpuPostVisibleMs += gPpuPostVisibleAccumMs;
    runtime->runtimeThreadPpuPostVisibleCalls += gPpuPostVisibleAccumCalls;
    runtime->runtimeThreadPpuPrefetchMs += gPpuPrefetchAccumMs;
    runtime->runtimeThreadPpuPrefetchCalls += gPpuPrefetchAccumCalls;
    runtime->runtimeThreadPpuNonVisibleScanlinesMs += gPpuNonVisibleScanlinesAccumMs;
    runtime->runtimeThreadPpuNonVisibleScanlinesCalls += gPpuNonVisibleScanlinesAccumCalls;
    runtime->runtimeThreadPpuOtherMs += gPpuOtherAccumMs;
    runtime->runtimeThreadPpuOtherCalls += gPpuOtherAccumCalls;
    resetPerInstructionAccumulators();
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
        case SmolnesPpuPhaseBucketPostVisible:
            gPpuPostVisibleAccumMs += durationMs;
            gPpuPostVisibleAccumCalls++;
            break;
        case SmolnesPpuPhaseBucketPrefetch:
            gPpuPrefetchAccumMs += durationMs;
            gPpuPrefetchAccumCalls++;
            break;
        case SmolnesPpuPhaseBucketNonVisibleScanlines:
            gPpuNonVisibleScanlinesAccumMs += durationMs;
            gPpuNonVisibleScanlinesAccumCalls++;
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
    gApuEnabled = runtime->apuEnabled;
    gPixelOutputEnabled = runtime->pixelOutputEnabled;
    gRgbaOutputEnabled = runtime->rgbaOutputEnabled;
    gDetailedTimingEnabled = runtime->detailedTimingEnabled;
    gTimingSampleRate = runtime->timingSampleRate > 0 ? runtime->timingSampleRate : 1;
    gApuStepActive = false;
    gApuStepStartMs = 0.0;
    gCpuStepActive = false;
    gCpuStepStartMs = 0.0;
    gEventPollActive = false;
    gEventPollStartMs = 0.0;
    gFrameExecutionActive = false;
    gFrameExecutionStartMs = 0.0;
    gPpuStepActive = false;
    gPpuStepStartMs = 0.0;
    resetPerInstructionAccumulators();
    gFrameSubmitActive = false;
    gFrameSubmitStartMs = 0.0;
    smolnesApuInit(&gApuState, 48000.0);
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
    gApuStepActive = false;
    gApuStepStartMs = 0.0;
    gCpuStepActive = false;
    gCpuStepStartMs = 0.0;
    gEventPollActive = false;
    gEventPollStartMs = 0.0;
    gFrameExecutionActive = false;
    gFrameExecutionStartMs = 0.0;
    gPpuStepActive = false;
    gPpuStepStartMs = 0.0;
    resetPerInstructionAccumulators();
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
    const uint8_t controller1State = runtime->latchedController1State;
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
    if (gRgbaOutputEnabled) {
        for (uint32_t row = 0; row < SMOLNES_RUNTIME_FRAME_HEIGHT; ++row) {
            const uint8_t* src = (const uint8_t*)pixels + ((size_t)row * (size_t)pitch);
            uint8_t* dst = runtime->latestFrame + (row * SMOLNES_RUNTIME_FRAME_PITCH_BYTES);
            memcpy(dst, src, SMOLNES_RUNTIME_FRAME_PITCH_BYTES);
        }
    }
    runtime->hasLatestFrame = true;
    const uint8_t* paletteSrc = frame_buffer_palette + (SMOLNES_RUNTIME_FRAME_WIDTH * 8u);
    for (uint32_t row = 0; row < SMOLNES_RUNTIME_FRAME_HEIGHT; ++row) {
        const uint8_t* src = paletteSrc + (row * SMOLNES_RUNTIME_FRAME_WIDTH);
        uint8_t* dst = runtime->latestPaletteFrame + (row * SMOLNES_RUNTIME_FRAME_WIDTH);
        memcpy(dst, src, SMOLNES_RUNTIME_FRAME_WIDTH);
    }
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

// CPU_STEP_BEGIN decides whether this instruction triplet (CPU/APU/PPU) is
// sampled. APU and PPU begin/end follow that decision via gTimingThisInstruction.
void smolnesRuntimeWrappedCpuStepBegin(void)
{
    gTotalInstructions++;
    if (!gDetailedTimingEnabled) {
        return;
    }

    if (++gTimingSampleCounter < gTimingSampleRate) {
        return;
    }
    gTimingSampleCounter = 0;
    gTimingThisInstruction = true;
    gSampledInstructions++;

    gCpuStepStartMs = monotonicNowMs();
    gCpuStepActive = true;
}

void smolnesRuntimeWrappedCpuStepEnd(void)
{
    if (!gCpuStepActive) {
        return;
    }

    const double cpuStepMs = monotonicNowMs() - gCpuStepStartMs;
    gCpuStepActive = false;
    if (cpuStepMs > 0.0) {
        gCpuStepAccumMs += cpuStepMs;
    }
}

void smolnesRuntimeWrappedApuClockBegin(void)
{
    if (!gTimingThisInstruction || !gApuEnabled) {
        return;
    }

    gApuStepStartMs = monotonicNowMs();
    gApuStepActive = true;
}

void smolnesRuntimeWrappedApuClockEnd(void)
{
    if (!gApuStepActive) {
        return;
    }

    const double apuStepMs = monotonicNowMs() - gApuStepStartMs;
    gApuStepActive = false;
    if (apuStepMs > 0.0) {
        gApuStepAccumMs += apuStepMs;
    }
}

void smolnesRuntimeWrappedFrameExecutionBegin(void)
{
    SmolnesRuntimeHandle* runtime = getCurrentRuntime();
    if (runtime == NULL) {
        return;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    while (runtime->waitingForInitialFrameRequest && !runtime->stopRequested
           && !isRealtimePacingMode(runtime) && runtime->renderedFrames >= runtime->targetFrames) {
        const double waitStartMs = monotonicNowMs();
        pthread_cond_wait(&runtime->runtimeCond, &runtime->runtimeMutex);
        recordIdleWaitLocked(runtime, monotonicNowMs() - waitStartMs);
    }
    runtime->waitingForInitialFrameRequest = false;
    if (runtime->pendingController1SequenceId != runtime->latchedController1SequenceId) {
        runtime->latchedController1State = runtime->pendingController1State;
        runtime->latchedController1ObservedTimestampNs =
            runtime->pendingController1ObservedTimestampNs;
        runtime->latchedController1RequestTimestampNs = runtime->pendingController1RequestTimestampNs;
        runtime->latchedController1SequenceId = runtime->pendingController1SequenceId;
        runtime->latchedController1AppliedFrameId =
            (runtime->latchedController1SequenceId == 0) ? 0 : (runtime->renderedFrames + 1);
        runtime->latchedController1LatchTimestampNs =
            (runtime->latchedController1SequenceId == 0) ? 0 : monotonicNowNs();
    }
    latchThreadKeyboardStateFromRuntime(runtime);
    gApuEnabled = runtime->apuEnabled;
    gPixelOutputEnabled = runtime->pixelOutputEnabled;
    gRgbaOutputEnabled = runtime->rgbaOutputEnabled;
    gDetailedTimingEnabled = runtime->detailedTimingEnabled;
    gTimingSampleRate = runtime->timingSampleRate > 0 ? runtime->timingSampleRate : 1;
    pthread_mutex_unlock(&runtime->runtimeMutex);
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
    flushPerInstructionAccumulatorsLocked(runtime);
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

void smolnesRuntimeWrappedPpuStepBegin(void)
{
    if (!gTimingThisInstruction) {
        return;
    }

    gPpuStepStartMs = monotonicNowMs();
    gPpuStepActive = true;
}

void smolnesRuntimeWrappedPpuStepEnd(void)
{
    if (!gPpuStepActive) {
        gTimingThisInstruction = false;
        return;
    }

    setPpuPhaseBucket(SmolnesPpuPhaseBucketNone);
    const double ppuStepMs = monotonicNowMs() - gPpuStepStartMs;
    gPpuStepActive = false;
    gTimingThisInstruction = false;
    if (ppuStepMs > 0.0) {
        gPpuStepAccumMs += ppuStepMs;
    }
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
        case 5u:
            nextPhase = SmolnesPpuPhaseBucketPostVisible;
            break;
        case 6u:
            nextPhase = SmolnesPpuPhaseBucketNonVisibleScanlines;
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

void smolnesRuntimeWrappedPpuVisibleBgOnlyStats(
    uint16_t spanPixels,
    uint16_t scalarPixels,
    uint16_t batchedPixels,
    uint16_t batchedCalls)
{
    if (!gDetailedTimingEnabled) {
        return;
    }

    gPpuVisibleBgOnlySpanCalls++;
    gPpuVisibleBgOnlySpanPixels += spanPixels;
    gPpuVisibleBgOnlyScalarPixels += scalarPixels;
    gPpuVisibleBgOnlyBatchedPixels += batchedPixels;
    gPpuVisibleBgOnlyBatchedCalls += batchedCalls;
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

    if (isRealtimePacingMode(runtime)) {
        double sleepMs = 0.0;
        if (!runtime->stopRequested) {
            gApuState.sampleCallback = runtime->apuSampleCallback;
            gApuState.sampleCallbackUserdata = runtime->apuSampleCallbackUserdata;
            const double presentStartMs = monotonicNowMs();
            refreshMemorySnapshotLocked(runtime);
            ++runtime->renderedFrames;
            runtime->latestFrameId = runtime->renderedFrames;
            runtime->latestFrameController1AppliedFrameId =
                runtime->latchedController1AppliedFrameId;
            runtime->latestFrameController1ObservedTimestampNs =
                runtime->latchedController1ObservedTimestampNs;
            runtime->latestFrameController1LatchTimestampNs =
                runtime->latchedController1LatchTimestampNs;
            runtime->latestFrameController1RequestTimestampNs =
                runtime->latchedController1RequestTimestampNs;
            runtime->latestFrameController1SequenceId = runtime->latchedController1SequenceId;
            runtime->latestFrameController1State = runtime->latchedController1State;
            if (runtime->targetFrames < runtime->renderedFrames) {
                runtime->targetFrames = runtime->renderedFrames;
            }
            runtime->runtimeThreadPresentMs += monotonicNowMs() - presentStartMs;
            runtime->runtimeThreadPresentCalls++;
            pthread_cond_broadcast(&runtime->runtimeCond);

            if (runtime->realtimePacingOriginMs == 0.0) {
                runtime->realtimePacingOriginMs = presentStartMs;
                runtime->realtimePacingOriginFrame = runtime->renderedFrames;
            }
            const double elapsed =
                (double)(runtime->renderedFrames - runtime->realtimePacingOriginFrame);
            const double nextFrameMs =
                runtime->realtimePacingOriginMs + elapsed * kNtscFramePeriodMs;
            sleepMs = nextFrameMs - monotonicNowMs();
        }
        pthread_mutex_unlock(&runtime->runtimeMutex);

        if (sleepMs > 0.5) {
            struct timespec ts;
            ts.tv_sec = (time_t)(sleepMs / 1000.0);
            ts.tv_nsec = (long)(fmod(sleepMs, 1000.0) * 1000000.0);
            clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
        }
    }
    else {
        if (!runtime->stopRequested && !isRealtimePacingMode(runtime)) {
            gApuState.sampleCallback = runtime->apuSampleCallback;
            gApuState.sampleCallbackUserdata = runtime->apuSampleCallbackUserdata;
            const double presentStartMs = monotonicNowMs();
            refreshMemorySnapshotLocked(runtime);
            ++runtime->renderedFrames;
            runtime->latestFrameId = runtime->renderedFrames;
            runtime->latestFrameController1AppliedFrameId =
                runtime->latchedController1AppliedFrameId;
            runtime->latestFrameController1ObservedTimestampNs =
                runtime->latchedController1ObservedTimestampNs;
            runtime->latestFrameController1LatchTimestampNs =
                runtime->latchedController1LatchTimestampNs;
            runtime->latestFrameController1RequestTimestampNs =
                runtime->latchedController1RequestTimestampNs;
            runtime->latestFrameController1SequenceId = runtime->latchedController1SequenceId;
            runtime->latestFrameController1State = runtime->latchedController1State;
            runtime->runtimeThreadPresentMs += monotonicNowMs() - presentStartMs;
            runtime->runtimeThreadPresentCalls++;
            pthread_cond_broadcast(&runtime->runtimeCond);
            while (!runtime->stopRequested && !isRealtimePacingMode(runtime)
                   && runtime->renderedFrames >= runtime->targetFrames) {
                const double waitStartMs = monotonicNowMs();
                pthread_cond_wait(&runtime->runtimeCond, &runtime->runtimeMutex);
                recordIdleWaitLocked(runtime, monotonicNowMs() - waitStartMs);
            }
        }
        pthread_mutex_unlock(&runtime->runtimeMutex);
    }
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
#define SMOLNES_PPU_PHASE_SET_IF_ACTIVE(phase)                                              \
    do {                                                                                     \
        if (gPpuStepActive) {                                                                \
            smolnesRuntimeWrappedPpuPhaseSet(phase);                                         \
        }                                                                                    \
    } while (0)
#define SMOLNES_PPU_VISIBLE_BG_ONLY_STATS(span_pixels, scalar_pixels, batched_pixels, batch_count) \
    smolnesRuntimeWrappedPpuVisibleBgOnlyStats(                                                  \
        (span_pixels), (scalar_pixels), (batched_pixels), (batch_count))
#define SMOLNES_FRAME_SUBMIT_BEGIN smolnesRuntimeWrappedFrameSubmitBegin
#define SMOLNES_FRAME_SUBMIT_END smolnesRuntimeWrappedFrameSubmitEnd
#define SMOLNES_EVENT_POLL_BEGIN smolnesRuntimeWrappedEventPollBegin
#define SMOLNES_EVENT_POLL_END smolnesRuntimeWrappedEventPollEnd
static void smolnesRuntimeWrappedApuWrite(uint16_t addr, uint8_t value)
{
    smolnesApuWrite(&gApuState, addr, value);
}

static uint8_t smolnesRuntimeWrappedApuRead(uint16_t addr)
{
    return smolnesApuRead(&gApuState, addr);
}

static void smolnesRuntimeWrappedApuClock(uint32_t cycles)
{
    if (!gApuEnabled) {
        return;
    }
    smolnesApuClock(&gApuState, cycles);
}

#define SMOLNES_PIXEL_OUTPUT(offset, color, palette) \
    do { \
        if (gPixelOutputEnabled) { \
            uint8_t pi_ = palette_ram[(color) ? (palette) | (color) : 0]; \
            frame_buffer_palette[offset] = pi_; \
            if (gRgbaOutputEnabled) { \
                frame_buffer[offset] = nes_palette_rgb565[pi_]; \
            } \
        } \
    } while (0)
#define SMOLNES_APU_CLOCK_BEGIN smolnesRuntimeWrappedApuClockBegin
#define SMOLNES_APU_CLOCK_END smolnesRuntimeWrappedApuClockEnd
#define SMOLNES_APU_WRITE(addr, value) smolnesRuntimeWrappedApuWrite(addr, value)
#define SMOLNES_APU_READ(addr) smolnesRuntimeWrappedApuRead(addr)
#define SMOLNES_APU_CLOCK(cycles) smolnesRuntimeWrappedApuClock(cycles)
#define SMOLNES_PIXEL_OUTPUT_ENABLED gPixelOutputEnabled
#define SMOLNES_RGBA_OUTPUT_ENABLED gRgbaOutputEnabled
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
#undef SMOLNES_PPU_PHASE_SET_IF_ACTIVE
#undef SMOLNES_FRAME_SUBMIT_BEGIN
#undef SMOLNES_FRAME_SUBMIT_END
#undef SMOLNES_EVENT_POLL_BEGIN
#undef SMOLNES_EVENT_POLL_END
#undef SMOLNES_PIXEL_OUTPUT
#undef SMOLNES_APU_CLOCK_BEGIN
#undef SMOLNES_APU_CLOCK_END
#undef SMOLNES_APU_WRITE
#undef SMOLNES_APU_READ
#undef SMOLNES_APU_CLOCK
#undef SMOLNES_PIXEL_OUTPUT_ENABLED
#undef SMOLNES_RGBA_OUTPUT_ENABLED
#undef SMOLNES_TLS
#undef main

static void refreshMemorySnapshotLocked(SmolnesRuntimeHandle* runtime)
{
    const double snapshotStartMs = monotonicNowMs();
    memcpy(runtime->cpuRamSnapshot, ram, SMOLNES_RUNTIME_CPU_RAM_BYTES);
    memcpy(runtime->prgRamSnapshot, prgram, SMOLNES_RUNTIME_PRG_RAM_BYTES);

    // Copy APU snapshot and samples.
    smolnesApuGetSnapshot(&gApuState, &runtime->apuSnapshot);
    runtime->apuSampleBufferCount = smolnesApuCopySamples(
        &gApuState,
        runtime->apuSampleBuffer,
        SMOLNES_APU_SAMPLE_COPY_MAX,
        runtime->apuSampleBufferLastIndex);
    runtime->apuSampleBufferLastIndex = smolnesApuGetSampleCount(&gApuState);
    runtime->hasApuSnapshot = true;

    runtime->memorySnapshotCopyMs += monotonicNowMs() - snapshotStartMs;
    runtime->memorySnapshotCopyCalls++;
    runtime->hasMemorySnapshot = true;
}

SmolnesRuntimeHandle* smolnesRuntimeCreate(void)
{
    SmolnesRuntimeHandle* runtime = (SmolnesRuntimeHandle*)calloc(1u, sizeof(SmolnesRuntimeHandle));
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
    runtime->apuEnabled = true;
    runtime->pixelOutputEnabled = true;
    runtime->rgbaOutputEnabled = true;
    runtime->detailedTimingEnabled = false;
    runtime->timingSampleRate = 64;
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
    runtime->runtimeThreadApuStepMs = 0.0;
    runtime->runtimeThreadApuStepCalls = 0;
    runtime->runtimeThreadCpuStepMs = 0.0;
    runtime->runtimeThreadCpuStepCalls = 0;
    runtime->runtimeThreadFrameExecutionMs = 0.0;
    runtime->runtimeThreadFrameExecutionCalls = 0;
    runtime->runtimeThreadPpuStepMs = 0.0;
    runtime->runtimeThreadPpuStepCalls = 0;
    runtime->runtimeThreadPpuVisiblePixelsMs = 0.0;
    runtime->runtimeThreadPpuVisiblePixelsCalls = 0;
    runtime->runtimeThreadPpuVisibleBgOnlySpanCalls = 0;
    runtime->runtimeThreadPpuVisibleBgOnlySpanPixels = 0;
    runtime->runtimeThreadPpuVisibleBgOnlyScalarPixels = 0;
    runtime->runtimeThreadPpuVisibleBgOnlyBatchedPixels = 0;
    runtime->runtimeThreadPpuVisibleBgOnlyBatchedCalls = 0;
    runtime->runtimeThreadPpuSpriteEvalMs = 0.0;
    runtime->runtimeThreadPpuSpriteEvalCalls = 0;
    runtime->runtimeThreadPpuPostVisibleMs = 0.0;
    runtime->runtimeThreadPpuPostVisibleCalls = 0;
    runtime->runtimeThreadPpuPrefetchMs = 0.0;
    runtime->runtimeThreadPpuPrefetchCalls = 0;
    runtime->runtimeThreadPpuNonVisibleScanlinesMs = 0.0;
    runtime->runtimeThreadPpuNonVisibleScanlinesCalls = 0;
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
    memset(&runtime->apuSnapshot, 0, sizeof(runtime->apuSnapshot));
    runtime->hasApuSnapshot = false;
    memset(runtime->apuSampleBuffer, 0, sizeof(runtime->apuSampleBuffer));
    runtime->apuSampleBufferCount = 0;
    runtime->apuSampleBufferLastIndex = 0;
    runtime->apuSampleCallback = NULL;
    runtime->apuSampleCallbackUserdata = NULL;
    runtime->pacingMode = SMOLNES_RUNTIME_PACING_MODE_LOCKSTEP;
    runtime->realtimePacingOriginMs = 0.0;
    runtime->realtimePacingOriginFrame = 0;
    runtime->waitingForInitialFrameRequest = true;
    runtime->latchedController1State = 0;
    runtime->pendingController1State = 0;
    runtime->pendingController1ObservedTimestampNs = 0;
    runtime->pendingController1RequestTimestampNs = 0;
    runtime->pendingController1SequenceId = 0;
    runtime->latchedController1ObservedTimestampNs = 0;
    runtime->latchedController1LatchTimestampNs = 0;
    runtime->latchedController1AppliedFrameId = 0;
    runtime->latchedController1RequestTimestampNs = 0;
    runtime->latchedController1SequenceId = 0;
    runtime->latestFrameController1AppliedFrameId = 0;
    runtime->latestFrameController1ObservedTimestampNs = 0;
    runtime->latestFrameController1LatchTimestampNs = 0;
    runtime->latestFrameController1RequestTimestampNs = 0;
    runtime->latestFrameController1SequenceId = 0;
    runtime->latestFrameController1State = 0;
    runtime->nextController1SequenceId = 1;
    runtime->threadRunning = true;

    const int createResult =
        pthread_create(&runtime->runtimeThread, NULL, runtimeThreadMain, runtime);
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
    while (runtime->renderedFrames < requestedFrames && runtime->threadRunning
           && runtime->healthy) {
        const double waitStartMs = monotonicNowMs();
        int waitResult = 0;
        if (timeoutMs == 0) {
            waitResult = pthread_cond_wait(&runtime->runtimeCond, &runtime->runtimeMutex);
        }
        else {
            waitResult =
                pthread_cond_timedwait(&runtime->runtimeCond, &runtime->runtimeMutex, &deadline);
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
    smolnesRuntimeSetController1StateObserved(runtime, buttonMask, 0);
}

void smolnesRuntimeSetController1StateObserved(
    SmolnesRuntimeHandle* runtime, uint8_t buttonMask, uint64_t observedTimestampNs)
{
    if (runtime == NULL) {
        return;
    }

    pthread_mutex_lock(&runtime->runtimeMutex);
    if (runtime->pendingController1State != buttonMask) {
        runtime->pendingController1State = buttonMask;
        runtime->pendingController1ObservedTimestampNs = observedTimestampNs;
        runtime->pendingController1RequestTimestampNs = monotonicNowNs();
        runtime->pendingController1SequenceId = runtime->nextController1SequenceId++;
        if (runtime->nextController1SequenceId == 0) {
            runtime->nextController1SequenceId = 1;
        }
    }
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

bool smolnesRuntimeCopyCpuRam(
    const SmolnesRuntimeHandle* runtime, uint8_t* buffer, uint32_t bufferSize)
{
    if (runtime == NULL || buffer == NULL || bufferSize < SMOLNES_RUNTIME_CPU_RAM_BYTES) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    if (!mutableRuntime->threadRunning || !mutableRuntime->healthy
        || !mutableRuntime->hasMemorySnapshot) {
        pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
        return false;
    }

    memcpy(buffer, mutableRuntime->cpuRamSnapshot, SMOLNES_RUNTIME_CPU_RAM_BYTES);
    pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
    return true;
}

bool smolnesRuntimeCopyMemorySnapshot(
    const SmolnesRuntimeHandle* runtime,
    uint8_t* cpuRamBuffer,
    uint32_t cpuRamBufferSize,
    uint8_t* prgRamBuffer,
    uint32_t prgRamBufferSize,
    uint64_t* frameId)
{
    if (runtime == NULL || cpuRamBuffer == NULL || prgRamBuffer == NULL
        || cpuRamBufferSize < SMOLNES_RUNTIME_CPU_RAM_BYTES
        || prgRamBufferSize < SMOLNES_RUNTIME_PRG_RAM_BYTES) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    if (!mutableRuntime->threadRunning || !mutableRuntime->healthy
        || !mutableRuntime->hasMemorySnapshot) {
        pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
        return false;
    }

    memcpy(cpuRamBuffer, mutableRuntime->cpuRamSnapshot, SMOLNES_RUNTIME_CPU_RAM_BYTES);
    memcpy(prgRamBuffer, mutableRuntime->prgRamSnapshot, SMOLNES_RUNTIME_PRG_RAM_BYTES);
    if (frameId != NULL) {
        *frameId = mutableRuntime->latestFrameId;
    }
    pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
    return true;
}

bool smolnesRuntimeCopyPrgRam(
    const SmolnesRuntimeHandle* runtime, uint8_t* buffer, uint32_t bufferSize)
{
    if (runtime == NULL || buffer == NULL || bufferSize < SMOLNES_RUNTIME_PRG_RAM_BYTES) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    if (!mutableRuntime->threadRunning || !mutableRuntime->healthy
        || !mutableRuntime->hasMemorySnapshot) {
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
    snapshotOut->runtime_thread_apu_step_ms = mutableRuntime->runtimeThreadApuStepMs;
    snapshotOut->runtime_thread_apu_step_calls = mutableRuntime->runtimeThreadApuStepCalls;
    snapshotOut->runtime_thread_cpu_step_ms = mutableRuntime->runtimeThreadCpuStepMs;
    snapshotOut->runtime_thread_cpu_step_calls = mutableRuntime->runtimeThreadCpuStepCalls;
    snapshotOut->runtime_thread_frame_execution_ms = mutableRuntime->runtimeThreadFrameExecutionMs;
    snapshotOut->runtime_thread_frame_execution_calls =
        mutableRuntime->runtimeThreadFrameExecutionCalls;
    snapshotOut->runtime_thread_ppu_step_ms = mutableRuntime->runtimeThreadPpuStepMs;
    snapshotOut->runtime_thread_ppu_step_calls = mutableRuntime->runtimeThreadPpuStepCalls;
    snapshotOut->runtime_thread_ppu_visible_pixels_ms =
        mutableRuntime->runtimeThreadPpuVisiblePixelsMs;
    snapshotOut->runtime_thread_ppu_visible_pixels_calls =
        mutableRuntime->runtimeThreadPpuVisiblePixelsCalls;
    snapshotOut->runtime_thread_ppu_visible_bg_only_span_calls =
        mutableRuntime->runtimeThreadPpuVisibleBgOnlySpanCalls;
    snapshotOut->runtime_thread_ppu_visible_bg_only_span_pixels =
        mutableRuntime->runtimeThreadPpuVisibleBgOnlySpanPixels;
    snapshotOut->runtime_thread_ppu_visible_bg_only_scalar_pixels =
        mutableRuntime->runtimeThreadPpuVisibleBgOnlyScalarPixels;
    snapshotOut->runtime_thread_ppu_visible_bg_only_batched_pixels =
        mutableRuntime->runtimeThreadPpuVisibleBgOnlyBatchedPixels;
    snapshotOut->runtime_thread_ppu_visible_bg_only_batched_calls =
        mutableRuntime->runtimeThreadPpuVisibleBgOnlyBatchedCalls;
    snapshotOut->runtime_thread_ppu_sprite_eval_ms = mutableRuntime->runtimeThreadPpuSpriteEvalMs;
    snapshotOut->runtime_thread_ppu_sprite_eval_calls =
        mutableRuntime->runtimeThreadPpuSpriteEvalCalls;
    snapshotOut->runtime_thread_ppu_post_visible_ms = mutableRuntime->runtimeThreadPpuPostVisibleMs;
    snapshotOut->runtime_thread_ppu_post_visible_calls =
        mutableRuntime->runtimeThreadPpuPostVisibleCalls;
    snapshotOut->runtime_thread_ppu_prefetch_ms = mutableRuntime->runtimeThreadPpuPrefetchMs;
    snapshotOut->runtime_thread_ppu_prefetch_calls = mutableRuntime->runtimeThreadPpuPrefetchCalls;
    snapshotOut->runtime_thread_ppu_non_visible_scanlines_ms =
        mutableRuntime->runtimeThreadPpuNonVisibleScanlinesMs;
    snapshotOut->runtime_thread_ppu_non_visible_scanlines_calls =
        mutableRuntime->runtimeThreadPpuNonVisibleScanlinesCalls;
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

bool smolnesRuntimeCopyControllerSnapshot(
    const SmolnesRuntimeHandle* runtime, SmolnesRuntimeControllerSnapshot* snapshotOut)
{
    if (runtime == NULL || snapshotOut == NULL) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    if (!mutableRuntime->hasLatestFrame) {
        pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
        return false;
    }

    snapshotOut->latest_frame_id = mutableRuntime->latestFrameId;
    snapshotOut->controller1_applied_frame_id =
        mutableRuntime->latestFrameController1AppliedFrameId;
    snapshotOut->controller1_observed_timestamp_ns =
        mutableRuntime->latestFrameController1ObservedTimestampNs;
    snapshotOut->controller1_latch_timestamp_ns =
        mutableRuntime->latestFrameController1LatchTimestampNs;
    snapshotOut->controller1_request_timestamp_ns =
        mutableRuntime->latestFrameController1RequestTimestampNs;
    snapshotOut->controller1_sequence_id = mutableRuntime->latestFrameController1SequenceId;
    snapshotOut->controller1_state = mutableRuntime->latestFrameController1State;
    pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
    return true;
}

bool smolnesRuntimeCopyLiveSnapshot(
    const SmolnesRuntimeHandle* runtime,
    uint8_t* frameBuffer,
    uint32_t frameBufferSize,
    uint8_t* paletteBuffer,
    uint32_t paletteBufferSize,
    uint8_t* cpuRamBuffer,
    uint32_t cpuRamBufferSize,
    uint8_t* prgRamBuffer,
    uint32_t prgRamBufferSize,
    uint64_t* frameId,
    SmolnesRuntimeControllerSnapshot* controllerSnapshotOut)
{
    if (runtime == NULL || frameBuffer == NULL || paletteBuffer == NULL || cpuRamBuffer == NULL
        || prgRamBuffer == NULL || controllerSnapshotOut == NULL
        || frameBufferSize < SMOLNES_RUNTIME_FRAME_BYTES
        || paletteBufferSize < SMOLNES_RUNTIME_PALETTE_FRAME_BYTES
        || cpuRamBufferSize < SMOLNES_RUNTIME_CPU_RAM_BYTES
        || prgRamBufferSize < SMOLNES_RUNTIME_PRG_RAM_BYTES) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    if (!mutableRuntime->threadRunning || !mutableRuntime->healthy || !mutableRuntime->hasLatestFrame
        || !mutableRuntime->hasLatestPaletteFrame || !mutableRuntime->hasMemorySnapshot) {
        pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
        return false;
    }

    memcpy(frameBuffer, mutableRuntime->latestFrame, SMOLNES_RUNTIME_FRAME_BYTES);
    memcpy(paletteBuffer, mutableRuntime->latestPaletteFrame, SMOLNES_RUNTIME_PALETTE_FRAME_BYTES);
    memcpy(cpuRamBuffer, mutableRuntime->cpuRamSnapshot, SMOLNES_RUNTIME_CPU_RAM_BYTES);
    memcpy(prgRamBuffer, mutableRuntime->prgRamSnapshot, SMOLNES_RUNTIME_PRG_RAM_BYTES);
    if (frameId != NULL) {
        *frameId = mutableRuntime->latestFrameId;
    }
    controllerSnapshotOut->latest_frame_id = mutableRuntime->latestFrameId;
    controllerSnapshotOut->controller1_applied_frame_id =
        mutableRuntime->latestFrameController1AppliedFrameId;
    controllerSnapshotOut->controller1_observed_timestamp_ns =
        mutableRuntime->latestFrameController1ObservedTimestampNs;
    controllerSnapshotOut->controller1_latch_timestamp_ns =
        mutableRuntime->latestFrameController1LatchTimestampNs;
    controllerSnapshotOut->controller1_request_timestamp_ns =
        mutableRuntime->latestFrameController1RequestTimestampNs;
    controllerSnapshotOut->controller1_sequence_id = mutableRuntime->latestFrameController1SequenceId;
    controllerSnapshotOut->controller1_state = mutableRuntime->latestFrameController1State;
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

bool smolnesRuntimeCopyApuSnapshot(
    const SmolnesRuntimeHandle* runtime, SmolnesApuSnapshot* snapshotOut)
{
    if (runtime == NULL || snapshotOut == NULL) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    if (!mutableRuntime->hasApuSnapshot) {
        pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
        return false;
    }

    *snapshotOut = mutableRuntime->apuSnapshot;
    pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
    return true;
}

bool smolnesRuntimeCopyApuSamples(
    const SmolnesRuntimeHandle* runtime, float* buffer, uint32_t maxSamples, uint32_t* samplesOut)
{
    if (runtime == NULL || buffer == NULL || samplesOut == NULL) {
        return false;
    }

    SmolnesRuntimeHandle* mutableRuntime = (SmolnesRuntimeHandle*)runtime;
    pthread_mutex_lock(&mutableRuntime->runtimeMutex);
    if (!mutableRuntime->hasApuSnapshot) {
        pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
        *samplesOut = 0;
        return false;
    }

    uint32_t count = mutableRuntime->apuSampleBufferCount;
    if (count > maxSamples) {
        count = maxSamples;
    }
    memcpy(buffer, mutableRuntime->apuSampleBuffer, count * sizeof(float));
    *samplesOut = count;
    pthread_mutex_unlock(&mutableRuntime->runtimeMutex);
    return true;
}

void smolnesRuntimeSetApuSampleCallback(
    SmolnesRuntimeHandle* runtime, SmolnesApuSampleCallback callback, void* userdata)
{
    if (runtime == NULL) {
        return;
    }
    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->apuSampleCallback = callback;
    runtime->apuSampleCallbackUserdata = userdata;
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

void smolnesRuntimeSetPacingMode(SmolnesRuntimeHandle* runtime, SmolnesRuntimePacingModeValue mode)
{
    if (runtime == NULL) {
        return;
    }
    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->pacingMode = mode;
    runtime->realtimePacingOriginMs = 0.0;
    runtime->realtimePacingOriginFrame = 0;
    pthread_cond_broadcast(&runtime->runtimeCond);
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

void smolnesRuntimeSetPixelOutputEnabled(SmolnesRuntimeHandle* runtime, bool enabled)
{
    if (runtime == NULL) {
        return;
    }
    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->pixelOutputEnabled = enabled;
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

void smolnesRuntimeSetRgbaOutputEnabled(SmolnesRuntimeHandle* runtime, bool enabled)
{
    if (runtime == NULL) {
        return;
    }
    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->rgbaOutputEnabled = enabled;
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

void smolnesRuntimeSetApuEnabled(SmolnesRuntimeHandle* runtime, bool enabled)
{
    if (runtime == NULL) {
        return;
    }
    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->apuEnabled = enabled;
    pthread_mutex_unlock(&runtime->runtimeMutex);
}

void smolnesRuntimeSetDetailedTimingEnabled(SmolnesRuntimeHandle* runtime, bool enabled)
{
    if (runtime == NULL) {
        return;
    }
    pthread_mutex_lock(&runtime->runtimeMutex);
    runtime->detailedTimingEnabled = enabled;
    pthread_mutex_unlock(&runtime->runtimeMutex);
}
