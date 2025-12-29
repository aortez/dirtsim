#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

// Forward declarations.
namespace DirtSim {
class World;

namespace Config {
struct Benchmark;
struct Clock;
struct DamBreak;
struct Empty;
struct FallingDirt;
struct Raining;
struct Sandbox;
struct TreeGermination;
struct WaterEqualization;
} // namespace Config

// Forward-declare the config variant.
using ScenarioConfig = std::variant<
    Config::Benchmark,
    Config::Clock,
    Config::DamBreak,
    Config::Empty,
    Config::FallingDirt,
    Config::Raining,
    Config::Sandbox,
    Config::TreeGermination,
    Config::WaterEqualization>;
} // namespace DirtSim

using namespace DirtSim;

struct ScenarioMetadata {
    std::string name;
    std::string description;
    std::string category;
    uint32_t requiredWidth = 0;
    uint32_t requiredHeight = 0;
};

class Scenario {
public:
    virtual ~Scenario() = default;

    virtual const ScenarioMetadata& getMetadata() const = 0;
    virtual ScenarioConfig getConfig() const = 0;
    virtual void setConfig(const ScenarioConfig& config, World& world) = 0;
    virtual void setup(World& world) = 0;
    virtual void reset(World& world) = 0;
    virtual void tick(World& world, double deltaTime) = 0;
};
