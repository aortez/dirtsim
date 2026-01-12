#pragma once

#include "PointLight.h"
#include "StrongType.h"
#include <functional>
#include <unordered_map>

namespace DirtSim {

using LightId = StrongType<struct LightIdTag>;
constexpr LightId INVALID_LIGHT_ID{};

class LightHandle;

/**
 * Manages point light sources with handle-based access and optional RAII cleanup.
 *
 * Provides two modes of operation:
 * 1. Manual mode: addLight() returns LightId for caller-managed lifecycle.
 * 2. RAII mode: createLight() returns LightHandle that auto-removes on destruction.
 *
 * Static lights (corner torches) should use manual mode.
 * Dynamic lights (door indicators) should use RAII mode.
 */
class LightManager {
public:
    LightManager() = default;
    ~LightManager() = default;

    // Non-copyable, movable.
    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;
    LightManager(LightManager&&) = default;
    LightManager& operator=(LightManager&&) = default;

    // Manual mode: caller must removeLight() when done.
    LightId addLight(const PointLight& light);

    // RAII mode: light removed when handle destroyed.
    [[nodiscard]] LightHandle createLight(const PointLight& light);

    // Manual removal (for manual mode or early cleanup).
    void removeLight(LightId id);

    // Access light by ID. Asserts if not found.
    PointLight& getLight(LightId id);
    const PointLight& getLight(LightId id) const;

    // Query.
    bool isValid(LightId id) const;
    size_t count() const;

    // Bulk operations.
    void clear();

    // Iteration for WorldLightCalculator.
    void forEachLight(const std::function<void(const PointLight&)>& callback) const;

private:
    friend class LightHandle;

    std::unordered_map<LightId, PointLight> lights_;
    LightId next_id_{ 1 };
};

/**
 * RAII handle that automatically removes a light when destroyed.
 *
 * Move-only to prevent double-removal. Use release() to transfer
 * ownership to manual management.
 */
class LightHandle {
public:
    LightHandle() = default;
    ~LightHandle();

    // Move-only.
    LightHandle(const LightHandle&) = delete;
    LightHandle& operator=(const LightHandle&) = delete;
    LightHandle(LightHandle&& other) noexcept;
    LightHandle& operator=(LightHandle&& other) noexcept;

    // Access.
    LightId id() const { return id_; }
    bool isValid() const;

    // Get underlying light. Asserts if handle is invalid.
    PointLight& get();
    const PointLight& get() const;

    // Release ownership without removing the light.
    LightId release();

private:
    friend class LightManager;
    LightHandle(LightManager* manager, LightId id);

    LightManager* manager_ = nullptr;
    LightId id_ = INVALID_LIGHT_ID;
};

} // namespace DirtSim
