#pragma once

#include "Cell.h"
#include "CellDebug.h"
#include "ColorNames.h"
#include "Entity.h"
#include "GridBuffer.h"
#include "ReflectSerializer.h"
#include "RenderMessage.h"
#include "Vector2.h"
#include "organisms/OrganismType.h"
#include "organisms/TreeSensoryData.h"
#include "organisms/evolution/GenomeMetadata.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {

struct WorldData {
    // ===== Fields 1-10: Binary serialized (zpp_bits) =====
    // Grid dimensions and cells (1D storage for performance).
    int16_t width = 0;
    int16_t height = 0;
    std::vector<Cell> cells;              // Flat array: cells[y * width + x]
    std::vector<OrganismId> organism_ids; // Parallel to cells: organism_ids[y * width + x]

    // Simulation state.
    int32_t timestep = 0;
    double removed_mass = 0.0;
    double fps_server = 0.0;

    // Feature flags.
    bool add_particles_enabled = true;

    // Tree organism data (optional - only present when showing a tree's vision).
    std::optional<TreeSensoryData> tree_vision;

    // Entities (duck, butterfly, etc.) - sprite-based world overlays.
    std::vector<Entity> entities;

    // Cell colors computed by light calculator (float RGB).
    GridBuffer<ColorNames::RgbF> colors;

    // Organism debug info (optional - only populated for debugging stuck organisms).
    struct OrganismDebugInfo {
        OrganismId id;
        std::string type; // "DUCK", "TREE", "GOOSE".
        Vector2i anchor_cell;
        std::string material_at_anchor;   // Material type at anchor position.
        OrganismId organism_id_at_anchor; // Cell's organism_id.
        std::optional<GenomeId> genome_id;
        using serialize = zpp::bits::members<6>;
    };
    std::vector<OrganismDebugInfo> organism_debug;

    // ===== NOT binary serialized (runtime/debug only) =====
    std::vector<CellDebug> debug_info; // Debug/viz info: debug_info[y * width + x]
    std::vector<BoneData> bones;       // Bone connections for organism structural visualization.

    // Bounds checking.
    inline bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

    inline bool inBounds(Vector2s pos) const { return inBounds(pos.x, pos.y); }

    // Direct cell access methods (inline for performance).
    inline Cell& at(int x, int y)
    {
        assert(inBounds(x, y));
        return cells[static_cast<size_t>(y) * width + x];
    }

    inline const Cell& at(int x, int y) const
    {
        assert(inBounds(x, y));
        return cells[static_cast<size_t>(y) * width + x];
    }

    inline Cell& at(Vector2s pos) { return at(pos.x, pos.y); }
    inline const Cell& at(Vector2s pos) const { return at(pos.x, pos.y); }

    // Custom zpp_bits serialization (excludes debug_info and bones).
    constexpr static auto serialize(auto& archive, auto& self)
    {
        return archive(
            self.width,
            self.height,
            self.cells,
            self.timestep,
            self.removed_mass,
            self.fps_server,
            self.add_particles_enabled,
            self.tree_vision,
            self.entities,
            self.colors,
            self.organism_debug);
        // debug_info and bones intentionally excluded from binary serialization.
    }
};

/**
 * JSON serialization for OrganismDebugInfo (nested struct).
 */
inline void to_json(nlohmann::json& j, const WorldData::OrganismDebugInfo& info)
{
    j = nlohmann::json{ { "id", info.id },
                        { "type", info.type },
                        { "anchor_cell", info.anchor_cell },
                        { "material_at_anchor", info.material_at_anchor },
                        { "organism_id_at_anchor", info.organism_id_at_anchor } };
    if (info.genome_id.has_value()) {
        j["genome_id"] = info.genome_id.value();
    }
    else {
        j["genome_id"] = nullptr;
    }
}

inline void from_json(const nlohmann::json& j, WorldData::OrganismDebugInfo& info)
{
    j.at("id").get_to(info.id);
    j.at("type").get_to(info.type);
    j.at("anchor_cell").get_to(info.anchor_cell);
    j.at("material_at_anchor").get_to(info.material_at_anchor);
    j.at("organism_id_at_anchor").get_to(info.organism_id_at_anchor);
    if (j.contains("genome_id") && !j.at("genome_id").is_null()) {
        info.genome_id = j.at("genome_id").get<GenomeId>();
    }
    else {
        info.genome_id = std::nullopt;
    }
}

/**
 * Optional serialization helpers for nlohmann::json.
 */
template <typename T>
void to_json(nlohmann::json& j, const std::optional<T>& opt)
{
    if (opt.has_value()) {
        j = opt.value();
    }
    else {
        j = nullptr;
    }
}

template <typename T>
void from_json(const nlohmann::json& j, std::optional<T>& opt)
{
    if (j.is_null()) {
        opt.reset();
    }
    else {
        opt = j.get<T>();
    }
}

/**
 * ADL (Argument-Dependent Lookup) functions for nlohmann::json automatic conversion.
 */
inline void to_json(nlohmann::json& j, const WorldData& data)
{
    j = ReflectSerializer::to_json(data);
}

inline void from_json(const nlohmann::json& j, WorldData& data)
{
    data = ReflectSerializer::from_json<WorldData>(j);
    // Ensure debug_info is sized correctly after deserialization.
    size_t cell_count = static_cast<size_t>(data.width) * data.height;
    if (data.debug_info.size() != cell_count) {
        data.debug_info.resize(cell_count);
    }
}

} // namespace DirtSim
