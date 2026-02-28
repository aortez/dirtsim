#pragma once

#include "core/organisms/OrganismType.h"

#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

struct FitnessMetric {
    std::string key;
    std::string label;
    std::string group;
    double raw = 0.0;
    double normalized = 0.0;
    std::optional<double> reference = std::nullopt;
    std::optional<double> weight = std::nullopt;
    std::optional<double> contribution = std::nullopt;
    std::string unit;

    using serialize = zpp::bits::members<9>;
};

struct FitnessBreakdownReport {
    OrganismType organismType = OrganismType::TREE;
    std::string modelId;
    int modelVersion = 1;
    double totalFitness = 0.0;
    std::string totalFormula;
    std::vector<FitnessMetric> metrics;

    using serialize = zpp::bits::members<6>;
};

void to_json(nlohmann::json& j, const FitnessMetric& value);
void from_json(const nlohmann::json& j, FitnessMetric& value);

void to_json(nlohmann::json& j, const FitnessBreakdownReport& value);
void from_json(const nlohmann::json& j, FitnessBreakdownReport& value);

} // namespace Api
} // namespace DirtSim
