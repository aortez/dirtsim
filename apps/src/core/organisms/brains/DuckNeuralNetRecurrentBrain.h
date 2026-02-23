#pragma once

#include "core/organisms/DuckBrain.h"
#include "core/organisms/brains/Genome.h"

#include <memory>
#include <random>

namespace DirtSim {

class Duck;

class DuckNeuralNetRecurrentBrain : public DuckBrain {
public:
    DuckNeuralNetRecurrentBrain();
    explicit DuckNeuralNetRecurrentBrain(const Genome& genome);
    explicit DuckNeuralNetRecurrentBrain(uint32_t seed);
    ~DuckNeuralNetRecurrentBrain() override;

    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override;
    DuckInput inferInput(const DuckSensoryData& sensory);

    Genome getGenome() const;
    void setGenome(const Genome& genome);

    static Genome randomGenome(std::mt19937& rng);
    static bool isGenomeCompatible(const Genome& genome);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    float lastMoveX_ = 0.0f;
    bool jumpHeld_ = false;
};

} // namespace DirtSim
