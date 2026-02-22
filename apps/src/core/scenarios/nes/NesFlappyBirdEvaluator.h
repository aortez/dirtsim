#pragma once

#include "core/organisms/evolution/NesPolicyLayout.h"

#include <array>
#include <cstdint>

namespace DirtSim {

struct NesFlappyBirdState {
    uint8_t gameState = 0;
    float birdY = 0.0f;
    float birdYFraction = 0.0f;
    float birdVelocity = 0.0f;
    uint8_t scrollX = 0;
    uint8_t scrollNt = 0;
    uint8_t nt0Pipe0Gap = 0;
    uint8_t nt0Pipe1Gap = 0;
    uint8_t nt1Pipe0Gap = 0;
    uint8_t nt1Pipe1Gap = 0;
    int score = 0;
};

struct NesFlappyBirdEvaluatorInput {
    NesFlappyBirdState state;
    uint8_t previousControllerMask = 0;
};

struct NesFlappyBirdEvaluatorOutput {
    bool done = false;
    double rewardDelta = 0.0;
    uint8_t gameState = 0;
    std::array<float, NesPolicyLayout::InputCount> features{};
};

class NesFlappyBirdEvaluator {
public:
    void reset();
    NesFlappyBirdEvaluatorOutput evaluate(const NesFlappyBirdEvaluatorInput& input);

private:
    bool didApplyDeathPenalty_ = false;
    bool hasLastScore_ = false;
    int lastScore_ = 0;
};

} // namespace DirtSim
