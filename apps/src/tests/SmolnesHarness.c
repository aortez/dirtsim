#include "SmolnesHarness.h"

#include <SDL2/SDL.h>
#include <stdint.h>
#include <string.h>

static int smolnesMaxFrames = 0;
static int smolnesRenderedFrames = 0;
static uint8_t smolnesKeyboardState[SDL_NUM_SCANCODES];
static uint8_t smolnesWindowStub = 0;
static uint8_t smolnesRendererStub = 0;
static uint8_t smolnesTextureStub = 0;

int smolnesWrappedInit(Uint32 flags)
{
    (void)flags;
    memset(smolnesKeyboardState, 0, sizeof(smolnesKeyboardState));
    return 0;
}

const Uint8* smolnesWrappedGetKeyboardState(int* numkeys)
{
    if (numkeys != NULL) {
        *numkeys = SDL_NUM_SCANCODES;
    }
    return smolnesKeyboardState;
}

SDL_Window* smolnesWrappedCreateWindow(const char* title, int x, int y, int w, int h, Uint32 flags)
{
    (void)title;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)flags;
    return (SDL_Window*)&smolnesWindowStub;
}

SDL_Renderer* smolnesWrappedCreateRenderer(SDL_Window* window, int index, Uint32 flags)
{
    (void)window;
    (void)index;
    (void)flags;
    return (SDL_Renderer*)&smolnesRendererStub;
}

SDL_Texture* smolnesWrappedCreateTexture(
    SDL_Renderer* renderer, Uint32 format, int access, int w, int h)
{
    (void)renderer;
    (void)format;
    (void)access;
    (void)w;
    (void)h;
    return (SDL_Texture*)&smolnesTextureStub;
}

int smolnesWrappedUpdateTexture(
    SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch)
{
    (void)texture;
    (void)rect;
    (void)pixels;
    (void)pitch;
    return 0;
}

int smolnesWrappedRenderCopy(
    SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcRect, const SDL_Rect* dstRect)
{
    (void)renderer;
    (void)texture;
    (void)srcRect;
    (void)dstRect;
    return 0;
}

void smolnesWrappedRenderPresent(SDL_Renderer* renderer)
{
    (void)renderer;
    ++smolnesRenderedFrames;
}

int smolnesWrappedPollEvent(SDL_Event* event)
{
    if (smolnesMaxFrames > 0 && smolnesRenderedFrames >= smolnesMaxFrames) {
        if (event != NULL) {
            memset(event, 0, sizeof(*event));
            event->type = SDL_QUIT;
        }
        return 1;
    }

    return 0;
}

#define SDL_CreateWindow smolnesWrappedCreateWindow
#define SDL_CreateRenderer smolnesWrappedCreateRenderer
#define SDL_CreateTexture smolnesWrappedCreateTexture
#define SDL_GetKeyboardState smolnesWrappedGetKeyboardState
#define SDL_Init smolnesWrappedInit
#define SDL_PollEvent smolnesWrappedPollEvent
#define SDL_RenderCopy smolnesWrappedRenderCopy
#define SDL_RenderPresent smolnesWrappedRenderPresent
#define SDL_UpdateTexture smolnesWrappedUpdateTexture
#define main smolnesEntryPoint

#include "../../external/smolnes/deobfuscated.c"

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

int getSmolnesRenderedFrameCount(void)
{
    return smolnesRenderedFrames;
}

int runSmolnesFrames(const char* romPath, int frameCount)
{
    if (romPath == NULL || frameCount <= 0) {
        return -1;
    }

    smolnesMaxFrames = frameCount;
    smolnesRenderedFrames = 0;

    char* argv[] = { "smolnes", (char*)romPath };
    return smolnesEntryPoint(2, argv);
}
