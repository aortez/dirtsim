#pragma once

#include "ui/UserSettings.h"

namespace DirtSim::Ui {

class UserSettingsManager {
public:
    virtual ~UserSettingsManager() = default;
    virtual UserSettings& get() = 0;
    virtual const UserSettings& get() const = 0;
};

class InMemoryUserSettingsManager final : public UserSettingsManager {
public:
    UserSettings& get() override { return settings_; }
    const UserSettings& get() const override { return settings_; }

private:
    UserSettings settings_{};
};

} // namespace DirtSim::Ui
