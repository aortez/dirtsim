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
    UserSettings& get() { return settings_; }
    const UserSettings& get() const { return settings_; }

    void setWebSocketService(Network::WebSocketServiceInterface* wsService);
    void syncFromServerOrAssert(int timeoutMs = 2000);
    void applyServerUpdate(const DirtSim::UserSettings& settings);

    void patchOrAssert(const Api::UserSettingsPatch::Command& patch, int timeoutMs = 2000);
    void setOrAssert(const DirtSim::UserSettings& settings, int timeoutMs = 2000);
    void resetOrAssert(int timeoutMs = 2000);

private:
    Network::WebSocketServiceInterface* wsService_ = nullptr;
    UserSettings settings_{};
};

} // namespace DirtSim::Ui
