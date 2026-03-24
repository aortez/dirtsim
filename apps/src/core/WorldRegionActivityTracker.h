#pragma once

#include "RegionDebugInfo.h"
#include "WorldCalculatorBase.h"

#include <cstdint>
#include <vector>

namespace DirtSim {

class GridOfCells;
class World;

struct RegionMeta {
    RegionState state = RegionState::Awake;
    uint16_t quiet_frames = 0;
    WakeReason last_wake_reason = WakeReason::None;
    uint32_t last_wake_step = 0;
};

struct RegionSummary {
    bool has_mac_bulk_water = false;
    bool has_mac_water_interface = false;
    float max_velocity = 0.0f;
    float max_mac_water_face_speed = 0.0f;
    float max_mac_water_interface_face_speed = 0.0f;
    float max_mac_water_volume_delta = 0.0f;
    float max_live_pressure_delta = 0.0f;
    float max_static_load_delta = 0.0f;
    bool has_empty_adjacency = false;
    bool has_water_adjacency = false;
    bool has_mixed_material = false;
    bool has_organism = false;
    bool touched_this_frame = false;
};

class WorldRegionActivityTracker : public WorldCalculatorBase {
public:
    struct Config {
        uint16_t quiet_frames_to_sleep = 12;
        float live_pressure_delta_epsilon = 0.02f;
        float mac_water_interface_face_speed_epsilon = 0.5f;
        float mac_water_volume_delta_epsilon = 0.0005f;
        float static_load_delta_epsilon = 0.02f;
        float velocity_epsilon = 0.01f;
        bool keep_empty_adjacent_awake = true;
        bool keep_mac_water_interface_face_speed_awake = true;
        bool keep_mac_water_interface_awake = false;
        bool keep_mixed_material_awake = true;
        bool keep_organism_regions_awake = true;
        bool keep_water_adjacent_awake = true;
    };

    WorldRegionActivityTracker() = default;

    void resize(int world_width, int world_height, int blocks_x, int blocks_y);
    void reset();
    void setConfig(const Config& config);

    void noteWakeAtCell(int x, int y, WakeReason reason);
    void noteWakeAtRegion(int block_x, int block_y, WakeReason reason);
    void noteBlockedTransfer(int x, int y);
    void noteMaterialMove(int from_x, int from_y, int to_x, int to_y);

    void beginFrame(const World& world, const GridOfCells& grid, uint32_t timestep);
    void summarizeFrame(const World& world, const GridOfCells& grid, uint32_t timestep);

    bool isCellActive(int x, int y) const;
    bool isRegionActive(int block_x, int block_y) const;

    RegionState getRegionState(int block_x, int block_y) const;
    WakeReason getLastWakeReason(int block_x, int block_y) const;
    const RegionMeta& getRegionMeta(int block_x, int block_y) const;
    const RegionSummary& getRegionSummary(int block_x, int block_y) const;
    void populateDebugInfo(std::vector<RegionDebugInfo>& out) const;

    int getBlocksX() const { return blocks_x_; }
    int getBlocksY() const { return blocks_y_; }

private:
    static constexpr int REGION_SIZE = 8;
    static constexpr double MIN_GRAVITY_DELTA = 0.001;

    int cellToRegionIndex(int x, int y) const;
    int regionIndex(int block_x, int block_y) const;
    void applyWakeRequests(uint32_t timestep);
    void buildActiveMasks();
    void snapshotPreviousFields(const World& world);

    Config config_;

    int world_width_ = 0;
    int world_height_ = 0;
    int blocks_x_ = 0;
    int blocks_y_ = 0;
    double previous_gravity_ = 0.0;
    bool previous_gravity_initialized_ = false;

    std::vector<float> previous_live_pressure_;
    std::vector<float> previous_static_load_;

    std::vector<RegionMeta> region_meta_;
    std::vector<RegionSummary> region_summary_;

    std::vector<uint8_t> cell_active_;
    std::vector<uint8_t> region_active_;
    std::vector<uint8_t> region_touched_this_frame_;
    std::vector<uint8_t> region_wake_requested_;
    std::vector<WakeReason> region_pending_wake_reason_;
};

} // namespace DirtSim
