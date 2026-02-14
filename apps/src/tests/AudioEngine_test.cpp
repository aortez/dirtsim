#include "audio/AudioEngine.h"
#include "audio/api/NoteOn.h"
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <thread>

namespace DirtSim {
namespace AudioProcess {
namespace {

AudioEngineConfig makeAudioConfig()
{
    AudioEngineConfig config;
    config.deviceName = "";
    config.sampleRate = 48000;
    config.bufferFrames = 256;
    config.channels = 1;
    return config;
}

bool containsNoteId(const AudioStatus& status, uint32_t noteId)
{
    for (const auto& note : status.activeNotes) {
        if (note.noteId == noteId) {
            return true;
        }
    }
    return false;
}

bool hasNoteInHoldState(const AudioStatus& status, uint32_t noteId, AudioNoteHoldState holdState)
{
    for (const auto& note : status.activeNotes) {
        if (note.noteId == noteId && note.holdState == holdState) {
            return true;
        }
    }
    return false;
}

bool waitForStatus(
    AudioEngine& engine,
    int timeoutMs,
    const std::function<bool(const AudioStatus&)>& predicate,
    AudioStatus* outStatus = nullptr)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        const AudioStatus status = engine.getStatus();
        if (predicate(status)) {
            if (outStatus) {
                *outStatus = status;
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (outStatus) {
        *outStatus = engine.getStatus();
    }
    return false;
}

void startEngineOrFail(AudioEngine& engine)
{
    const auto result = engine.start(makeAudioConfig());
    ASSERT_FALSE(result.isError()) << result.errorValue().message;
}

} // namespace

TEST(AudioEngineTest, SupportsPolyphonyAndSelectiveNoteOff)
{
    AudioEngine engine;
    startEngineOrFail(engine);

    constexpr uint32_t firstNoteId = 1001;
    constexpr uint32_t secondNoteId = 1002;

    engine.enqueueNoteOn(261.63, 0.5, 0.002, 0.0, 0.12, Audio::Waveform::Sine, firstNoteId);
    engine.enqueueNoteOn(329.63, 0.5, 0.002, 0.0, 0.12, Audio::Waveform::Square, secondNoteId);

    ASSERT_TRUE(waitForStatus(
        engine, 2000, [](const AudioStatus& status) { return status.activeNotes.size() >= 2; }));

    engine.enqueueNoteOff(firstNoteId);

    ASSERT_TRUE(waitForStatus(engine, 2000, [](const AudioStatus& status) {
        return containsNoteId(status, secondNoteId)
            && hasNoteInHoldState(status, firstNoteId, AudioNoteHoldState::Releasing);
    }));

    engine.enqueueNoteOff(0);
    ASSERT_TRUE(waitForStatus(
        engine, 2000, [](const AudioStatus& status) { return status.activeNotes.empty(); }));

    engine.stop();
}

TEST(AudioEngineTest, VoiceStealingRemovesOldestHeldVoiceWhenPoolIsFull)
{
    AudioEngine engine;
    startEngineOrFail(engine);

    for (uint32_t noteId = 1; noteId <= 16; ++noteId) {
        const double frequency = 200.0 + static_cast<double>(noteId);
        engine.enqueueNoteOn(frequency, 0.4, 0.001, 0.0, 0.2, Audio::Waveform::Triangle, noteId);
    }

    ASSERT_TRUE(waitForStatus(
        engine, 2000, [](const AudioStatus& status) { return status.activeNotes.size() == 16; }));

    constexpr uint32_t replacementNoteId = 17;
    engine.enqueueNoteOn(999.0, 0.6, 0.001, 0.0, 0.2, Audio::Waveform::Saw, replacementNoteId);

    AudioStatus status;
    ASSERT_TRUE(waitForStatus(
        engine,
        2000,
        [](const AudioStatus& current) {
            return current.activeNotes.size() == 16 && containsNoteId(current, replacementNoteId)
                && !containsNoteId(current, 1);
        },
        &status));

    EXPECT_FALSE(containsNoteId(status, 1));
    EXPECT_TRUE(containsNoteId(status, replacementNoteId));

    engine.stop();
}

TEST(AudioEngineTest, VoiceStealingPrefersReleasingVoices)
{
    AudioEngine engine;
    startEngineOrFail(engine);

    for (uint32_t noteId = 1; noteId <= 16; ++noteId) {
        const double frequency = 400.0 + static_cast<double>(noteId);
        engine.enqueueNoteOn(frequency, 0.4, 0.001, 0.0, 0.4, Audio::Waveform::Sine, noteId);
    }

    ASSERT_TRUE(waitForStatus(
        engine, 2000, [](const AudioStatus& status) { return status.activeNotes.size() == 16; }));

    constexpr uint32_t releasingNoteId = 8;
    constexpr uint32_t replacementNoteId = 17;

    engine.enqueueNoteOff(releasingNoteId);
    engine.enqueueNoteOn(1200.0, 0.7, 0.001, 0.0, 0.4, Audio::Waveform::Square, replacementNoteId);

    ASSERT_TRUE(waitForStatus(engine, 2000, [](const AudioStatus& status) {
        return status.activeNotes.size() == 16 && containsNoteId(status, replacementNoteId)
            && !containsNoteId(status, releasingNoteId);
    }));

    engine.stop();
}

TEST(AudioEngineTest, RetriggerUpdatesExistingNoteIdInPlace)
{
    AudioEngine engine;
    startEngineOrFail(engine);

    constexpr uint32_t retriggerNoteId = 500;
    engine.enqueueNoteOn(220.0, 0.2, 0.001, 0.0, 0.15, Audio::Waveform::Sine, retriggerNoteId);

    ASSERT_TRUE(waitForStatus(engine, 2000, [](const AudioStatus& status) {
        return status.activeNotes.size() == 1 && containsNoteId(status, retriggerNoteId);
    }));

    engine.enqueueNoteOn(660.0, 0.8, 0.001, 0.0, 0.15, Audio::Waveform::Saw, retriggerNoteId);

    AudioStatus status;
    ASSERT_TRUE(waitForStatus(
        engine,
        2000,
        [](const AudioStatus& current) {
            if (current.activeNotes.size() != 1) {
                return false;
            }
            if (current.activeNotes.front().noteId != retriggerNoteId) {
                return false;
            }
            return current.activeNotes.front().frequencyHz > 640.0
                && current.activeNotes.front().frequencyHz < 680.0
                && current.activeNotes.front().amplitude > 0.75;
        },
        &status));

    EXPECT_EQ(status.activeNotes.size(), 1u);
    EXPECT_EQ(status.activeNotes.front().noteId, retriggerNoteId);

    engine.stop();
}

TEST(AudioEngineTest, PositiveDurationTransitionsToReleasing)
{
    AudioEngine engine;
    startEngineOrFail(engine);

    constexpr uint32_t timedNoteId = 700;
    engine.enqueueNoteOn(523.25, 0.3, 0.001, 0.03, 0.08, Audio::Waveform::Triangle, timedNoteId);

    ASSERT_TRUE(waitForStatus(engine, 2000, [](const AudioStatus& status) {
        return hasNoteInHoldState(status, timedNoteId, AudioNoteHoldState::Releasing);
    }));

    ASSERT_TRUE(waitForStatus(engine, 2000, [](const AudioStatus& status) {
        return !containsNoteId(status, timedNoteId);
    }));

    engine.stop();
}

TEST(AudioApiNoteOnTest, MissingDurationDefaultsToHeld)
{
    nlohmann::json json;
    json["frequency_hz"] = 440.0;
    json["amplitude"] = 0.5;

    const auto command = AudioApi::NoteOn::Command::fromJson(json);
    EXPECT_DOUBLE_EQ(command.duration_ms, 0.0);
}

TEST(AudioApiNoteOnTest, NonPositiveDurationIsAccepted)
{
    nlohmann::json json;
    json["frequency_hz"] = 440.0;
    json["amplitude"] = 0.5;
    json["duration_ms"] = -250.0;

    const auto command = AudioApi::NoteOn::Command::fromJson(json);
    EXPECT_DOUBLE_EQ(command.duration_ms, -250.0);
}

} // namespace AudioProcess
} // namespace DirtSim
