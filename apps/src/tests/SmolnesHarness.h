#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int getSmolnesRenderedFrameCount(void);
int runSmolnesFrames(const char* romPath, int frameCount);

#ifdef __cplusplus
}
#endif
