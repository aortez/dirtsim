#pragma once

#include "core/organisms/evolution/GenomeMetadata.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

/**
 * Progress update broadcast from server during evolution.
 * Not a request/response — pushed to subscribed clients.
 */
struct EvolutionProgress {
    int generation = 0;
    int maxGenerations = 0;
    int currentEval = 0;
    int populationSize = 0;
    double bestFitnessThisGen = 0.0;
    double bestFitnessAllTime = 0.0;
    double averageFitness = 0.0;
    GenomeId bestGenomeId{};

    nlohmann::json toJson() const;
    static constexpr const char* name() { return "EvolutionProgress"; }

    using serialize = zpp::bits::members<8>;
};

} // namespace Api
} // namespace DirtSim
