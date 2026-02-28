#pragma once

#include "ui/UserSettings.h"

namespace DirtSim {
namespace Api::UserSettingsGet {
struct Command;
struct Okay;
} // namespace Api::UserSettingsGet
namespace Api::UserSettingsPatch {
struct Command;
struct Okay;
} // namespace Api::UserSettingsPatch
namespace Api::UserSettingsReset {
struct Command;
struct Okay;
} // namespace Api::UserSettingsReset
namespace Api::UserSettingsSet {
struct Command;
struct Okay;
} // namespace Api::UserSettingsSet

namespace Network {
class WebSocketServiceInterface;
} // namespace Network
} // namespace DirtSim

namespace DirtSim::Ui {

class UserSettingsManager {
public:
    virtual ~UserSettingsManager() = default;
    virtual UserSettings& get() = 0;
    virtual const UserSettings& get() const = 0;

    virtual void setWebSocketService(Network::WebSocketServiceInterface* wsService) = 0;
    virtual void syncFromServerOrAssert(int timeoutMs = 2000) = 0;
    virtual void applyServerUpdate(const DirtSim::UserSettings& settings) = 0;

    virtual void patchOrAssert(
        const Api::UserSettingsPatch::Command& patch, int timeoutMs = 2000) = 0;
    virtual void setOrAssert(const DirtSim::UserSettings& settings, int timeoutMs = 2000) = 0;
    virtual void resetOrAssert(int timeoutMs = 2000) = 0;
};

class InMemoryUserSettingsManager final : public UserSettingsManager {
public:
    UserSettings& get() override { return settings_; }
    const UserSettings& get() const override { return settings_; }

    void setWebSocketService(Network::WebSocketServiceInterface* wsService) override;
    void syncFromServerOrAssert(int timeoutMs = 2000) override;
    void applyServerUpdate(const DirtSim::UserSettings& settings) override;

    void patchOrAssert(const Api::UserSettingsPatch::Command& patch, int timeoutMs = 2000) override;
    void setOrAssert(const DirtSim::UserSettings& settings, int timeoutMs = 2000) override;
    void resetOrAssert(int timeoutMs = 2000) override;

private:
    Network::WebSocketServiceInterface* wsService_ = nullptr;
    UserSettings settings_{};
};

} // namespace DirtSim::Ui
