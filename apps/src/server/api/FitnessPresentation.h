#pragma once

#include "core/organisms/OrganismType.h"

#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim::Api {

struct FitnessPresentationMetric {
    std::string key;
    std::string label;
    double value = 0.0;
    std::optional<double> reference = std::nullopt;
    std::optional<double> normalized = std::nullopt;
    std::string unit;

    using serialize = zpp::bits::members<6>;
};

struct FitnessPresentationSection {
    std::string key;
    std::string label;
    std::optional<double> score = std::nullopt;
    std::vector<FitnessPresentationMetric> metrics;

    using serialize = zpp::bits::members<4>;
};

struct FitnessPresentation {
    OrganismType organismType = OrganismType::TREE;
    std::string modelId;
    double totalFitness = 0.0;
    std::string summary;
    std::vector<FitnessPresentationSection> sections;

    using serialize = zpp::bits::members<5>;
};

void to_json(nlohmann::json& j, const FitnessPresentationMetric& value);
void from_json(const nlohmann::json& j, FitnessPresentationMetric& value);

void to_json(nlohmann::json& j, const FitnessPresentationSection& value);
void from_json(const nlohmann::json& j, FitnessPresentationSection& value);

void to_json(nlohmann::json& j, const FitnessPresentation& value);
void from_json(const nlohmann::json& j, FitnessPresentation& value);

} // namespace DirtSim::Api
