#pragma once

#include "SmolnesApu.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SmolnesRuntimeHandle SmolnesRuntimeHandle;

typedef struct SmolnesRuntimeProfilingSnapshot {
    double run_frames_wait_ms;
    uint64_t run_frames_wait_calls;
    double runtime_thread_idle_wait_ms;
    uint64_t runtime_thread_idle_wait_calls;
    double runtime_thread_cpu_step_ms;
    uint64_t runtime_thread_cpu_step_calls;
    double runtime_thread_frame_execution_ms;
    uint64_t runtime_thread_frame_execution_calls;
    double runtime_thread_ppu_step_ms;
    uint64_t runtime_thread_ppu_step_calls;
    double runtime_thread_ppu_visible_pixels_ms;
    uint64_t runtime_thread_ppu_visible_pixels_calls;
    double runtime_thread_ppu_sprite_eval_ms;
    uint64_t runtime_thread_ppu_sprite_eval_calls;
    double runtime_thread_ppu_prefetch_ms;
    uint64_t runtime_thread_ppu_prefetch_calls;
    double runtime_thread_ppu_other_ms;
    uint64_t runtime_thread_ppu_other_calls;
    double runtime_thread_frame_submit_ms;
    uint64_t runtime_thread_frame_submit_calls;
    double runtime_thread_event_poll_ms;
    uint64_t runtime_thread_event_poll_calls;
    double runtime_thread_present_ms;
    uint64_t runtime_thread_present_calls;
    double memory_snapshot_copy_ms;
    uint64_t memory_snapshot_copy_calls;
} SmolnesRuntimeProfilingSnapshot;

typedef struct SmolnesRuntimeControllerSnapshot {
    uint64_t latest_frame_id;
    uint64_t controller1_applied_frame_id;
    uint64_t controller1_observed_timestamp_ns;
    uint64_t controller1_latch_timestamp_ns;
    uint64_t controller1_request_timestamp_ns;
    uint64_t controller1_sequence_id;
    uint8_t controller1_state;
} SmolnesRuntimeControllerSnapshot;

typedef enum SmolnesRuntimePacingModeValue {
    SMOLNES_RUNTIME_PACING_MODE_LOCKSTEP = 0,
    SMOLNES_RUNTIME_PACING_MODE_REALTIME = 1,
} SmolnesRuntimePacingModeValue;

#define SMOLNES_RUNTIME_FRAME_WIDTH 256u
#define SMOLNES_RUNTIME_FRAME_HEIGHT 224u
#define SMOLNES_RUNTIME_FRAME_PITCH_BYTES (SMOLNES_RUNTIME_FRAME_WIDTH * 2u)
#define SMOLNES_RUNTIME_FRAME_BYTES \
    (SMOLNES_RUNTIME_FRAME_PITCH_BYTES * SMOLNES_RUNTIME_FRAME_HEIGHT)
#define SMOLNES_RUNTIME_PALETTE_FRAME_BYTES \
    (SMOLNES_RUNTIME_FRAME_WIDTH * SMOLNES_RUNTIME_FRAME_HEIGHT)
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

uint32_t smolnesRuntimeGetSavestateSize(void);

SmolnesRuntimeHandle* smolnesRuntimeCreate(void);
void smolnesRuntimeDestroy(SmolnesRuntimeHandle* runtime);

bool smolnesRuntimeStart(SmolnesRuntimeHandle* runtime, const char* romPath);
bool smolnesRuntimeRunFrames(
    SmolnesRuntimeHandle* runtime, uint32_t frameCount, uint32_t timeoutMs);
void smolnesRuntimeStop(SmolnesRuntimeHandle* runtime);

bool smolnesRuntimeIsHealthy(const SmolnesRuntimeHandle* runtime);
bool smolnesRuntimeIsRunning(const SmolnesRuntimeHandle* runtime);
uint64_t smolnesRuntimeGetRenderedFrameCount(const SmolnesRuntimeHandle* runtime);
void smolnesRuntimeSetController1State(SmolnesRuntimeHandle* runtime, uint8_t buttonMask);
void smolnesRuntimeSetController1StateObserved(
    SmolnesRuntimeHandle* runtime, uint8_t buttonMask, uint64_t observedTimestampNs);
bool smolnesRuntimeCopyLatestFrame(
    const SmolnesRuntimeHandle* runtime, uint8_t* buffer, uint32_t bufferSize, uint64_t* frameId);
bool smolnesRuntimeCopyLatestPaletteIndices(
    const SmolnesRuntimeHandle* runtime, uint8_t* buffer, uint32_t bufferSize, uint64_t* frameId);
bool smolnesRuntimeCopyMemorySnapshot(
    const SmolnesRuntimeHandle* runtime,
    uint8_t* cpuRamBuffer,
    uint32_t cpuRamBufferSize,
    uint8_t* prgRamBuffer,
    uint32_t prgRamBufferSize,
    uint64_t* frameId);
bool smolnesRuntimeCopyCpuRam(
    const SmolnesRuntimeHandle* runtime, uint8_t* buffer, uint32_t bufferSize);
bool smolnesRuntimeCopyPrgRam(
    const SmolnesRuntimeHandle* runtime, uint8_t* buffer, uint32_t bufferSize);
bool smolnesRuntimeCopyProfilingSnapshot(
    const SmolnesRuntimeHandle* runtime, SmolnesRuntimeProfilingSnapshot* snapshotOut);
bool smolnesRuntimeCopyControllerSnapshot(
    const SmolnesRuntimeHandle* runtime, SmolnesRuntimeControllerSnapshot* snapshotOut);
bool smolnesRuntimeCopyLiveSnapshot(
    const SmolnesRuntimeHandle* runtime,
    uint8_t* frameBuffer,
    uint32_t frameBufferSize,
    uint8_t* paletteBuffer,
    uint32_t paletteBufferSize,
    uint8_t* cpuRamBuffer,
    uint32_t cpuRamBufferSize,
    uint8_t* prgRamBuffer,
    uint32_t prgRamBufferSize,
    uint64_t* frameId,
    SmolnesRuntimeControllerSnapshot* controllerSnapshotOut);
bool smolnesRuntimeCopyApuSnapshot(
    const SmolnesRuntimeHandle* runtime, SmolnesApuSnapshot* snapshotOut);
bool smolnesRuntimeCopyApuSamples(
    const SmolnesRuntimeHandle* runtime, float* buffer, uint32_t maxSamples, uint32_t* samplesOut);
bool smolnesRuntimeCopySavestate(
    const SmolnesRuntimeHandle* runtime, uint8_t* buffer, uint32_t bufferSize, uint64_t* frameId);
bool smolnesRuntimeLoadSavestate(
    SmolnesRuntimeHandle* runtime, const uint8_t* buffer, uint32_t bufferSize, uint32_t timeoutMs);
void smolnesRuntimeGetLastErrorCopy(
    const SmolnesRuntimeHandle* runtime, char* buffer, uint32_t bufferSize);
void smolnesRuntimeSetApuSampleCallback(
    SmolnesRuntimeHandle* runtime, SmolnesApuSampleCallback callback, void* userdata);
void smolnesRuntimeSetPacingMode(SmolnesRuntimeHandle* runtime, SmolnesRuntimePacingModeValue mode);

#ifdef __cplusplus
}
#endif
