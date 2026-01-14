#include "StormManager.h"

#include "core/WorldLightCalculator.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace DirtSim {

void StormManager::reset()
{
    state_ = State::Dormant;
    striking_ = false;
    currentFlashIntensity_ = 0.0f;
    nextStrikeTime_ = {};
    totalStrokes_ = 0;
    currentStroke_ = 0;
    strokeStartTime_ = {};
    nextStrokeTime_ = {};
}

void StormManager::update(
    WorldLightCalculator& lightCalc, double /*deltaTime*/, double stormIntensity, std::mt19937& rng)
{
    if (stormIntensity <= 0.0) {
        if (striking_) {
            striking_ = false;
            currentFlashIntensity_ = 0.0f;
        }
        state_ = State::Dormant;
        nextStrikeTime_ = {};
        return;
    }

    switch (state_) {
        case State::Dormant:
            updateDormant(stormIntensity, rng);
            break;
        case State::Striking:
            updateStriking(lightCalc, rng);
            break;
    }
}

void StormManager::enterDormant(double stormIntensity, std::mt19937& rng)
{
    state_ = State::Dormant;
    striking_ = false;
    currentFlashIntensity_ = 0.0f;

    double intensityFactor = std::clamp(stormIntensity, 0.0, 1.0);
    double intervalRange = kMaxStrikeIntervalMs - kMinStrikeIntervalMs;
    double baseInterval = kMaxStrikeIntervalMs - (intensityFactor * intervalRange * 0.8);

    std::uniform_real_distribution<double> jitter(0.8, 1.2);
    double intervalMs = baseInterval * jitter(rng);

    auto now = std::chrono::steady_clock::now();
    nextStrikeTime_ = now + std::chrono::milliseconds(static_cast<int64_t>(intervalMs));

    spdlog::info(
        "StormManager: Next strike in {:.1f}s (intensity: {:.2f})",
        intervalMs / 1000.0,
        intensityFactor);
}

void StormManager::enterStriking(std::mt19937& rng)
{
    state_ = State::Striking;
    striking_ = true;

    std::uniform_int_distribution<int> strokeDist(kMinStrokes, kMaxStrokes);
    totalStrokes_ = strokeDist(rng);
    currentStroke_ = 0;

    auto now = std::chrono::steady_clock::now();
    strokeStartTime_ = now;
    nextStrokeTime_ = now;

    spdlog::info("StormManager: Lightning strike starting ({} strokes)", totalStrokes_);
}

void StormManager::updateDormant(double stormIntensity, std::mt19937& rng)
{
    auto now = std::chrono::steady_clock::now();

    if (nextStrikeTime_ == std::chrono::steady_clock::time_point{}) {
        enterDormant(stormIntensity, rng);
        return;
    }

    if (now >= nextStrikeTime_) {
        enterStriking(rng);
    }
}

void StormManager::updateStriking(WorldLightCalculator& lightCalc, std::mt19937& rng)
{
    auto now = std::chrono::steady_clock::now();

    if (now >= nextStrokeTime_ && currentStroke_ < totalStrokes_) {
        strokeStartTime_ = now;
        currentStroke_++;

        std::uniform_real_distribution<double> gapDist(kMinStrokeGapMs, kMaxStrokeGapMs);
        double gapMs = gapDist(rng);
        nextStrokeTime_ = now + std::chrono::milliseconds(static_cast<int64_t>(gapMs));
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - strokeStartTime_);
    double elapsedMs = static_cast<double>(elapsed.count());
    float intensity = calculateStrokeIntensity(elapsedMs);

    currentFlashIntensity_ = intensity;

    if (intensity > 0.01f) {
        applyFlash(lightCalc, intensity);
    }

    bool allStrokesDone = (currentStroke_ >= totalStrokes_);
    bool decayed = (intensity < 0.01f);

    if (allStrokesDone && decayed) {
        enterDormant(0.5, rng);
    }
}

float StormManager::calculateStrokeIntensity(double elapsedMs)
{
    if (elapsedMs < 0.0) {
        return 0.0f;
    }

    if (elapsedMs < kStrokePeakMs) {
        return 1.0f;
    }

    double decayTime = elapsedMs - kStrokePeakMs;
    double decayFactor = std::exp(-decayTime / kStrokeDecayMs * 3.0);
    return static_cast<float>(decayFactor);
}

void StormManager::applyFlash(WorldLightCalculator& lightCalc, float intensity)
{
    ColorNames::RgbF flash{ 0.9f * intensity, 0.92f * intensity, 1.0f * intensity };
    lightCalc.setAmbientBoost(flash);
}

} // namespace DirtSim
