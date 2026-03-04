#include "Genome.h"

namespace DirtSim {

Genome::Genome(size_t weightCount) : weights(weightCount, 0.0f)
{}

Genome::Genome(size_t weightCount, WeightType value) : weights(weightCount, value)
{}

bool Genome::operator==(const Genome& other) const
{
    return weights == other.weights;
}

} // namespace DirtSim
