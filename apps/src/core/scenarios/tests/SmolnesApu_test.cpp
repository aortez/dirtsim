#include "core/scenarios/nes/SmolnesApu.h"
#include <cmath>
#include <gtest/gtest.h>

class SmolnesApuTest : public ::testing::Test {
protected:
    SmolnesApuState state{};

    void SetUp() override { smolnesApuInit(&state, 48000.0); }
};

TEST_F(SmolnesApuTest, InitState)
{
    EXPECT_EQ(state.noise.shiftRegister, 1);
    EXPECT_FALSE(state.pulse1.enabled);
    EXPECT_FALSE(state.pulse2.enabled);
    EXPECT_FALSE(state.triangle.enabled);
    EXPECT_FALSE(state.noise.enabled);
    EXPECT_EQ(state.pulse1.lengthCounter, 0);
    EXPECT_EQ(state.totalSampleCount, 0u);
    EXPECT_EQ(state.registerWriteCount, 0u);
}

TEST_F(SmolnesApuTest, StatusWriteReadEnablesChannels)
{
    // Enable all four channels.
    smolnesApuWrite(&state, 0x4015, 0x0F);
    EXPECT_TRUE(state.pulse1.enabled);
    EXPECT_TRUE(state.pulse2.enabled);
    EXPECT_TRUE(state.triangle.enabled);
    EXPECT_TRUE(state.noise.enabled);

    // Read status returns 0 because no length counters have been loaded.
    EXPECT_EQ(smolnesApuRead(&state, 0x4015), 0);

    // Disable pulse 1.
    smolnesApuWrite(&state, 0x4015, 0x0E);
    EXPECT_FALSE(state.pulse1.enabled);
}

TEST_F(SmolnesApuTest, PulseRegistersSetDutyAndTimerPeriod)
{
    smolnesApuWrite(&state, 0x4015, 0x01); // Enable pulse 1.
    smolnesApuWrite(&state, 0x4000, 0x80); // Duty = 2 (50%), volume = 0.
    EXPECT_EQ(state.pulse1.duty, 2);

    // Timer period from $4002/$4003.
    smolnesApuWrite(&state, 0x4002, 0xFD); // Low 8 bits = 253.
    smolnesApuWrite(&state, 0x4003, 0x00); // High 3 bits = 0, length index = 0.
    EXPECT_EQ(state.pulse1.timerPeriod, 253);
}

TEST_F(SmolnesApuTest, LengthCounterLoadFromTable)
{
    smolnesApuWrite(&state, 0x4015, 0x01); // Enable pulse 1.
    smolnesApuWrite(&state, 0x4000, 0x30); // Constant volume, halt.
    // Write $4003 with length index 1 (value >> 3 = 1) => table[1] = 254.
    smolnesApuWrite(&state, 0x4003, 0x08);
    EXPECT_EQ(state.pulse1.lengthCounter, 254);

    // Status read should show pulse 1 active.
    EXPECT_EQ(smolnesApuRead(&state, 0x4015) & 1, 1);
}

TEST_F(SmolnesApuTest, LengthCounterDisabledChannelZeros)
{
    smolnesApuWrite(&state, 0x4015, 0x01);
    smolnesApuWrite(&state, 0x4000, 0x30);
    smolnesApuWrite(&state, 0x4003, 0x08);
    EXPECT_GT(state.pulse1.lengthCounter, 0);

    // Disable pulse 1 via status.
    smolnesApuWrite(&state, 0x4015, 0x00);
    EXPECT_EQ(state.pulse1.lengthCounter, 0);
}

TEST_F(SmolnesApuTest, EnvelopeDecayOnQuarterFrame)
{
    smolnesApuWrite(&state, 0x4015, 0x01);
    // Non-constant volume, volume/period = 3, no halt/loop.
    smolnesApuWrite(&state, 0x4000, 0x03);
    smolnesApuWrite(&state, 0x4002, 0x80);
    smolnesApuWrite(&state, 0x4003, 0x08);

    // The envelope start flag is set by $4003 write.
    EXPECT_TRUE(state.pulse1.envelope.start);

    // Clock enough to hit the first quarter frame (7457 cycles).
    smolnesApuClock(&state, 7457);
    // After first quarter frame, envelope should have initialized.
    EXPECT_FALSE(state.pulse1.envelope.start);
    EXPECT_EQ(state.pulse1.envelope.decayLevel, 15);
}

TEST_F(SmolnesApuTest, ConstantVolumeMode)
{
    smolnesApuWrite(&state, 0x4015, 0x01);
    // Constant volume = 1, volume = 10.
    smolnesApuWrite(&state, 0x4000, 0x1A);
    EXPECT_TRUE(state.pulse1.envelope.constantVolume);
    EXPECT_EQ(state.pulse1.envelope.volume, 10);
}

TEST_F(SmolnesApuTest, SweepReload)
{
    smolnesApuWrite(&state, 0x4015, 0x01);
    // Enable sweep, period=2, negate, shift=1.
    smolnesApuWrite(&state, 0x4001, 0xA9);
    EXPECT_TRUE(state.pulse1.sweep.enabled);
    EXPECT_EQ(state.pulse1.sweep.period, 2);
    EXPECT_TRUE(state.pulse1.sweep.negate);
    EXPECT_EQ(state.pulse1.sweep.shift, 1);
    EXPECT_TRUE(state.pulse1.sweep.reload);
}

TEST_F(SmolnesApuTest, LengthCounterDecrementsOnHalfFrame)
{
    smolnesApuWrite(&state, 0x4015, 0x01);
    // No halt (loop=0).
    smolnesApuWrite(&state, 0x4000, 0x00);
    smolnesApuWrite(&state, 0x4003, 0x08); // Length index 1 => 254.
    EXPECT_EQ(state.pulse1.lengthCounter, 254);

    // Clock to half frame at 14913 cycles.
    smolnesApuClock(&state, 14913);
    EXPECT_LT(state.pulse1.lengthCounter, 254);
}

TEST_F(SmolnesApuTest, LengthCounterHaltPreventsDecrement)
{
    smolnesApuWrite(&state, 0x4015, 0x01);
    // Halt/loop flag set (bit 5).
    smolnesApuWrite(&state, 0x4000, 0x20);
    smolnesApuWrite(&state, 0x4003, 0x08);
    const uint8_t initial = state.pulse1.lengthCounter;

    // Clock past half frame.
    smolnesApuClock(&state, 14913);
    EXPECT_EQ(state.pulse1.lengthCounter, initial);
}

TEST_F(SmolnesApuTest, TriangleLinearCounter)
{
    smolnesApuWrite(&state, 0x4015, 0x04); // Enable triangle.
    // Linear counter reload = 20, control = 1.
    smolnesApuWrite(&state, 0x4008, 0x94); // bit7=1 (control), low 7 = 20.
    smolnesApuWrite(&state, 0x400B, 0x08); // Load length, set reload flag.

    // After quarter frame, linear counter should be loaded.
    smolnesApuClock(&state, 7457);
    EXPECT_EQ(state.triangle.linearCounter, 20);
}

TEST_F(SmolnesApuTest, NoiseLfsrShortMode)
{
    smolnesApuWrite(&state, 0x4015, 0x08); // Enable noise.
    smolnesApuWrite(&state, 0x400E, 0x80); // Short mode, period index 0.
    EXPECT_TRUE(state.noise.mode);
    EXPECT_EQ(state.noise.timerPeriod, 4);

    // Initial LFSR is 1.
    EXPECT_EQ(state.noise.shiftRegister, 1);

    // Clock some cycles - LFSR should change.
    smolnesApuClock(&state, 100);
    EXPECT_NE(state.noise.shiftRegister, 1);
}

TEST_F(SmolnesApuTest, NoiseLfsrLongMode)
{
    smolnesApuWrite(&state, 0x4015, 0x08);
    smolnesApuWrite(&state, 0x400E, 0x00); // Long mode, period index 0.
    EXPECT_FALSE(state.noise.mode);
}

TEST_F(SmolnesApuTest, FrameCounter5StepMode)
{
    // Write $4017 with mode=1 (5-step).
    smolnesApuWrite(&state, 0x4017, 0x80);
    EXPECT_TRUE(state.frameCounter.mode5Step);
    EXPECT_EQ(state.frameCounter.cycleCount, 0u);
}

TEST_F(SmolnesApuTest, MixerSilenceWhenDisabled)
{
    // All channels disabled, clock some cycles.
    smolnesApuClock(&state, 1000);

    // All samples should be zero/silent.
    float samples[100];
    uint32_t count = smolnesApuCopySamples(&state, samples, 100, 0);
    EXPECT_GT(count, 0u);

    bool allSilent = true;
    for (uint32_t i = 0; i < count; i++) {
        if (std::fabs(samples[i]) > 0.0001f) {
            allSilent = false;
            break;
        }
    }
    EXPECT_TRUE(allSilent);
}

TEST_F(SmolnesApuTest, MixerNonZeroWhenActive)
{
    // Configure pulse 1 with audible output.
    smolnesApuWrite(&state, 0x4015, 0x01);
    smolnesApuWrite(&state, 0x4000, 0xBF); // Duty 2, halt, constant vol, vol=15.
    smolnesApuWrite(&state, 0x4002, 0xFD); // Timer low = 253.
    smolnesApuWrite(&state, 0x4003, 0x08); // Timer high = 0, length index 1.

    // Clock enough to produce samples.
    smolnesApuClock(&state, 5000);

    float samples[100];
    uint32_t count = smolnesApuCopySamples(&state, samples, 100, 0);
    EXPECT_GT(count, 0u);

    bool hasNonZero = false;
    for (uint32_t i = 0; i < count; i++) {
        if (std::fabs(samples[i]) > 0.001f) {
            hasNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(hasNonZero);
}

TEST_F(SmolnesApuTest, SampleBufferProducesAtOutputRate)
{
    // Clock 48000 CPU cycles (~26.8ms at 1.789MHz).
    // At 48kHz output, expect ~26.8 samples.
    smolnesApuClock(&state, 48000);
    uint64_t sampleCount = smolnesApuGetSampleCount(&state);
    // 48000 / 37.28 ~= 1287 samples.
    EXPECT_GT(sampleCount, 1000u);
    EXPECT_LT(sampleCount, 1500u);
}

TEST_F(SmolnesApuTest, RegisterWriteCountIncrements)
{
    EXPECT_EQ(state.registerWriteCount, 0u);
    smolnesApuWrite(&state, 0x4015, 0x0F);
    EXPECT_EQ(state.registerWriteCount, 1u);
    smolnesApuWrite(&state, 0x4000, 0x80);
    EXPECT_EQ(state.registerWriteCount, 2u);
}

TEST_F(SmolnesApuTest, SnapshotCapture)
{
    smolnesApuWrite(&state, 0x4015, 0x0F);
    smolnesApuWrite(&state, 0x4000, 0xBF);
    smolnesApuWrite(&state, 0x4002, 0x80);
    smolnesApuWrite(&state, 0x4003, 0x08);
    smolnesApuClock(&state, 1000);

    SmolnesApuSnapshot snapshot{};
    smolnesApuGetSnapshot(&state, &snapshot);

    EXPECT_TRUE(snapshot.pulse1Enabled);
    EXPECT_TRUE(snapshot.pulse2Enabled);
    EXPECT_TRUE(snapshot.triangleEnabled);
    EXPECT_TRUE(snapshot.noiseEnabled);
    EXPECT_GT(snapshot.registerWriteCount, 0u);
    EXPECT_GT(snapshot.totalSamplesGenerated, 0u);
}

TEST_F(SmolnesApuTest, Pulse440HzFrequencyViaSamples)
{
    // Configure pulse 1 for ~440Hz.
    // NES frequency = CPU_CLOCK / (16 * (timer_period + 1)).
    // 440 = 1789773 / (16 * (period + 1)) => period = 253.
    smolnesApuWrite(&state, 0x4015, 0x01);
    smolnesApuWrite(&state, 0x4000, 0xBF); // Duty 2 (50%), halt, constant vol=15.
    smolnesApuWrite(&state, 0x4002, 0xFD); // Timer low = 253.
    smolnesApuWrite(&state, 0x4003, 0x08); // Timer high = 0, length index 1.

    // Generate ~50ms worth of audio.
    const uint32_t cpuCyclesFor50ms = 89489; // 1789773 * 0.05.
    smolnesApuClock(&state, cpuCyclesFor50ms);

    // Collect samples.
    float samples[4096];
    uint32_t count = smolnesApuCopySamples(&state, samples, 4096, 0);
    EXPECT_GT(count, 1000u);

    // Count zero crossings (transitions from <=0 to >0 or vice versa).
    uint32_t zeroCrossings = 0;
    for (uint32_t i = 1; i < count; i++) {
        if ((samples[i] > 0.001f && samples[i - 1] <= 0.001f)
            || (samples[i] <= 0.001f && samples[i - 1] > 0.001f)) {
            zeroCrossings++;
        }
    }

    // Each cycle of a square wave has 2 zero crossings.
    // In 50ms at 440Hz, expect ~44 cycles = ~88 crossings.
    double durationSec = (double)count / 48000.0;
    double measuredFreq = (double)zeroCrossings / (2.0 * durationSec);

    EXPECT_NEAR(measuredFreq, 440.0, 40.0)
        << "Expected ~440Hz, got " << measuredFreq << "Hz (crossings=" << zeroCrossings
        << ", samples=" << count << ")";
}
