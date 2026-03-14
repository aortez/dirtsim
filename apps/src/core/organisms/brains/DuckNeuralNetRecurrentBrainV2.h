#pragma once

#include "core/organisms/DuckBrain.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/GenomeLayout.h"

#include <memory>
#include <random>

namespace DirtSim {

class Duck;

class DuckNeuralNetRecurrentBrainV2 : public DuckBrain {
public:
    struct ControllerOutput {
        float x = 0.0f;
        float y = 0.0f;
        bool a = false;
        bool b = false;
        float xRaw = 0.0f;
        float yRaw = 0.0f;
        float aRaw = 0.0f;
        float bRaw = 0.0f;
    };

    DuckNeuralNetRecurrentBrainV2();
    explicit DuckNeuralNetRecurrentBrainV2(const Genome& genome);
    explicit DuckNeuralNetRecurrentBrainV2(uint32_t seed);
    ~DuckNeuralNetRecurrentBrainV2() override;

    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override;

    // Advances recurrent state. Call at most one inference method per tick.
    DuckInput inferInput(const DuckSensoryData& sensory);

    // Advances recurrent state. Intended for NES-style controller mapping (x/y + A/B).
    ControllerOutput inferControllerOutput(const DuckSensoryData& sensory);

    Genome getGenome() const;
    void setGenome(const Genome& genome);

    static Genome randomGenome(std::mt19937& rng);
    static bool isGenomeCompatible(const Genome& genome);
    static GenomeLayout getGenomeLayout();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    float lastMoveX_ = 0.0f;
    float lastMoveY_ = 0.0f;
    bool buttonAHeld_ = false;
    bool buttonBHeld_ = false;
};

} // namespace DirtSim
