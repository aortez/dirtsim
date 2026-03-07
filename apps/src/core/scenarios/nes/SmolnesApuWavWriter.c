#include "SmolnesApu.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void writeWavHeader(FILE* f, uint32_t numSamples, uint32_t sampleRate)
{
    uint32_t dataSize = numSamples * 2; // 16-bit mono.
    uint32_t fileSize = 36 + dataSize;

    fwrite("RIFF", 1, 4, f);
    fwrite(&fileSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    // fmt chunk.
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1; // PCM.
    uint16_t numChannels = 1;
    uint32_t byteRate = sampleRate * 2;
    uint16_t blockAlign = 2;
    uint16_t bitsPerSample = 16;
    fwrite(&fmtSize, 4, 1, f);
    fwrite(&audioFormat, 2, 1, f);
    fwrite(&numChannels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);

    // data chunk.
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);
}

typedef struct {
    int16_t* data;
    uint32_t count;
    uint32_t capacity;
} PcmBuffer;

static void pcmBufferInit(PcmBuffer* buf, uint32_t capacity)
{
    buf->data = (int16_t*)malloc(capacity * sizeof(int16_t));
    buf->count = 0;
    buf->capacity = capacity;
}

static void pcmBufferFree(PcmBuffer* buf)
{
    free(buf->data);
    buf->data = NULL;
}

// Drain new samples from APU into PCM buffer.
static void drainApuSamples(SmolnesApuState* state, PcmBuffer* buf, uint64_t* lastIndex)
{
    float chunk[SMOLNES_APU_SAMPLE_BUFFER_SIZE];
    uint32_t copied = smolnesApuCopySamples(state, chunk, SMOLNES_APU_SAMPLE_BUFFER_SIZE, *lastIndex);
    *lastIndex = smolnesApuGetSampleCount(state);

    for (uint32_t i = 0; i < copied; i++) {
        if (buf->count >= buf->capacity) {
            buf->capacity *= 2;
            buf->data = (int16_t*)realloc(buf->data, buf->capacity * sizeof(int16_t));
        }
        float s = chunk[i];
        if (s > 1.0f) {
            s = 1.0f;
        }
        if (s < -1.0f) {
            s = -1.0f;
        }
        buf->data[buf->count++] = (int16_t)(s * 32767.0f);
    }
}

// Timer period for a frequency: period = CPU_CLOCK / (16 * freq) - 1.
static uint16_t freqToTimer(double freqHz)
{
    return (uint16_t)(1789773.0 / (16.0 * freqHz) - 1.0);
}

// Play a note on pulse 1 and drain samples.
static void playNote(
    SmolnesApuState* state, PcmBuffer* buf, uint64_t* lastIndex,
    uint16_t timerPeriod, uint8_t duty, uint8_t volume, uint32_t durationCycles)
{
    smolnesApuWrite(state, 0x4000,
        (uint8_t)((duty << 6) | 0x30 | (volume & 0x0F)));
    smolnesApuWrite(state, 0x4002, (uint8_t)(timerPeriod & 0xFF));
    smolnesApuWrite(state, 0x4003,
        (uint8_t)(((timerPeriod >> 8) & 0x07) | (0x01 << 3)));

    // Clock in small chunks so we don't overflow the ring buffer.
    const uint32_t chunkSize = 2048;
    uint32_t remaining = durationCycles;
    while (remaining > 0) {
        uint32_t step = remaining < chunkSize ? remaining : chunkSize;
        smolnesApuClock(state, step);
        drainApuSamples(state, buf, lastIndex);
        remaining -= step;
    }
}

static void playSilence(
    SmolnesApuState* state, PcmBuffer* buf, uint64_t* lastIndex, uint32_t durationCycles)
{
    smolnesApuWrite(state, 0x4015, 0x00);

    const uint32_t chunkSize = 2048;
    uint32_t remaining = durationCycles;
    while (remaining > 0) {
        uint32_t step = remaining < chunkSize ? remaining : chunkSize;
        smolnesApuClock(state, step);
        drainApuSamples(state, buf, lastIndex);
        remaining -= step;
    }

    smolnesApuWrite(state, 0x4015, 0x0F); // Re-enable all channels.
}

// Play a note on the triangle channel.
static void playTriNote(
    SmolnesApuState* state, PcmBuffer* buf, uint64_t* lastIndex,
    uint16_t timerPeriod, uint32_t durationCycles)
{
    // Triangle uses different timer formula: freq = CPU / (32 * (period + 1)).
    // So we pass a pre-computed timer period.
    smolnesApuWrite(state, 0x4008, 0xFF); // Linear counter = 127, control = 1.
    smolnesApuWrite(state, 0x400A, (uint8_t)(timerPeriod & 0xFF));
    smolnesApuWrite(state, 0x400B,
        (uint8_t)(((timerPeriod >> 8) & 0x07) | (0x01 << 3)));

    const uint32_t chunkSize = 2048;
    uint32_t remaining = durationCycles;
    while (remaining > 0) {
        uint32_t step = remaining < chunkSize ? remaining : chunkSize;
        smolnesApuClock(state, step);
        drainApuSamples(state, buf, lastIndex);
        remaining -= step;
    }
}

static uint16_t freqToTriTimer(double freqHz)
{
    return (uint16_t)(1789773.0 / (32.0 * freqHz) - 1.0);
}

static int generateToneTest(const char* outputPath)
{
    SmolnesApuState state;
    smolnesApuInit(&state, 48000.0);

    PcmBuffer buf;
    pcmBufferInit(&buf, 48000 * 10); // Pre-allocate ~10 seconds.
    uint64_t lastIndex = 0;

    smolnesApuWrite(&state, 0x4015, 0x0F); // Enable all channels.

    // -- Section 1: Pulse scale (C4 to C5). --
    const double scale[] = {
        261.63, 293.66, 329.63, 349.23, 392.00, 440.00, 493.88, 523.25
    };
    const uint32_t noteDuration = 1789773 / 4; // ~250ms.
    const uint32_t gapDuration = 1789773 / 20; // ~50ms.

    for (int i = 0; i < 8; i++) {
        playNote(&state, &buf, &lastIndex,
            freqToTimer(scale[i]), 2, 12, noteDuration);
        playSilence(&state, &buf, &lastIndex, gapDuration);
    }

    // -- Section 2: Same A4 with 4 duty cycles (12.5%, 25%, 50%, 75%). --
    uint16_t a4 = freqToTimer(440.0);
    const uint32_t sustainDuration = 1789773 / 2; // 500ms each.

    for (uint8_t duty = 0; duty < 4; duty++) {
        playNote(&state, &buf, &lastIndex, a4, duty, 12, sustainDuration);
        playSilence(&state, &buf, &lastIndex, gapDuration);
    }

    // -- Section 3: Triangle wave scale. --
    for (int i = 0; i < 8; i++) {
        playTriNote(&state, &buf, &lastIndex,
            freqToTriTimer(scale[i]), noteDuration);
        playSilence(&state, &buf, &lastIndex, gapDuration);
    }

    // -- Section 4: Noise burst. --
    smolnesApuWrite(&state, 0x400C, 0x3F); // Constant volume, vol=15.
    smolnesApuWrite(&state, 0x400E, 0x02); // Long mode, period index 2.
    smolnesApuWrite(&state, 0x400F, 0x08); // Load length.
    {
        const uint32_t chunkSize = 2048;
        uint32_t remaining = 1789773 / 2; // 500ms.
        while (remaining > 0) {
            uint32_t step = remaining < chunkSize ? remaining : chunkSize;
            smolnesApuClock(&state, step);
            drainApuSamples(&state, &buf, &lastIndex);
            remaining -= step;
        }
    }
    playSilence(&state, &buf, &lastIndex, gapDuration);

    // Short mode noise.
    smolnesApuWrite(&state, 0x4015, 0x0F);
    smolnesApuWrite(&state, 0x400C, 0x3F);
    smolnesApuWrite(&state, 0x400E, 0x82); // Short mode, period index 2.
    smolnesApuWrite(&state, 0x400F, 0x08);
    {
        const uint32_t chunkSize = 2048;
        uint32_t remaining = 1789773 / 2;
        while (remaining > 0) {
            uint32_t step = remaining < chunkSize ? remaining : chunkSize;
            smolnesApuClock(&state, step);
            drainApuSamples(&state, &buf, &lastIndex);
            remaining -= step;
        }
    }

    // Write WAV.
    FILE* f = fopen(outputPath, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing.\n", outputPath);
        pcmBufferFree(&buf);
        return 1;
    }

    writeWavHeader(f, buf.count, 48000);
    fwrite(buf.data, 2, buf.count, f);
    fclose(f);

    fprintf(stderr, "Wrote %u samples (%.1fs) to %s\n",
        buf.count, (double)buf.count / 48000.0, outputPath);

    pcmBufferFree(&buf);
    return 0;
}

int main(int argc, char** argv)
{
    const char* output = "apu_test.wav";
    if (argc > 1) {
        output = argv[1];
    }
    return generateToneTest(output);
}
