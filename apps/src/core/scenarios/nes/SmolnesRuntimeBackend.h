#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool smolnesRuntimeStart(const char* romPath);
bool smolnesRuntimeRunFrames(uint32_t frameCount, uint32_t timeoutMs);
void smolnesRuntimeStop(void);

bool smolnesRuntimeIsHealthy(void);
bool smolnesRuntimeIsRunning(void);
uint64_t smolnesRuntimeGetRenderedFrameCount(void);
void smolnesRuntimeGetLastErrorCopy(char* buffer, uint32_t bufferSize);

#ifdef __cplusplus
}
#endif
