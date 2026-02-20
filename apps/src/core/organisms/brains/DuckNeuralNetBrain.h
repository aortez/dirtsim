#pragma once

#include "core/organisms/DuckBrain.h"
#include "core/organisms/brains/Genome.h"

#include <memory>
#include <random>

namespace DirtSim {

class Duck;

class DuckNeuralNetBrain : public DuckBrain {
public:
    DuckNeuralNetBrain();
    explicit DuckNeuralNetBrain(const Genome& genome);
    explicit DuckNeuralNetBrain(uint32_t seed);
    ~DuckNeuralNetBrain() override;

    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override;

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
