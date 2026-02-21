#include "SmolnesRuntimeBackend.h"

#include <SDL2/SDL.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t gRuntimeMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gRuntimeCond = PTHREAD_COND_INITIALIZER;

static pthread_t gRuntimeThread;
static bool gThreadJoinable = false;
static bool gThreadRunning = false;
static bool gStopRequested = false;
static bool gHealthy = false;

static uint64_t gRenderedFrames = 0;
static uint64_t gTargetFrames = 0;

static char gLastError[256] = { 0 };
static char gRomPath[1024] = { 0 };

static uint8_t gKeyboardState[SDL_NUM_SCANCODES] = { 0 };
static uint8_t gWindowStub = 0;
static uint8_t gRendererStub = 0;
static uint8_t gTextureStub = 0;

int smolnesRuntimeEntryPoint(int argc, char** argv);

static void clearLastErrorLocked()
{
    gLastError[0] = '\0';
}

static void setLastErrorLocked(const char* message)
{
    if (message == NULL) {
        clearLastErrorLocked();
        return;
    }
    snprintf(gLastError, sizeof(gLastError), "%s", message);
}

static void setLastError(const char* message)
{
    pthread_mutex_lock(&gRuntimeMutex);
    setLastErrorLocked(message);
    pthread_mutex_unlock(&gRuntimeMutex);
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

static void* runtimeThreadMain(void* arg)
{
    (void)arg;
    char* argv[] = { "smolnes", gRomPath };
    const int exitCode = smolnesRuntimeEntryPoint(2, argv);

    pthread_mutex_lock(&gRuntimeMutex);
    gThreadRunning = false;
    if (!gStopRequested && exitCode != 0) {
        gHealthy = false;
        setLastErrorLocked("smolnes runtime exited with an error.");
    }
    pthread_cond_broadcast(&gRuntimeCond);
    pthread_mutex_unlock(&gRuntimeMutex);
    return NULL;
}

int smolnesRuntimeWrappedInit(Uint32 flags)
{
    (void)flags;
    memset(gKeyboardState, 0, sizeof(gKeyboardState));
    return 0;
}

const Uint8* smolnesRuntimeWrappedGetKeyboardState(int* numkeys)
{
    if (numkeys != NULL) {
        *numkeys = SDL_NUM_SCANCODES;
    }
    return gKeyboardState;
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
    return (SDL_Window*)&gWindowStub;
}

SDL_Renderer* smolnesRuntimeWrappedCreateRenderer(SDL_Window* window, int index, Uint32 flags)
{
    (void)window;
    (void)index;
    (void)flags;
    return (SDL_Renderer*)&gRendererStub;
}

SDL_Texture* smolnesRuntimeWrappedCreateTexture(
    SDL_Renderer* renderer, Uint32 format, int access, int w, int h)
{
    (void)renderer;
    (void)format;
    (void)access;
    (void)w;
    (void)h;
    return (SDL_Texture*)&gTextureStub;
}

int smolnesRuntimeWrappedUpdateTexture(
    SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch)
{
    (void)texture;
    (void)rect;
    (void)pixels;
    (void)pitch;
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

    pthread_mutex_lock(&gRuntimeMutex);
    while (!gStopRequested && gRenderedFrames >= gTargetFrames) {
        pthread_cond_wait(&gRuntimeCond, &gRuntimeMutex);
    }
    if (!gStopRequested) {
        ++gRenderedFrames;
        pthread_cond_broadcast(&gRuntimeCond);
    }
    pthread_mutex_unlock(&gRuntimeMutex);
}

int smolnesRuntimeWrappedPollEvent(SDL_Event* event)
{
    pthread_mutex_lock(&gRuntimeMutex);
    const bool shouldStop = gStopRequested;
    pthread_mutex_unlock(&gRuntimeMutex);

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
#undef main

bool smolnesRuntimeStart(const char* romPath)
{
    if (romPath == NULL || romPath[0] == '\0') {
        setLastError("ROM path is empty.");
        return false;
    }

    pthread_mutex_lock(&gRuntimeMutex);
    if (gThreadRunning) {
        setLastErrorLocked("smolnes runtime is already running.");
        pthread_mutex_unlock(&gRuntimeMutex);
        return false;
    }
    const bool joinOldThread = gThreadJoinable;
    pthread_mutex_unlock(&gRuntimeMutex);

    if (joinOldThread) {
        pthread_join(gRuntimeThread, NULL);
        pthread_mutex_lock(&gRuntimeMutex);
        gThreadJoinable = false;
        pthread_mutex_unlock(&gRuntimeMutex);
    }

    pthread_mutex_lock(&gRuntimeMutex);
    clearLastErrorLocked();

    snprintf(gRomPath, sizeof(gRomPath), "%s", romPath);

    gStopRequested = false;
    gHealthy = true;
    gRenderedFrames = 0;
    gTargetFrames = 0;
    gThreadRunning = true;

    const int createResult = pthread_create(&gRuntimeThread, NULL, runtimeThreadMain, NULL);
    if (createResult != 0) {
        gThreadRunning = false;
        gHealthy = false;
        setLastErrorLocked("Failed to start smolnes runtime thread.");
        pthread_mutex_unlock(&gRuntimeMutex);
        return false;
    }

    gThreadJoinable = true;
    pthread_mutex_unlock(&gRuntimeMutex);
    return true;
}

bool smolnesRuntimeRunFrames(uint32_t frameCount, uint32_t timeoutMs)
{
    if (frameCount == 0) {
        return true;
    }

    pthread_mutex_lock(&gRuntimeMutex);
    if (!gThreadRunning || !gHealthy) {
        setLastErrorLocked("smolnes runtime is not healthy.");
        pthread_mutex_unlock(&gRuntimeMutex);
        return false;
    }

    const uint64_t requestedFrames = gTargetFrames + frameCount;
    gTargetFrames = requestedFrames;
    pthread_cond_broadcast(&gRuntimeCond);

    const struct timespec deadline = buildDeadline(timeoutMs);
    while (gRenderedFrames < requestedFrames && gThreadRunning && gHealthy) {
        int waitResult = 0;
        if (timeoutMs == 0) {
            waitResult = pthread_cond_wait(&gRuntimeCond, &gRuntimeMutex);
        }
        else {
            waitResult = pthread_cond_timedwait(&gRuntimeCond, &gRuntimeMutex, &deadline);
        }

        if (waitResult == ETIMEDOUT) {
            gHealthy = false;
            setLastErrorLocked("Timed out waiting for smolnes frame progression.");
            pthread_mutex_unlock(&gRuntimeMutex);
            return false;
        }
    }

    if (gRenderedFrames < requestedFrames) {
        gHealthy = false;
        setLastErrorLocked("smolnes runtime stopped before requested frames completed.");
        pthread_mutex_unlock(&gRuntimeMutex);
        return false;
    }

    pthread_mutex_unlock(&gRuntimeMutex);
    return true;
}

void smolnesRuntimeStop(void)
{
    pthread_mutex_lock(&gRuntimeMutex);
    const bool joinThread = gThreadJoinable;
    gStopRequested = true;
    pthread_cond_broadcast(&gRuntimeCond);
    pthread_mutex_unlock(&gRuntimeMutex);

    if (joinThread) {
        pthread_join(gRuntimeThread, NULL);
    }

    pthread_mutex_lock(&gRuntimeMutex);
    gThreadJoinable = false;
    gThreadRunning = false;
    gStopRequested = false;
    gTargetFrames = gRenderedFrames;
    pthread_cond_broadcast(&gRuntimeCond);
    pthread_mutex_unlock(&gRuntimeMutex);
}

bool smolnesRuntimeIsHealthy(void)
{
    pthread_mutex_lock(&gRuntimeMutex);
    const bool healthy = gHealthy;
    pthread_mutex_unlock(&gRuntimeMutex);
    return healthy;
}

bool smolnesRuntimeIsRunning(void)
{
    pthread_mutex_lock(&gRuntimeMutex);
    const bool running = gThreadRunning;
    pthread_mutex_unlock(&gRuntimeMutex);
    return running;
}

uint64_t smolnesRuntimeGetRenderedFrameCount(void)
{
    pthread_mutex_lock(&gRuntimeMutex);
    const uint64_t frameCount = gRenderedFrames;
    pthread_mutex_unlock(&gRuntimeMutex);
    return frameCount;
}

void smolnesRuntimeGetLastErrorCopy(char* buffer, uint32_t bufferSize)
{
    if (buffer == NULL || bufferSize == 0) {
        return;
    }

    pthread_mutex_lock(&gRuntimeMutex);
    snprintf(buffer, bufferSize, "%s", gLastError);
    pthread_mutex_unlock(&gRuntimeMutex);
}
