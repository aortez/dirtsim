#include "LightManager.h"

namespace DirtSim {

// ============================================================================
// LightManager
// ============================================================================

LightId LightManager::addLight(const Light& light)
{
    LightId id = next_id_++;
    lights_[id] = light;
    return id;
}

LightHandle LightManager::createLight(const Light& light)
{
    LightId id = addLight(light);
    return LightHandle(this, id);
}

void LightManager::removeLight(LightId id)
{
    lights_.erase(id);
}

bool LightManager::isValid(LightId id) const
{
    return lights_.contains(id);
}

size_t LightManager::count() const
{
    return lights_.size();
}

void LightManager::clear()
{
    lights_.clear();
}

void LightManager::forEachLight(const std::function<void(LightId, const Light&)>& callback) const
{
    for (const auto& [id, light] : lights_) {
        callback(id, light);
    }
}

// ============================================================================
// LightHandle
// ============================================================================

LightHandle::LightHandle(LightManager* manager, LightId id) : manager_(manager), id_(id)
{}

LightHandle::~LightHandle()
{
    if (manager_ != nullptr && id_ != INVALID_LIGHT_ID) {
        manager_->removeLight(id_);
    }
}

LightHandle::LightHandle(LightHandle&& other) noexcept : manager_(other.manager_), id_(other.id_)
{
    other.manager_ = nullptr;
    other.id_ = INVALID_LIGHT_ID;
}

LightHandle& LightHandle::operator=(LightHandle&& other) noexcept
{
    if (this != &other) {
        // Remove current light if we own one.
        if (manager_ != nullptr && id_ != INVALID_LIGHT_ID) {
            manager_->removeLight(id_);
        }
        // Take ownership of other's light.
        manager_ = other.manager_;
        id_ = other.id_;
        other.manager_ = nullptr;
        other.id_ = INVALID_LIGHT_ID;
    }
    return *this;
}

bool LightHandle::isValid() const
{
    return manager_ != nullptr && id_ != INVALID_LIGHT_ID && manager_->isValid(id_);
}

LightId LightHandle::release()
{
    LightId id = id_;
    manager_ = nullptr;
    id_ = INVALID_LIGHT_ID;
    return id;
}

} // namespace DirtSim
