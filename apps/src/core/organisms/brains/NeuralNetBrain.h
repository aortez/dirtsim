#pragma once

#include "core/organisms/TreeBrain.h"

#include <cstdint>
#include <memory>

namespace DirtSim {

struct Genome;

/**
 * Neural network brain for tree organisms.
 *
 * Uses a simple feedforward network with factorized outputs:
 * command selection (7 types) and position selection (15x15 grid).
 */
class NeuralNetBrain : public TreeBrain {
public:
    NeuralNetBrain();
    explicit NeuralNetBrain(const Genome& genome);
    explicit NeuralNetBrain(uint32_t seed);
    ~NeuralNetBrain() override;

    TreeCommand decide(const TreeSensoryData& sensory) override;

    Genome getGenome() const;
    void setGenome(const Genome& genome);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace DirtSim
