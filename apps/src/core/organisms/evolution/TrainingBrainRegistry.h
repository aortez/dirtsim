#pragma once

#include "core/organisms/OrganismType.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace DirtSim {

class World;
struct Genome;

namespace TrainingBrainKind {
inline constexpr const char* NeuralNet = "NeuralNet";
inline constexpr const char* RuleBased = "RuleBased";
inline constexpr const char* RuleBased2 = "RuleBased2";
inline constexpr const char* Random = "Random";
inline constexpr const char* WallBouncing = "WallBouncing";
inline constexpr const char* DuckBrain2 = "DuckBrain2";
} // namespace TrainingBrainKind

struct BrainRegistryKey {
    OrganismType organismType = OrganismType::TREE;
    std::string brainKind;
    std::string brainVariant;
};

inline bool operator==(const BrainRegistryKey& lhs, const BrainRegistryKey& rhs)
{
    return lhs.organismType == rhs.organismType && lhs.brainKind == rhs.brainKind
        && lhs.brainVariant == rhs.brainVariant;
}

struct BrainRegistryKeyHash {
    std::size_t operator()(const BrainRegistryKey& key) const noexcept
    {
        const auto typeHash = std::hash<int>{}(static_cast<int>(key.organismType));
        const auto kindHash = std::hash<std::string>{}(key.brainKind);
        const auto variantHash = std::hash<std::string>{}(key.brainVariant);
        return typeHash ^ (kindHash << 1) ^ (variantHash << 2);
    }
};

struct BrainRegistryEntry {
    bool requiresGenome = false;
    bool allowsMutation = false;
    std::function<OrganismId(World& world, uint32_t x, uint32_t y, const Genome* genome)> spawn;
};

class TrainingBrainRegistry {
public:
    void registerBrain(
        OrganismType organismType,
        const std::string& brainKind,
        const std::string& brainVariant,
        BrainRegistryEntry entry);

    const BrainRegistryEntry* find(
        OrganismType organismType,
        const std::string& brainKind,
        const std::string& brainVariant) const;

    static TrainingBrainRegistry createDefault();

private:
    std::unordered_map<BrainRegistryKey, BrainRegistryEntry, BrainRegistryKeyHash> entries_;
};

} // namespace DirtSim
