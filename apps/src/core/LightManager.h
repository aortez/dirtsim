#pragma once

#include "Assert.h"
#include "LightTypes.h"
#include "StrongType.h"
#include <functional>
#include <unordered_map>

namespace DirtSim {

using LightId = StrongType<struct LightIdTag>;
constexpr LightId INVALID_LIGHT_ID{};

class LightHandle;

/**
 * Manages light sources with handle-based access and optional RAII cleanup.
 *
 * Provides two modes of operation:
 * 1. Manual mode: addLight() returns LightId for caller-managed lifecycle.
 * 2. RAII mode: createLight() returns LightHandle that auto-removes on destruction.
 *
 * Supports multiple light types (PointLight, SpotLight, RotatingLight) via variant.
 */
class LightManager {
public:
    LightManager() = default;
    ~LightManager() = default;

    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;
    LightManager(LightManager&&) = default;
    LightManager& operator=(LightManager&&) = default;

    LightId addLight(const Light& light);
    [[nodiscard]] LightHandle createLight(const Light& light);
    void removeLight(LightId id);

    template <typename T>
    T* getLight(LightId id)
    {
        auto it = lights_.find(id);
        if (it == lights_.end()) {
            return nullptr;
        }
        return std::get_if<T>(&it->second.getVariant());
    }

    template <typename T>
    const T* getLight(LightId id) const
    {
        auto it = lights_.find(id);
        if (it == lights_.end()) {
            return nullptr;
        }
        return std::get_if<T>(&it->second.getVariant());
    }

    bool isValid(LightId id) const;
    size_t count() const;
    void clear();

    void forEachLight(const std::function<void(LightId, const Light&)>& callback) const;

private:
    friend class LightHandle;

    std::unordered_map<LightId, Light> lights_;
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

    LightHandle(const LightHandle&) = delete;
    LightHandle& operator=(const LightHandle&) = delete;
    LightHandle(LightHandle&& other) noexcept;
    LightHandle& operator=(LightHandle&& other) noexcept;

    LightId id() const { return id_; }
    bool isValid() const;
    LightId release();

    // Get the underlying light for modification.
    template <typename T>
    T* get()
    {
        DIRTSIM_ASSERT(manager_ != nullptr && id_ != INVALID_LIGHT_ID, "Invalid LightHandle");
        return manager_->getLight<T>(id_);
    }

    template <typename T>
    const T* get() const
    {
        DIRTSIM_ASSERT(manager_ != nullptr && id_ != INVALID_LIGHT_ID, "Invalid LightHandle");
        return manager_->getLight<T>(id_);
    }

private:
    friend class LightManager;
    LightHandle(LightManager* manager, LightId id);

    LightManager* manager_ = nullptr;
    LightId id_ = INVALID_LIGHT_ID;
};

} // namespace DirtSim
