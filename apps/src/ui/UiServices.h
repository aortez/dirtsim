#pragma once

namespace DirtSim::Ui {

class ScenarioMetadataManager;
class UserSettingsManager;

class UiServices {
public:
    virtual ~UiServices() = default;

    virtual UserSettingsManager& userSettingsManager() = 0;
    virtual const UserSettingsManager& userSettingsManager() const = 0;

    virtual ScenarioMetadataManager& scenarioMetadataManager() = 0;
    virtual const ScenarioMetadataManager& scenarioMetadataManager() const = 0;
};

} // namespace DirtSim::Ui
