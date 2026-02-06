#pragma once

#include "core/ScenarioId.h"
#include <nlohmann/json_fwd.hpp>
#include <zpp_bits.h>

namespace DirtSim {

struct UserSettings {
    int timezoneIndex = 2;
    int volumePercent = 20;
    Scenario::EnumType defaultScenario = Scenario::EnumType::Sandbox;

    using serialize = zpp::bits::members<3>;
};

void from_json(const nlohmann::json& j, UserSettings& settings);
void to_json(nlohmann::json& j, const UserSettings& settings);

} // namespace DirtSim
