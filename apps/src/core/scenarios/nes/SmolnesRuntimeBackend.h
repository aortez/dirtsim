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

bool smolnesRuntimeStart(const char* romPath);
bool smolnesRuntimeRunFrames(uint32_t frameCount, uint32_t timeoutMs);
void smolnesRuntimeStop(void);

bool smolnesRuntimeIsHealthy(void);
bool smolnesRuntimeIsRunning(void);
uint64_t smolnesRuntimeGetRenderedFrameCount(void);
bool smolnesRuntimeCopyLatestFrame(uint8_t* buffer, uint32_t bufferSize, uint64_t* frameId);
void smolnesRuntimeGetLastErrorCopy(char* buffer, uint32_t bufferSize);

#ifdef __cplusplus
}
#endif
