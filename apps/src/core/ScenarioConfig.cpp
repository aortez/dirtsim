#include "ScenarioConfig.h"
#include "VariantSerializer.h"

namespace DirtSim {

Scenario::EnumType getScenarioId(const ScenarioConfig& config)
{
    return static_cast<Scenario::EnumType>(config.index());
}

ScenarioConfig makeDefaultConfig(Scenario::EnumType id)
{
    switch (id) {
        case Scenario::EnumType::Benchmark:
            return Config::Benchmark{};
        case Scenario::EnumType::Clock:
            return Config::Clock{};
        case Scenario::EnumType::DamBreak:
            return Config::DamBreak{};
        case Scenario::EnumType::Empty:
            return Config::Empty{};
        case Scenario::EnumType::GooseTest:
            return Config::GooseTest{};
        case Scenario::EnumType::Lights:
            return Config::Lights{};
        case Scenario::EnumType::Raining:
            return Config::Raining{};
        case Scenario::EnumType::Sandbox:
            return Config::Sandbox{};
        case Scenario::EnumType::TreeGermination:
            return Config::TreeGermination{};
        case Scenario::EnumType::WaterEqualization:
            return Config::WaterEqualization{};
        case Scenario::EnumType::Nes:
            return Config::Nes{};
    }
    return Config::Empty{};
}

} // namespace DirtSim
