#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMOLNES_RUNTIME_FRAME_WIDTH 256u
#define SMOLNES_RUNTIME_FRAME_HEIGHT 224u
#define SMOLNES_RUNTIME_FRAME_PITCH_BYTES (SMOLNES_RUNTIME_FRAME_WIDTH * 2u)
#define SMOLNES_RUNTIME_FRAME_BYTES \
    (SMOLNES_RUNTIME_FRAME_PITCH_BYTES * SMOLNES_RUNTIME_FRAME_HEIGHT)
#define SMOLNES_RUNTIME_CPU_RAM_BYTES 8192u
#define SMOLNES_RUNTIME_PRG_RAM_BYTES 8192u

#define SMOLNES_RUNTIME_BUTTON_A (1u << 0)
#define SMOLNES_RUNTIME_BUTTON_B (1u << 1)
#define SMOLNES_RUNTIME_BUTTON_SELECT (1u << 2)
#define SMOLNES_RUNTIME_BUTTON_START (1u << 3)
#define SMOLNES_RUNTIME_BUTTON_UP (1u << 4)
#define SMOLNES_RUNTIME_BUTTON_DOWN (1u << 5)
#define SMOLNES_RUNTIME_BUTTON_LEFT (1u << 6)
#define SMOLNES_RUNTIME_BUTTON_RIGHT (1u << 7)

bool smolnesRuntimeStart(const char* romPath);
bool smolnesRuntimeRunFrames(uint32_t frameCount, uint32_t timeoutMs);
void smolnesRuntimeStop(void);

bool smolnesRuntimeIsHealthy(void);
bool smolnesRuntimeIsRunning(void);
uint64_t smolnesRuntimeGetRenderedFrameCount(void);
void smolnesRuntimeSetController1State(uint8_t buttonMask);
bool smolnesRuntimeCopyLatestFrame(uint8_t* buffer, uint32_t bufferSize, uint64_t* frameId);
bool smolnesRuntimeCopyCpuRam(uint8_t* buffer, uint32_t bufferSize);
bool smolnesRuntimeCopyPrgRam(uint8_t* buffer, uint32_t bufferSize);
void smolnesRuntimeGetLastErrorCopy(char* buffer, uint32_t bufferSize);

#ifdef __cplusplus
}
#endif
