#pragma once

#include "MaterialType.h"
#include "Pimpl.h"
#include "Vector2.h"
#include "organisms/OrganismType.h"

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

class Timers;

namespace DirtSim {
class Cell;
struct MaterialMove;
struct WorldData;
struct PhysicsSettings;
class LightManager;
class WorldAdhesionCalculator;
class WorldCollisionCalculator;
class WorldFrictionCalculator;
class WorldLightCalculator;
class WorldPressureCalculator;
class WorldViscosityCalculator;
class GridOfCells;
struct LightBuffer;
} // namespace DirtSim

namespace DirtSim {

class OrganismManager;
class ScenarioRunner;

class World {
public:
    // Motion states for viscosity calculations.
    enum class MotionState {
        STATIC,   // Supported by surface, minimal velocity.
        FALLING,  // No support, downward velocity.
        SLIDING,  // Moving along a surface with support.
        TURBULENT // High velocity differences with neighbors.
    };

    World();
    World(int width, int height);
    ~World();

    World(const World& other) = default;
    World& operator=(const World& other) = default;
    World(World&&) = default;
    World& operator=(World&&) = default;

    // =================================================================
    // CORE SIMULATION
    // =================================================================

    void advanceTime(double deltaTimeSeconds);
    void setup();
    void applyPhysicsSettings(const PhysicsSettings& settings);

    // =================================================================
    // BLESSED API - Cell Manipulation with Organism Tracking
    // =================================================================
    //
    // Organism tracking is owned by OrganismManager (single source of truth).
    // These methods maintain the invariant between cells and organism tracking.
    //
    void swapCells(Vector2s pos1, Vector2s pos2);
    void replaceMaterialAtCell(Vector2s pos, Material::EnumType material);
    void clearCellAtPosition(Vector2s pos);

    // =================================================================
    // MATERIAL ADDITION
    // =================================================================

    // Material selection state management (for UI/API coordination).
    void setSelectedMaterial(Material::EnumType type);
    Material::EnumType getSelectedMaterial() const;

    // =================================================================
    // PHYSICS PARAMETERS
    // =================================================================

    void setDirtFragmentationFactor(double factor);

    // =================================================================
    // PRESSURE SYSTEM
    // =================================================================

    // Use getPhysicsSettings() to access pressure settings directly.

    // Calculator access methods.
    WorldPressureCalculator& getPressureCalculator();
    const WorldPressureCalculator& getPressureCalculator() const;

    WorldCollisionCalculator& getCollisionCalculator();
    const WorldCollisionCalculator& getCollisionCalculator() const;

    WorldLightCalculator& getLightCalculator();
    const WorldLightCalculator& getLightCalculator() const;

    // Light management.
    LightManager& getLightManager();
    const LightManager& getLightManager() const;

    WorldAdhesionCalculator& getAdhesionCalculator();
    const WorldAdhesionCalculator& getAdhesionCalculator() const;

    WorldViscosityCalculator& getViscosityCalculator();
    const WorldViscosityCalculator& getViscosityCalculator() const;

    // =================================================================
    // TIME REVERSAL (NO-OP)
    // =================================================================

    void enableTimeReversal(bool enabled);
    bool isTimeReversalEnabled() const;
    void saveWorldState();
    bool canGoBackward() const;
    bool canGoForward() const;
    void goBackward();
    void goForward();
    void clearHistory();
    size_t getHistorySize() const;

    // World-specific wall setup behavior
    void setWallsEnabled(bool enabled);

    // COHESION PHYSICS CONTROL
    void setCohesionBindForceEnabled(bool enabled);
    bool isCohesionBindForceEnabled() const;

    void setCohesionComForceEnabled(bool enabled);
    bool isCohesionComForceEnabled() const;

    void setCohesionComForceStrength(double strength);
    double getCohesionComForceStrength() const;

    void setAdhesionStrength(double strength);
    double getAdhesionStrength() const;

    void setAdhesionEnabled(bool enabled);
    bool isAdhesionEnabled() const;

    void setCohesionBindForceStrength(double strength);
    double getCohesionBindForceStrength() const;

    // Viscosity control.
    void setViscosityStrength(double strength);
    double getViscosityStrength() const;

    // Friction control (velocity-dependent viscosity).
    void setFrictionStrength(double strength);
    double getFrictionStrength() const;

    void setCOMCohesionRange(int range);
    int getCOMCohesionRange() const;

    // Motion state multiplier calculation (for viscosity and other systems).

    // AIR RESISTANCE CONTROL
    void setAirResistanceEnabled(bool enabled);
    bool isAirResistanceEnabled() const;
    void setAirResistanceStrength(double strength);
    double getAirResistanceStrength() const;

    // COM cohesion mode removed - always uses ORIGINAL implementation

    // GRID MANAGEMENT
    void resizeGrid(int16_t newWidth, int16_t newHeight);

    // PERFORMANCE AND DEBUGGING
    void dumpTimerStats() const;
    void markUserInput();
    std::string settingsToString() const;
    Timers& getTimers();
    const Timers& getTimers() const;

    // =================================================================
    // WORLD-SPECIFIC METHODS
    // =================================================================

    // Add material at specific cell coordinates.
    void addMaterialAtCell(Vector2s pos, Material::EnumType type, float amount = 1.0f);
    void addMaterialAtCell(int x, int y, Material::EnumType type, float amount = 1.0f);

    static constexpr double MIN_MATTER_THRESHOLD = 0.001; // Minimum matter to process.

    // Mass-based COM cohesion constants
    static constexpr double COM_COHESION_INNER_THRESHOLD =
        0.5; // COM must be > 0.5 from center to activate
    static constexpr double COM_COHESION_MIN_DISTANCE = 0.1; // Prevent division by near-zero
    static constexpr double COM_COHESION_MAX_FORCE = 5.0;    // Cap maximum force magnitude.

    // =================================================================
    // FORCE CALCULATION METHODS
    // =================================================================

    // Material transfer computation - computes moves without processing them.
    std::vector<MaterialMove> computeMaterialMoves(double deltaTime);

    // =================================================================
    // JSON SERIALIZATION
    // =================================================================

    // Serialize complete world state to JSON (lossless).
    nlohmann::json toJSON() const;

    // Deserialize world state from JSON.
    void fromJSON(const nlohmann::json& doc);

    // =================================================================
    // UTILITY METHODS
    // =================================================================

    std::string toAsciiDiagram() const;

    void spawnMaterialBall(Material::EnumType material, Vector2s center);

    // World state data - public accessors for Pimpl-stored state.
    WorldData& getData();
    const WorldData& getData() const;

    // Grid cache for debug info access.
    GridOfCells& getGrid();
    const GridOfCells& getGrid() const;

    // Physics settings - public accessors for Pimpl-stored settings.
    PhysicsSettings& getPhysicsSettings();
    const PhysicsSettings& getPhysicsSettings() const;

    const LightBuffer& getRawLightBuffer() const;

    // WorldInterface hook implementations (rarely overridden - can be public).
    void onPreResize(int16_t newWidth, int16_t newHeight);
    bool shouldResize(int16_t newWidth, int16_t newHeight) const;

    // =================================================================
    // CONFIGURATION (public - direct access preferred)
    // =================================================================

    // Physics parameters (TODO: migrate to WorldData).
    // NOTE: Most physics parameters now use physicsSettings as single source of truth.
    bool cohesion_bind_force_enabled_;
    double cohesion_bind_force_strength_;
    int com_cohesion_range_;
    bool air_resistance_enabled_;
    double air_resistance_strength_;
    Material::EnumType selected_material_;

    struct Impl;
    Pimpl<Impl> pImpl;

    std::unique_ptr<class OrganismManager> organism_manager_;

    // Accessor for organism manager.
    class OrganismManager& getOrganismManager() { return *organism_manager_; }
    const class OrganismManager& getOrganismManager() const { return *organism_manager_; }

    // Scenario - called during advanceTime after force clear, before force application.
    void setScenario(ScenarioRunner* scenario) { scenario_ = scenario; }
    ScenarioRunner* getScenario() const { return scenario_; }

    std::unique_ptr<std::mt19937> rng_;

    void setRandomSeed(uint32_t seed);

private:
    ScenarioRunner* scenario_ = nullptr;

    void clearPendingForces();
    void applyGravity();
    void applyAirResistance();
    void applyCohesionForces(const GridOfCells& grid);
    void applyPressureForces();
    void resolveForces(double deltaTime, const GridOfCells& grid);
    void resolveRigidBodies(double deltaTime);
    void pruneDisconnectedFragments();
    Vector2d computeOrganismSupportForce(
        const std::vector<Vector2i>& organism_cells, OrganismId organism_id) const;
    void processVelocityLimiting(double deltaTime);
    void processMaterialMoves();
    void setupBoundaryWalls();
    void ensureGridCacheFresh(const char* timerName);
    void rebuildGridCache(const char* timerName);
    void markGridCacheDirty();

    // Coordinate conversion helpers (can be public if needed).
    void pixelToCell(int pixelX, int pixelY, int& cellX, int& cellY) const;
    Vector2i pixelToCell(int pixelX, int pixelY) const;
    bool isValidCell(int x, int y) const;
    bool isValidCell(const Vector2i& pos) const;
};

/**
 * ADL (Argument-Dependent Lookup) functions for nlohmann::json automatic conversion.
 */
void to_json(nlohmann::json& j, World::MotionState state);
void from_json(const nlohmann::json& j, World::MotionState& state);

} // namespace DirtSim
