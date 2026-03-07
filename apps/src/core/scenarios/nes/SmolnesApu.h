#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMOLNES_APU_SAMPLE_BUFFER_SIZE 4096u
#define SMOLNES_APU_SAMPLE_COPY_MAX 1024u

typedef struct SmolnesApuEnvelope {
    bool start;
    uint8_t divider;
    uint8_t decayLevel;
    uint8_t volume;
    bool constantVolume;
    bool loop;
} SmolnesApuEnvelope;

typedef struct SmolnesApuSweep {
    bool enabled;
    uint8_t divider;
    uint8_t period;
    bool negate;
    uint8_t shift;
    bool reload;
} SmolnesApuSweep;

typedef struct SmolnesApuPulse {
    uint8_t duty;
    uint8_t dutyPosition;
    uint16_t timerPeriod;
    uint16_t timerValue;
    uint8_t lengthCounter;
    bool enabled;
    SmolnesApuEnvelope envelope;
    SmolnesApuSweep sweep;
} SmolnesApuPulse;

typedef struct SmolnesApuTriangle {
    uint16_t timerPeriod;
    uint16_t timerValue;
    uint8_t sequencePosition;
    uint8_t lengthCounter;
    uint8_t linearCounter;
    uint8_t linearCounterReload;
    bool linearCounterControl;
    bool linearCounterReloadFlag;
    bool enabled;
} SmolnesApuTriangle;

typedef struct SmolnesApuNoise {
    uint16_t shiftRegister;
    uint16_t timerPeriod;
    uint16_t timerValue;
    bool mode;
    uint8_t lengthCounter;
    bool enabled;
    SmolnesApuEnvelope envelope;
} SmolnesApuNoise;

typedef struct SmolnesApuFrameCounter {
    uint32_t cycleCount;
    bool mode5Step;
    bool irqInhibit;
    bool irqPending;
} SmolnesApuFrameCounter;

typedef struct SmolnesApuState {
    SmolnesApuPulse pulse1;
    SmolnesApuPulse pulse2;
    SmolnesApuTriangle triangle;
    SmolnesApuNoise noise;
    SmolnesApuFrameCounter frameCounter;

    // Downsampling state.
    double sampleAccumulator;
    uint32_t sampleAccumulatorCount;
    double cycleRemainder;
    double outputSampleRate;
    double cyclesPerSample;

    // Ring buffer output.
    float sampleBuffer[SMOLNES_APU_SAMPLE_BUFFER_SIZE];
    uint32_t writePos;
    uint64_t totalSampleCount;

    uint64_t registerWriteCount;
} SmolnesApuState;

typedef struct SmolnesApuSnapshot {
    bool pulse1Enabled;
    bool pulse2Enabled;
    bool triangleEnabled;
    bool noiseEnabled;
    uint8_t pulse1LengthCounter;
    uint8_t pulse2LengthCounter;
    uint8_t triangleLengthCounter;
    uint8_t noiseLengthCounter;
    uint16_t pulse1TimerPeriod;
    uint16_t pulse2TimerPeriod;
    uint16_t triangleTimerPeriod;
    uint16_t noiseTimerPeriod;
    uint8_t pulse1Duty;
    uint8_t pulse2Duty;
    bool noiseMode;
    bool frameCounterMode5Step;
    uint64_t registerWriteCount;
    uint64_t totalSamplesGenerated;
} SmolnesApuSnapshot;

void smolnesApuInit(SmolnesApuState* state, double outputSampleRate);
void smolnesApuWrite(SmolnesApuState* state, uint16_t addr, uint8_t value);
uint8_t smolnesApuRead(SmolnesApuState* state, uint16_t addr);
void smolnesApuClock(SmolnesApuState* state, uint32_t cpuCycles);
void smolnesApuGetSnapshot(const SmolnesApuState* state, SmolnesApuSnapshot* snapshot);
uint32_t smolnesApuCopySamples(
    const SmolnesApuState* state, float* buffer, uint32_t maxSamples, uint64_t fromIndex);
uint64_t smolnesApuGetSampleCount(const SmolnesApuState* state);

#ifdef __cplusplus
}
#endif
