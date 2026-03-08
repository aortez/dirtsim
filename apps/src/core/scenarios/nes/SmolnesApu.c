#include "SmolnesApu.h"

#include <string.h>

// NES CPU clock rate in Hz.
static const double kCpuClockRate = 1789773.0;

static const uint8_t kLengthCounterTable[32] = {
    10, 254, 20,  2,  40,  4,  80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18,  48, 20,  96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

static const uint16_t kNoisePeriodTable[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

static const uint8_t kDutyTable[4][8] = {
    { 0, 1, 0, 0, 0, 0, 0, 0 },  // 12.5%
    { 0, 1, 1, 0, 0, 0, 0, 0 },  // 25%
    { 0, 1, 1, 1, 1, 0, 0, 0 },  // 50%
    { 1, 0, 0, 1, 1, 1, 1, 1 }   // 75%
};

static const uint8_t kTriangleSequence[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
     0,  1,  2,  3,  4,  5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

// Frame counter thresholds in CPU cycles (NTSC, 4-step mode).
static const uint32_t kFrameCounterStep4[4] = { 7457, 14913, 22371, 29829 };
// 5-step mode thresholds.
static const uint32_t kFrameCounterStep5[5] = { 7457, 14913, 22371, 29829, 37281 };

static void clockEnvelope(SmolnesApuEnvelope* env)
{
    if (env->start) {
        env->start = false;
        env->decayLevel = 15;
        env->divider = env->volume;
        return;
    }

    if (env->divider > 0) {
        env->divider--;
        return;
    }

    env->divider = env->volume;
    if (env->decayLevel > 0) {
        env->decayLevel--;
    } else if (env->loop) {
        env->decayLevel = 15;
    }
}

static uint8_t envelopeOutput(const SmolnesApuEnvelope* env)
{
    return env->constantVolume ? env->volume : env->decayLevel;
}

static void applySweep(SmolnesApuPulse* pulse, bool isPulse1)
{
    if (!pulse->sweep.enabled || pulse->sweep.shift == 0) {
        return;
    }
    uint16_t delta = pulse->timerPeriod >> pulse->sweep.shift;
    if (pulse->sweep.negate) {
        // Pulse 1 uses one's complement (subtract delta + 1).
        // Pulse 2 uses two's complement (subtract delta).
        uint16_t sub = delta + (isPulse1 ? 1 : 0);
        if (sub <= pulse->timerPeriod) {
            pulse->timerPeriod -= sub;
        }
    } else {
        uint16_t target = pulse->timerPeriod + delta;
        if (target <= 0x7FF) {
            pulse->timerPeriod = target;
        }
    }
}

static void clockPulseSweep(SmolnesApuPulse* pulse, bool isPulse1)
{
    if (pulse->sweep.reload) {
        if (pulse->sweep.divider == 0) {
            applySweep(pulse, isPulse1);
        }
        pulse->sweep.divider = pulse->sweep.period;
        pulse->sweep.reload = false;
        return;
    }

    if (pulse->sweep.divider > 0) {
        pulse->sweep.divider--;
        return;
    }

    pulse->sweep.divider = pulse->sweep.period;
    applySweep(pulse, isPulse1);
}

static void clockQuarterFrame(SmolnesApuState* state)
{
    clockEnvelope(&state->pulse1.envelope);
    clockEnvelope(&state->pulse2.envelope);
    clockEnvelope(&state->noise.envelope);

    // Triangle linear counter.
    if (state->triangle.linearCounterReloadFlag) {
        state->triangle.linearCounter = state->triangle.linearCounterReload;
    } else if (state->triangle.linearCounter > 0) {
        state->triangle.linearCounter--;
    }
    if (!state->triangle.linearCounterControl) {
        state->triangle.linearCounterReloadFlag = false;
    }
}

static void clockHalfFrame(SmolnesApuState* state)
{
    // Length counters.
    if (state->pulse1.lengthCounter > 0 && !state->pulse1.envelope.loop) {
        state->pulse1.lengthCounter--;
    }
    if (state->pulse2.lengthCounter > 0 && !state->pulse2.envelope.loop) {
        state->pulse2.lengthCounter--;
    }
    if (state->triangle.lengthCounter > 0 && !state->triangle.linearCounterControl) {
        state->triangle.lengthCounter--;
    }
    if (state->noise.lengthCounter > 0 && !state->noise.envelope.loop) {
        state->noise.lengthCounter--;
    }

    // Sweep units.
    clockPulseSweep(&state->pulse1, true);
    clockPulseSweep(&state->pulse2, false);
}

static uint8_t pulseOutput(const SmolnesApuPulse* pulse)
{
    if (!pulse->enabled) {
        return 0;
    }
    if (pulse->lengthCounter == 0) {
        return 0;
    }
    if (pulse->timerPeriod < 8) {
        return 0;
    }
    if (!kDutyTable[pulse->duty][pulse->dutyPosition]) {
        return 0;
    }
    return envelopeOutput(&pulse->envelope);
}

static uint8_t triangleOutput(const SmolnesApuTriangle* tri)
{
    if (!tri->enabled) {
        return 0;
    }
    if (tri->lengthCounter == 0) {
        return 0;
    }
    if (tri->linearCounter == 0) {
        return 0;
    }
    return kTriangleSequence[tri->sequencePosition];
}

static uint8_t noiseOutput(const SmolnesApuNoise* noise)
{
    if (!noise->enabled) {
        return 0;
    }
    if (noise->lengthCounter == 0) {
        return 0;
    }
    if (noise->shiftRegister & 1) {
        return 0;
    }
    return envelopeOutput(&noise->envelope);
}

// Non-linear mixer from NESDev wiki. Models the NES DAC more accurately than a
// linear approximation — prevents volume compression when multiple channels are
// active simultaneously.
static float mixSamples(uint8_t p1, uint8_t p2, uint8_t tri, uint8_t nse)
{
    float pulseOut = 0.0f;
    if (p1 + p2 > 0) {
        pulseOut = 95.88f / (8128.0f / (float)(p1 + p2) + 100.0f);
    }

    float tndOut = 0.0f;
    float tndSum = (float)tri / 8227.0f + (float)nse / 12241.0f;
    if (tndSum > 0.0f) {
        tndOut = 159.79f / (1.0f / tndSum + 100.0f);
    }

    return pulseOut + tndOut;
}

void smolnesApuInit(SmolnesApuState* state, double outputSampleRate)
{
    memset(state, 0, sizeof(*state));
    state->noise.shiftRegister = 1;
    state->outputSampleRate = outputSampleRate;
    state->cyclesPerSample = kCpuClockRate / outputSampleRate;
    state->sampleCallback = NULL;
    state->sampleCallbackUserdata = NULL;
}

void smolnesApuWrite(SmolnesApuState* state, uint16_t addr, uint8_t value)
{
    state->registerWriteCount++;

    switch (addr) {
    // Pulse 1: $4000-$4003.
    case 0x4000:
        state->pulse1.duty = (value >> 6) & 3;
        state->pulse1.envelope.loop = (value >> 5) & 1;
        state->pulse1.envelope.constantVolume = (value >> 4) & 1;
        state->pulse1.envelope.volume = value & 0x0F;
        break;
    case 0x4001:
        state->pulse1.sweep.enabled = (value >> 7) & 1;
        state->pulse1.sweep.period = (value >> 4) & 7;
        state->pulse1.sweep.negate = (value >> 3) & 1;
        state->pulse1.sweep.shift = value & 7;
        state->pulse1.sweep.reload = true;
        break;
    case 0x4002:
        state->pulse1.timerPeriod =
            (state->pulse1.timerPeriod & 0x0700) | (uint16_t)value;
        break;
    case 0x4003:
        state->pulse1.timerPeriod =
            (state->pulse1.timerPeriod & 0x00FF) | ((uint16_t)(value & 7) << 8);
        if (state->pulse1.enabled) {
            state->pulse1.lengthCounter = kLengthCounterTable[value >> 3];
        }
        state->pulse1.envelope.start = true;
        state->pulse1.dutyPosition = 0;
        break;

    // Pulse 2: $4004-$4007.
    case 0x4004:
        state->pulse2.duty = (value >> 6) & 3;
        state->pulse2.envelope.loop = (value >> 5) & 1;
        state->pulse2.envelope.constantVolume = (value >> 4) & 1;
        state->pulse2.envelope.volume = value & 0x0F;
        break;
    case 0x4005:
        state->pulse2.sweep.enabled = (value >> 7) & 1;
        state->pulse2.sweep.period = (value >> 4) & 7;
        state->pulse2.sweep.negate = (value >> 3) & 1;
        state->pulse2.sweep.shift = value & 7;
        state->pulse2.sweep.reload = true;
        break;
    case 0x4006:
        state->pulse2.timerPeriod =
            (state->pulse2.timerPeriod & 0x0700) | (uint16_t)value;
        break;
    case 0x4007:
        state->pulse2.timerPeriod =
            (state->pulse2.timerPeriod & 0x00FF) | ((uint16_t)(value & 7) << 8);
        if (state->pulse2.enabled) {
            state->pulse2.lengthCounter = kLengthCounterTable[value >> 3];
        }
        state->pulse2.envelope.start = true;
        state->pulse2.dutyPosition = 0;
        break;

    // Triangle: $4008-$400B.
    case 0x4008:
        state->triangle.linearCounterControl = (value >> 7) & 1;
        state->triangle.linearCounterReload = value & 0x7F;
        break;
    case 0x4009:
        // Unused.
        break;
    case 0x400A:
        state->triangle.timerPeriod =
            (state->triangle.timerPeriod & 0x0700) | (uint16_t)value;
        break;
    case 0x400B:
        state->triangle.timerPeriod =
            (state->triangle.timerPeriod & 0x00FF) | ((uint16_t)(value & 7) << 8);
        if (state->triangle.enabled) {
            state->triangle.lengthCounter = kLengthCounterTable[value >> 3];
        }
        state->triangle.linearCounterReloadFlag = true;
        break;

    // Noise: $400C-$400F.
    case 0x400C:
        state->noise.envelope.loop = (value >> 5) & 1;
        state->noise.envelope.constantVolume = (value >> 4) & 1;
        state->noise.envelope.volume = value & 0x0F;
        break;
    case 0x400D:
        // Unused.
        break;
    case 0x400E:
        state->noise.mode = (value >> 7) & 1;
        state->noise.timerPeriod = kNoisePeriodTable[value & 0x0F];
        break;
    case 0x400F:
        if (state->noise.enabled) {
            state->noise.lengthCounter = kLengthCounterTable[value >> 3];
        }
        state->noise.envelope.start = true;
        break;

    // DMC: $4010-$4013 (ignored).
    case 0x4010:
    case 0x4011:
    case 0x4012:
    case 0x4013:
        break;

    // Status: $4015.
    case 0x4015:
        state->pulse1.enabled = (value >> 0) & 1;
        state->pulse2.enabled = (value >> 1) & 1;
        state->triangle.enabled = (value >> 2) & 1;
        state->noise.enabled = (value >> 3) & 1;
        if (!state->pulse1.enabled) {
            state->pulse1.lengthCounter = 0;
        }
        if (!state->pulse2.enabled) {
            state->pulse2.lengthCounter = 0;
        }
        if (!state->triangle.enabled) {
            state->triangle.lengthCounter = 0;
        }
        if (!state->noise.enabled) {
            state->noise.lengthCounter = 0;
        }
        break;

    // Frame counter: $4017.
    case 0x4017:
        state->frameCounter.mode5Step = (value >> 7) & 1;
        state->frameCounter.irqInhibit = (value >> 6) & 1;
        state->frameCounter.cycleCount = 0;
        if (state->frameCounter.mode5Step) {
            clockQuarterFrame(state);
            clockHalfFrame(state);
        }
        break;

    default:
        break;
    }
}

uint8_t smolnesApuRead(SmolnesApuState* state, uint16_t addr)
{
    if (addr != 0x4015) {
        return 0;
    }

    uint8_t status = 0;
    if (state->pulse1.lengthCounter > 0) {
        status |= 1;
    }
    if (state->pulse2.lengthCounter > 0) {
        status |= 2;
    }
    if (state->triangle.lengthCounter > 0) {
        status |= 4;
    }
    if (state->noise.lengthCounter > 0) {
        status |= 8;
    }
    state->frameCounter.irqPending = false;
    return status;
}

void smolnesApuClock(SmolnesApuState* state, uint32_t cpuCycles)
{
    for (uint32_t i = 0; i < cpuCycles; i++) {
        // Frame counter.
        state->frameCounter.cycleCount++;
        const uint32_t cc = state->frameCounter.cycleCount;

        if (!state->frameCounter.mode5Step) {
            // 4-step mode.
            if (cc == kFrameCounterStep4[0] || cc == kFrameCounterStep4[2]) {
                clockQuarterFrame(state);
            } else if (cc == kFrameCounterStep4[1]) {
                clockQuarterFrame(state);
                clockHalfFrame(state);
            } else if (cc == kFrameCounterStep4[3]) {
                clockQuarterFrame(state);
                clockHalfFrame(state);
                state->frameCounter.cycleCount = 0;
            }
        } else {
            // 5-step mode.
            if (cc == kFrameCounterStep5[0] || cc == kFrameCounterStep5[2]) {
                clockQuarterFrame(state);
            } else if (cc == kFrameCounterStep5[1] || cc == kFrameCounterStep5[3]) {
                clockQuarterFrame(state);
                clockHalfFrame(state);
            } else if (cc == kFrameCounterStep5[4]) {
                state->frameCounter.cycleCount = 0;
            }
        }

        // Pulse timers clock every 2 CPU cycles (APU half-cycle).
        if ((state->frameCounter.cycleCount & 1) == 0) {
            // Pulse 1.
            if (state->pulse1.timerValue > 0) {
                state->pulse1.timerValue--;
            } else {
                state->pulse1.timerValue = state->pulse1.timerPeriod;
                state->pulse1.dutyPosition = (state->pulse1.dutyPosition + 1) & 7;
            }

            // Pulse 2.
            if (state->pulse2.timerValue > 0) {
                state->pulse2.timerValue--;
            } else {
                state->pulse2.timerValue = state->pulse2.timerPeriod;
                state->pulse2.dutyPosition = (state->pulse2.dutyPosition + 1) & 7;
            }
        }

        // Noise clocks every CPU cycle.
        if (state->noise.timerValue > 0) {
            state->noise.timerValue--;
        } else {
            state->noise.timerValue = state->noise.timerPeriod;
            uint8_t bit = state->noise.mode ? 6 : 1;
            uint16_t feedback =
                (state->noise.shiftRegister & 1) ^
                ((state->noise.shiftRegister >> bit) & 1);
            state->noise.shiftRegister =
                (state->noise.shiftRegister >> 1) | (feedback << 14);
        }

        // Triangle clocks every CPU cycle.
        if (state->triangle.timerValue > 0) {
            state->triangle.timerValue--;
        } else {
            state->triangle.timerValue = state->triangle.timerPeriod;
            if (state->triangle.lengthCounter > 0 &&
                state->triangle.linearCounter > 0) {
                state->triangle.sequencePosition =
                    (state->triangle.sequencePosition + 1) & 31;
            }
        }

        // Mix and accumulate for downsampling.
        uint8_t p1 = pulseOutput(&state->pulse1);
        uint8_t p2 = pulseOutput(&state->pulse2);
        uint8_t tri = triangleOutput(&state->triangle);
        uint8_t nse = noiseOutput(&state->noise);
        float sample = mixSamples(p1, p2, tri, nse);
        state->sampleAccumulator += (double)sample;
        state->sampleAccumulatorCount++;

        // Downsample to output rate.
        state->cycleRemainder += 1.0;
        if (state->cycleRemainder >= state->cyclesPerSample) {
            state->cycleRemainder -= state->cyclesPerSample;
            float averaged = (float)(state->sampleAccumulator /
                                     (double)state->sampleAccumulatorCount);
            state->sampleBuffer[state->writePos] = averaged;
            state->writePos =
                (state->writePos + 1) % SMOLNES_APU_SAMPLE_BUFFER_SIZE;
            state->totalSampleCount++;
            if (state->sampleCallback) {
                state->sampleCallback(averaged, state->sampleCallbackUserdata);
            }
            state->sampleAccumulator = 0.0;
            state->sampleAccumulatorCount = 0;
        }
    }
}

void smolnesApuGetSnapshot(const SmolnesApuState* state, SmolnesApuSnapshot* snapshot)
{
    snapshot->pulse1Enabled = state->pulse1.enabled;
    snapshot->pulse2Enabled = state->pulse2.enabled;
    snapshot->triangleEnabled = state->triangle.enabled;
    snapshot->noiseEnabled = state->noise.enabled;
    snapshot->pulse1LengthCounter = state->pulse1.lengthCounter;
    snapshot->pulse2LengthCounter = state->pulse2.lengthCounter;
    snapshot->triangleLengthCounter = state->triangle.lengthCounter;
    snapshot->noiseLengthCounter = state->noise.lengthCounter;
    snapshot->pulse1TimerPeriod = state->pulse1.timerPeriod;
    snapshot->pulse2TimerPeriod = state->pulse2.timerPeriod;
    snapshot->triangleTimerPeriod = state->triangle.timerPeriod;
    snapshot->noiseTimerPeriod = state->noise.timerPeriod;
    snapshot->pulse1Duty = state->pulse1.duty;
    snapshot->pulse2Duty = state->pulse2.duty;
    snapshot->noiseMode = state->noise.mode;
    snapshot->frameCounterMode5Step = state->frameCounter.mode5Step;
    snapshot->registerWriteCount = state->registerWriteCount;
    snapshot->totalSamplesGenerated = state->totalSampleCount;
}

uint32_t smolnesApuCopySamples(
    const SmolnesApuState* state, float* buffer, uint32_t maxSamples, uint64_t fromIndex)
{
    if (state->totalSampleCount == 0 || maxSamples == 0) {
        return 0;
    }

    // Determine how many samples are available from fromIndex.
    if (fromIndex >= state->totalSampleCount) {
        return 0;
    }

    uint64_t available = state->totalSampleCount - fromIndex;
    // Clamp to ring buffer size.
    if (available > SMOLNES_APU_SAMPLE_BUFFER_SIZE) {
        available = SMOLNES_APU_SAMPLE_BUFFER_SIZE;
        fromIndex = state->totalSampleCount - SMOLNES_APU_SAMPLE_BUFFER_SIZE;
    }

    uint32_t count = (uint32_t)(available < maxSamples ? available : maxSamples);

    // Calculate the start position in the ring buffer.
    uint32_t startPos =
        (uint32_t)((state->writePos + SMOLNES_APU_SAMPLE_BUFFER_SIZE -
                     (uint32_t)(state->totalSampleCount - fromIndex)) %
                    SMOLNES_APU_SAMPLE_BUFFER_SIZE);

    for (uint32_t j = 0; j < count; j++) {
        buffer[j] = state->sampleBuffer[(startPos + j) % SMOLNES_APU_SAMPLE_BUFFER_SIZE];
    }

    return count;
}

uint64_t smolnesApuGetSampleCount(const SmolnesApuState* state)
{
    return state->totalSampleCount;
}
